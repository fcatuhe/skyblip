// Bring-up: the receiver is woken, identified, configured and raised to the port's rate, and
// found again after a wrong baud, a factory reset or a cold start.
#include <cstring>
#include <string>

#include "doctest/doctest.h"
#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/l76k/model.h"
#include "test/support/l76k_rig.h"

using namespace skyblip;

// The receiver boots with pedestrian smoothing, a factory sentence set and 1 Hz.
// SoftRF sends exactly three commands to this part 250 ms apart
// (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:1029-1057); the fourth, the fix
// rate, is ours. The model validates the NMEA checksum before applying anything,
// so a driver that miscomputes one configures nothing.
TEST_CASE("l76k: bring-up configures constellations, sentences, dynamic model and rate") {
    models::L76k chip;
    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    parts::L76k gnss(chip, chip);

    run(gnss, chip, 0, kBringUpLeadMs + 1000);

    CHECK(chip.commands_seen == parts::L76k::kCommandCount + 1);  // + $PCAS06
    CHECK(chip.commands_rejected == 0);
    CHECK(chip.constellations == 7);  // GPS + GLONASS + BeiDou
    CHECK(chip.sentence_set_applied);
    CHECK(chip.aviation_dynamic_model());
    CHECK(chip.solution_period_ms == parts::L76k::kFixPeriodMs);
}

// I, rows "A wake byte before probing" and "Receiver identification", in one
// assertion: the order the datasheet and SoftRF want. The wake byte comes first
// because the receiver is deaf until UART activity has woken it
// (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:1383-1387), the identification
// handshake comes next because there is no point configuring a part that is not
// the part we think it is (.../GNSS.cpp:981-1010), and the four configuration
// sentences follow in SoftRF's order.
TEST_CASE("l76k: the receiver is woken, then identified, then configured, in that order") {
    models::L76k chip;
    chip.asleep = true;  // cold: it has heard nothing since power came up
    parts::L76k gnss(chip, chip);

    run(gnss, chip, 0, kBringUpLeadMs + 1000);

    CHECK(chip.heard() == "WAKE,PCAS06,PCAS04,PCAS03,PCAS11,PCAS02,");
    CHECK(chip.wake_bytes == 1);
    CHECK(gnss.identified());
    CHECK(std::string(gnss.firmware_version()) == "URANUS5,V5.1.0.0");
}

// The bug the wake byte exists to prevent, at the chip: everything said inside
// the wake window lands on a receiver that is not listening yet. Written against
// the model directly, because a driver that gets this right can no longer
// demonstrate it.
TEST_CASE("l76k: a cold receiver eats whatever is said in the first 500 ms") {
    models::L76k chip;
    chip.asleep = true;
    chip.tick(0);

    const char* first = "$PCAS04,7*1E\r\n";
    chip.write(reinterpret_cast<const uint8_t*>(first), std::strlen(first));
    CHECK(chip.constellations == 0);  // gone: the receiver was waking up
    CHECK(chip.deaf_bytes > 0);

    // Still inside the window a moment later, still deaf.
    chip.tick(models::L76k::kWakeMs - 10);
    chip.write(reinterpret_cast<const uint8_t*>(first), std::strlen(first));
    CHECK(chip.constellations == 0);

    // Past it, awake, and it obeys.
    chip.tick(models::L76k::kWakeMs);
    chip.write(reinterpret_cast<const uint8_t*>(first), std::strlen(first));
    CHECK(chip.constellations == 7);
}

// A part that takes $PCAS sentences but never introduces itself is a clone, or a
// receiver whose TX line is broken in one direction. It still gets configured,
// because the alternative on this board is leaving it on factory defaults, but
// the self-test says the handshake failed and support has that fact.
TEST_CASE("l76k: an unidentified receiver is still configured, and still says so") {
    models::L76k chip;
    chip.answers_identification = false;
    parts::L76k gnss(chip, chip);

    run(gnss, chip, 0, kBringUpLeadMs + 5000);

    CHECK_FALSE(gnss.identified());
    CHECK(gnss.firmware_version()[0] == 0);
    CHECK(chip.aviation_dynamic_model());
    CHECK(gnss.configured());
}

// Nothing acknowledges a $PCAS sentence, so the driver treats the receiver's own
// cadence as the acknowledgement. A receiver that hears every command and obeys
// none is a degraded capability, not a working one on silent defaults.
TEST_CASE("l76k: a receiver that never obeys is reported degraded, not assumed good") {
    models::L76k chip;
    chip.accepts_commands = false;
    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    parts::L76k gnss(chip, chip);

    run(gnss, chip, 0, 30000);

    // Every attempt wakes the receiver and asks it who it is before it starts
    // configuring, so an attempt is five sentences, not four.
    CHECK(chip.commands_seen ==
          (parts::L76k::kCommandCount + 1) * uint32_t(parts::L76k::kMaxConfigAttempts));
    // It is talking, so the rate is right and autobaud must not run: walking the
    // baud rates here would throw away the sentences we do get.
    CHECK(chip.baud_changes == 0);
    CHECK(gnss.degraded());
    CHECK_FALSE(gnss.configured());
    CHECK(gnss.updates() > 0);  // it is talking, just not listening
}

TEST_CASE("l76k: a receiver that obeys is reported configured") {
    models::L76k chip;
    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    parts::L76k gnss(chip, chip);
    CHECK(gnss.config_state() == parts::L76k::Config::Idle);

    run(gnss, chip, 0, 10000);

    CHECK(gnss.configured());
    CHECK_FALSE(gnss.degraded());
}

// A receiver moved to a rate the port cannot follow is a GNSS-less device that looks fitted.
TEST_CASE("l76k: bring-up raises the receiver and the port together") {
    models::L76k chip;
    parts::L76k gnss(chip, chip);
    REQUIRE(gnss.baud_rate() == parts::L76k::kBaudRate);

    run(gnss, chip, 0, kBringUpLeadMs + 2 * parts::L76k::kVerifyWindowMs);

    CHECK(gnss.configured());
    CHECK(gnss.baud_rate() == parts::L76k::kTargetBaudRate);
    CHECK(chip.baud == parts::L76k::kTargetBaudRate);
    CHECK(chip.port_baud() == parts::L76k::kTargetBaudRate);
    CHECK(gnss.solution().fix_valid);
    CHECK(gnss.solution().pps_latency_ms == parts::L76k::kBurstMs);
}

// A clone that takes every other $PCAS sentence and ignores this one leaves us deaf at 115200.
TEST_CASE("l76k: a receiver that ignores the rate command is followed back down to 9600") {
    models::L76k chip;
    chip.refuses_baud_command = true;
    parts::L76k gnss(chip, chip);

    run(gnss, chip, 0, kBringUpLeadMs + 3 * parts::L76k::kVerifyWindowMs);

    CHECK(chip.baud == parts::L76k::kBaudRate);
    CHECK(gnss.baud_rate() == parts::L76k::kBaudRate);
    CHECK(chip.port_baud() == parts::L76k::kBaudRate);
    CHECK(gnss.configured());
    CHECK(gnss.solution().fix_valid);
}

// Without a rate port the receiver must be left where it boots, not asked to move alone.
TEST_CASE("l76k: a port that cannot retune never moves the receiver") {
    models::L76k chip;
    parts::L76k gnss(chip);  // no rate port: io::FixedUartRate

    run(gnss, chip, 0, kBringUpLeadMs + 2 * parts::L76k::kVerifyWindowMs);

    CHECK(chip.baud == parts::L76k::kBaudRate);
    CHECK(gnss.baud_rate() == parts::L76k::kBaudRate);
    CHECK(gnss.configured());
}

// I, row "Baud detection and recovery". A receiver that comes up at another rate
// (a returned unit somebody reflashed, a module whose backup domain kept a
// $PCAS01) is silently GNSS-less forever if 9600 is an assumption. moshe-braner
// walks the rates until NMEA appears (MB .../src/driver/GNSS.cpp:1700-1739); OGN
// steps to the next rate after two seconds of nothing (src/gps.cpp:1205-1222).
TEST_CASE("l76k: a receiver at the wrong baud is found, not written off") {
    models::L76k chip;
    chip.baud = 38400;  // not what the devicetree pins
    parts::L76k gnss(chip, chip);
    REQUIRE(gnss.baud_rate() == parts::L76k::kBaudRate);

    run(gnss, chip, 0, 30000);

    CHECK(gnss.configured());
    CHECK_FALSE(gnss.degraded());
    CHECK(gnss.solution().fix_valid);
    // Found at 38400, then moved: 9600, 115200, 38400, and 115200 again for good.
    CHECK(gnss.baud_rate() == parts::L76k::kTargetBaudRate);
    CHECK(chip.port_baud() == parts::L76k::kTargetBaudRate);
    CHECK(chip.baud_changes == 3);
}

// Absent hardware is a capability. A platform whose UART cannot be retuned hands
// the driver the null rate control, and the receiver at the wrong baud degrades
// after the ordinary number of attempts instead of the driver pretending it
// changed something.
TEST_CASE("l76k: without a retunable port, autobaud is a capability we do not have") {
    models::L76k chip;
    chip.baud = 38400;
    parts::L76k gnss(chip);  // no rate port: io::FixedUartRate

    run(gnss, chip, 0, 30000);

    CHECK(gnss.baud_rate() == parts::L76k::kBaudRate);
    CHECK(chip.baud_changes == 0);
    CHECK(gnss.degraded());
    CHECK(gnss.updates() == 0);
}

// I, row "Cold start and factory reset". A receiver with a poisoned almanac
// takes twenty minutes to sort itself out, which a pilot reads as a broken
// device. $PCAS10 is the way out, and the factory variant takes our
// configuration with it: the driver has to notice and put it back, or the escape
// hatch leaves the receiver on pedestrian smoothing at 1 Hz.
TEST_CASE("l76k: a factory reset is recoverable, and the configuration goes back") {
    models::L76k chip;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 5000);
    REQUIRE(gnss.configured());
    REQUIRE(chip.aviation_dynamic_model());

    gnss.request_restart(ports::Restart::Factory);
    uint32_t t = kBringUpLeadMs + 5010;
    chip.tick(t);
    gnss.service(t);
    CHECK(chip.factory_resets == 1);
    CHECK(chip.heard().rfind("PCAS10,") == chip.heard().size() - 7);
    // The receiver that comes back is not the one we configured.
    CHECK_FALSE(chip.aviation_dynamic_model());
    CHECK(chip.solution_period_ms == models::L76k::kFactoryPeriodMs);
    CHECK(gnss.config_state() == parts::L76k::Config::Restarting);

    run(gnss, chip, t + 10, t + 15000);
    CHECK(gnss.configured());
    CHECK(chip.aviation_dynamic_model());
    CHECK(chip.solution_period_ms == parts::L76k::kFixPeriodMs);

    // And the fix comes back on its own once the receiver has an almanac again,
    // which is the whole point: the pilot is told to wait, not to send it back.
    CHECK_FALSE(gnss.solution().fix_valid);
    run(gnss, chip, t + 15010, t + models::L76k::kColdStartTtffMs + 3000);
    CHECK(gnss.solution().fix_valid);
}

// A cold start throws the orbit data away and keeps everything we configured:
// that is the difference between $PCAS10,2 and $PCAS10,3, and it is the one a
// support script should reach for first.
TEST_CASE("l76k: a cold start keeps the configuration and loses only the almanac") {
    models::L76k chip;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 5000);
    REQUIRE(gnss.solution().fix_valid);

    gnss.request_restart(ports::Restart::Cold);
    const uint32_t t = kBringUpLeadMs + 5010;
    chip.tick(t);
    gnss.service(t);

    CHECK(chip.restarts == 1);
    CHECK(chip.factory_resets == 0);
    CHECK(chip.aviation_dynamic_model());
    CHECK(chip.solution_period_ms == parts::L76k::kFixPeriodMs);
    CHECK(gnss.config_state() == parts::L76k::Config::Ready);

    run(gnss, chip, t + 10, t + 5000);
    CHECK_FALSE(gnss.solution().fix_valid);
    run(gnss, chip, t + 5010, t + models::L76k::kColdStartTtffMs + 3000);
    CHECK(gnss.solution().fix_valid);
}
