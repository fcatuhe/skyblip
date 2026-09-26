// L76K GNSS driver tests against models/l76k.h. The driver owns the UART and the
// NMEA parser, so what these pin down is the seam a shell depends on: a fix is
// reported exactly once, a silent receiver never reports one at all (the DFU
// health gate treats that as "cannot talk to its own peripherals"), and the
// receiver is configured rather than left on the factory defaults it boots with.
#include <cstring>
#include <string>

#include "core/protocol/adsl.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/l76k/model.h"

using namespace skyblip;

namespace {
// The wire rather than the receiver: a burst arrives one sentence at a time.
class SentenceWire : public io::Uart {
   public:
    explicit SentenceWire(models::L76k& chip) : chip_(chip) {}

    size_t write(const uint8_t* data, size_t len) override { return chip_.write(data, len); }

    size_t read(uint8_t* out, size_t cap) override {
        if (buffered_.empty()) {
            uint8_t buf[1024];
            const size_t n = chip_.read(buf, sizeof(buf));
            buffered_.assign(reinterpret_cast<const char*>(buf), n);
        }
        if (buffered_.empty()) return 0;
        const size_t line = buffered_.find('\n');
        size_t take = line == std::string::npos ? buffered_.size() : line + 1;
        if (take > cap) take = cap;
        for (size_t i = 0; i < take; i++) out[i] = static_cast<uint8_t>(buffered_[i]);
        buffered_.erase(0, take);
        return take;
    }

    size_t available() override { return buffered_.size(); }

   private:
    models::L76k& chip_;
    std::string buffered_;
};

// One service call and one drain per 10 ms runtime tick, which is how the board
// polls this part.
void run(parts::L76k& driver, models::L76k& chip, uint32_t from_ms, uint32_t to_ms) {
    for (uint32_t t = from_ms; t <= to_ms; t += 10) {
        chip.tick(t);
        driver.service(t);
        driver.poll();
    }
}

// How long bring-up takes before the first configuration sentence is on the
// wire: the wake byte, the half second the receiver spends waking, and the
// identification handshake. Written out because every timed case below has to
// clear it, and a magic 1500 in six places is how a test stops being a
// specification.
constexpr uint32_t kBringUpLeadMs = parts::L76k::kWakeDelayMs + parts::L76k::kIdentifyWindowMs;
}

TEST_CASE("l76k: a new fix is reported exactly once") {
    models::L76k chip;
    chip.fix = true;
    chip.sats = 9;
    chip.alt_m = 1200;
    chip.lat_1e7 = 485000000;
    parts::L76k gnss(chip);

    CHECK_FALSE(gnss.poll());  // the receiver has said nothing yet
    chip.tick(0);              // arms the 1 Hz cadence, emits nothing
    CHECK_FALSE(gnss.poll());

    chip.tick(1000);  // one $GPRMC + $GPGGA burst, >64 B so the chunked read wraps
    CHECK(gnss.poll());
    CHECK(gnss.solution().fix_valid);
    CHECK(gnss.solution().sats == 9);
    CHECK(gnss.solution().alt_mm == 1200000);
    CHECK(gnss.solution().lat_1e7 == doctest::Approx(485000000).epsilon(0.0001));

    CHECK_FALSE(gnss.poll());  // no new bytes: the same fix is not re-delivered
}

TEST_CASE("l76k: a wired but silent receiver never reports a fix") {
    models::L76k chip;  // never ticked: powered, connected, saying nothing
    parts::L76k gnss(chip);
    for (uint32_t t = 0; t <= 5000; t += 100) CHECK_FALSE(gnss.poll());
    CHECK(gnss.updates() == 0);
}

TEST_CASE("l76k: losing the fix is reported like any other update") {
    models::L76k chip;
    chip.fix = true;
    parts::L76k gnss(chip);
    chip.tick(0);
    chip.tick(1000);
    REQUIRE(gnss.poll());
    REQUIRE(gnss.solution().fix_valid);

    chip.fix = false;
    chip.tick(2000);
    CHECK(gnss.poll());
    CHECK_FALSE(gnss.solution().fix_valid);
}

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

// G.1.15 wants a vertical accuracy claim, and GSA alone carries the VDOP it is made of.
TEST_CASE("l76k: GSA is asked for, and the VDOP in it is the one the fix carries") {
    models::L76k chip;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 1000);

    CHECK(chip.gga_enabled);
    CHECK(chip.rmc_enabled);
    CHECK(chip.gsa_enabled);
    CHECK(parts::L76k::kGsaEnabled == chip.gsa_enabled);

    CHECK(gnss.solution().hdop_e2 == chip.hdop_e2);
    CHECK(gnss.solution().vdop_e2 == chip.vdop_e2);

    // Three sentences that say what we read, and not one byte of line time more.
    CHECK_FALSE(chip.gll_enabled);
    CHECK_FALSE(chip.gsv_enabled);
    CHECK_FALSE(chip.vtg_enabled);
}

// A 2D solution reports no VDOP, and adsl.cpp then claims no vertical accuracy at all.
TEST_CASE("l76k: a receiver reporting no VDOP leaves the fix without one") {
    models::L76k chip;
    chip.vdop_e2 = 0;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 1000);

    CHECK(gnss.solution().vdop_e2 == 0);
}

// The rate rule refuses a MISSED solution, not the wait between the top of the second and the slot.
TEST_CASE("l76k: one solution per second clears the transmit rate rule") {
    CHECK(parts::L76k::kFixPeriodMs == 1000);
    CHECK(static_cast<int32_t>(parts::L76k::kFixPeriodMs) > timing::Transmitter::kFixLagMaxMs);

    models::L76k chip;
    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 1000);

    uint32_t fixes = 0;
    for (uint32_t t = kBringUpLeadMs + 1010; t <= kBringUpLeadMs + 5010; t += 10) {
        chip.tick(t);
        gnss.service(t);
        if (gnss.poll()) fixes++;
    }
    CHECK(fixes >= 4000 / parts::L76k::kFixPeriodMs);
}

// The pinned bug: a fix published per SENTENCE reached the bus twice, the first one half updated.
TEST_CASE("l76k: a burst reaches the bus once, on the sentence that closes it") {
    models::L76k chip;
    SentenceWire wire(chip);
    parts::L76k gnss(wire, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 5000);
    REQUIRE(gnss.configured());
    REQUIRE(gnss.solution().fix_valid);

    const uint32_t at = kBringUpLeadMs + 6000;
    chip.alt_m = 1500;
    chip.tick(at);

    int published = 0;
    for (int drain = 0; drain < 8; drain++)
        if (gnss.poll(at)) published++;

    CHECK(published == 1);
    CHECK(gnss.solution().alt_mm == 1500000);
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

// The estimate own-ship falls back to with no PPS edge latched, stamped by the part that knows it.
TEST_CASE("l76k: the fix carries the part's burst-to-PPS latency") {
    models::L76k chip;
    parts::L76k gnss(chip);
    chip.tick(0);
    chip.tick(1000);
    REQUIRE(gnss.poll());

    CHECK(gnss.solution().pps_latency_ms == gnss.pps_latency_ms());
    CHECK(gnss::solution_instant_ms(gnss.solution(), 1000) == 1000 - gnss.pps_latency_ms());
}

// Nothing in the build compares the driver's baud with the devicetree's, so the driver states it.
TEST_CASE("l76k: the rate the receiver is met at is the baud the devicetree pins") {
    CHECK(parts::L76k::kBaudRate == 9600);
    CHECK(parts::L76k::kBaudCandidates[0] == parts::L76k::kBaudRate);
}

// The fix is refused as too old unless it is parsed before the radio's first transmit instant.
TEST_CASE("l76k: the whole burst closes before the direct slot opens, satellites in view and all") {
    // GGA, three GSA and RMC: 333 ms of 9600 baud line, 28 ms of 115200.
    CHECK(parts::wire_ms(parts::L76k::kBurstBytes, parts::L76k::kBaudRate) ==
          doctest::Approx(333).epsilon(0.02));
    CHECK(parts::L76k::kBurstMs == doctest::Approx(28).epsilon(0.04));

    CHECK(parts::L76k::kBurstStartMs + parts::L76k::kBurstMs < uint32_t(timing::kDirectStart));
    CHECK(parts::L76k::kBurstStartMs + parts::L76k::kSearchingBurstMs <
          uint32_t(timing::kDirectStart));
    // The same burst at the rate the receiver boots at: past the slot, and past the second.
    CHECK(parts::L76k::kBurstStartMs +
              parts::wire_ms(parts::L76k::kSearchingBurstBytes, parts::L76k::kBaudRate) >
          uint32_t(timing::kDirectStart));
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

// The gyroscope and the barometer beside this part sample far faster than it reports.
TEST_CASE("l76k: the receiver solves continuously and reports on its cadence") {
    models::L76k chip;
    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    chip.alt_m = 1000;
    chip.track_deg = 90;
    chip.climb_mm_s = 1500;
    chip.turn_dps = 3;

    chip.tick(0);
    chip.tick(100);

    CHECK(chip.alt_mm() == 1000 * 1000 + 150);
    CHECK(chip.heading_deg() == doctest::Approx(90.3));
    CHECK(chip.available() == 0);  // a tenth of a second in, it has said nothing

    chip.tick(1000);
    CHECK(chip.available() > 0);
}

// A replayed instant must not fly the aircraft a thousand kilometres: the gap is signed.
TEST_CASE("l76k: a clock that steps backwards leaves the aircraft where it is") {
    models::L76k chip;
    chip.tick(0);
    chip.tick(1000);
    const int32_t lat_1e7 = chip.lat_1e7;
    const int32_t lon_1e7 = chip.lon_1e7;

    chip.tick(995);

    CHECK(chip.lat_1e7 == lat_1e7);
    CHECK(chip.lon_1e7 == lon_1e7);
}

// The geometry the circling scenarios rest on, checked against the model that
// produces it rather than assumed. A glider thermalling at 45 kt (23.15 m/s) and
// 13 deg/s flies radius = speed / turn rate = 102 m, a 200 m circle closed in
// 27.7 s, which is the ordinary way a glider climbs and the band
// core/traffic/alarm.h already calls circling. Same figure from the other side:
// radius = speed^2 / (g tan(bank)) puts that circle at 29 deg of bank.
TEST_CASE("l76k: a turn rate flies a circle, and it is the size the arithmetic says") {
    models::L76k chip;
    chip.solution_period_ms = 200;
    chip.speed_mm_s = 23150;  // 45 kt
    chip.track_deg = 0;
    chip.turn_dps = 13;
    const int32_t start_lat = chip.lat_1e7;
    const int32_t start_lon = chip.lon_1e7;

    int32_t east_lat = 0, east_lon = 0;
    for (uint32_t t = 0; t <= 27700; t += 100) {
        chip.tick(t);
        // A quarter of the way round a right turn from north, own-ship is due
        // east of where it started by one radius and level with the centre.
        if (t == 6900) {
            east_lat = chip.lat_1e7;
            east_lon = chip.lon_1e7;
        }
    }

    const double north_m = (east_lat - start_lat) * 11132 / 1e6;
    const double east_m = (east_lon - start_lon) * 11132 / 1e6 * 0.6626;
    MESSAGE("quarter circle: " << north_m << " m north, " << east_m << " m east");
    CHECK(east_m == doctest::Approx(102).epsilon(0.05));
    CHECK(north_m == doctest::Approx(102).epsilon(0.05));

    // A full circle later the track is back where it started and so is the
    // aircraft: the integration closes the loop rather than spiralling.
    CHECK((chip.track_deg <= 3 || chip.track_deg >= 357));
    const double closed_m = (chip.lat_1e7 - start_lat) * 11132 / 1e6;
    CHECK(closed_m < 5.0);
    CHECK(closed_m > -5.0);
}

// I, row "Fix age as validity", at the seam that matters: the board pushes
// whatever this driver hands it, so a receiver that stops talking has to be
// withdrawn HERE or the last position it managed to send stands forever. The
// pinned bug: poll() only reported when a new sentence arrived, and a silent
// receiver sends none by definition, so a unit whose antenna came off in flight
// kept transmitting the place it lost the sky.
TEST_CASE("l76k: a receiver that goes silent withdraws its fix, once") {
    models::L76k chip;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 5000);
    REQUIRE(gnss.configured());
    REQUIRE(gnss.solution().fix_valid);

    // The chip is no longer ticked: powered, wired, saying nothing. The driver
    // is serviced and drained exactly as the board does it.
    const uint32_t quiet_from = kBringUpLeadMs + 5000;
    // Bring-up has already refused one solution: the first RMC arrives before
    // the first GGA and half a burst is not a fix, which is the rule working.
    const uint32_t rejects_before = gnss.rejected();
    uint32_t withdrawn_at = 0;
    uint32_t updates = 0;
    for (uint32_t t = quiet_from + 10; t <= quiet_from + 10000; t += 10) {
        gnss.service(t);
        if (gnss.poll()) {
            updates++;
            if (!gnss.solution().fix_valid && withdrawn_at == 0) withdrawn_at = t;
        }
    }

    CHECK(withdrawn_at > quiet_from);
    CHECK(withdrawn_at - quiet_from <= gnss::kSentenceMaxAgeMs + 20);
    CHECK(gnss.reject_reason() == gnss::FixReject::Stale);
    // One withdrawal, not one per poll: the bus queue is two deep and the
    // downstream services react to changes.
    CHECK(updates == 1);
    CHECK(gnss.rejected() == rejects_before + 1);
}

// I, row "Date and jump sanity", through the driver: the receiver claims a
// solution, its position is perfectly ordinary, and its date is the MTK 1980
// lie. What reaches the bus is not a fix.
TEST_CASE("l76k: a solution dated 1980 does not reach the bus as a fix") {
    models::L76k chip;
    chip.date = "010180";
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 5000);

    CHECK(gnss.configured());                // the receiver is fine, it is being obeyed
    CHECK(gnss.updates() > 0);               // and it is talking
    CHECK_FALSE(gnss.solution().fix_valid);  // and none of that is a fix
    CHECK(gnss.reject_reason() == gnss::FixReject::NoDate);

    // The almanac lands and the date becomes real: the fix follows, with no
    // reconfiguration and no reset.
    chip.date = "010125";
    run(gnss, chip, kBringUpLeadMs + 5010, kBringUpLeadMs + 6000);
    CHECK(gnss.solution().fix_valid);
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

// The levels cost a set of sentences a second, so only the page that draws them asks for them.
TEST_CASE("l76k: satellites in view are asked for, and given up again, one sentence each way") {
    models::L76k chip;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 4000);
    REQUIRE(gnss.configured());
    REQUIRE_FALSE(chip.gsv_enabled);  // the bring-up sentence set switches it off
    REQUIRE(gnss.sky().count() == 0);

    const uint32_t commands = chip.commands_seen;
    gnss.request_satellites_in_view(true);
    run(gnss, chip, kBringUpLeadMs + 4010, kBringUpLeadMs + 7000);
    CHECK(chip.commands_seen == commands + 1);
    CHECK(chip.gsv_enabled);
    CHECK(gnss.satellites_in_view_live());
    CHECK(gnss.sky().count() == chip.gps_in_view + chip.beidou_in_view + chip.glonass_in_view);
    CHECK(gnss.sky().in_view_of(gnss::System::Gps) == chip.gps_in_view);
    CHECK(gnss.sky().in_view_of(gnss::System::Beidou) == chip.beidou_in_view);

    // Nothing else in the sentence set moved: the nulls in $PCAS03 mean "keep".
    CHECK(chip.gga_enabled);
    CHECK(chip.rmc_enabled);
    CHECK(chip.gsa_enabled);
    CHECK_FALSE(chip.gll_enabled);
    CHECK_FALSE(chip.vtg_enabled);

    gnss.request_satellites_in_view(false);
    run(gnss, chip, kBringUpLeadMs + 7010, kBringUpLeadMs + 9000);
    CHECK(chip.commands_seen == commands + 2);
    CHECK_FALSE(chip.gsv_enabled);
    CHECK_FALSE(gnss.satellites_in_view_live());

    // Asking again for what is already so costs nothing.
    gnss.request_satellites_in_view(false);
    run(gnss, chip, kBringUpLeadMs + 9010, kBringUpLeadMs + 11000);
    CHECK(chip.commands_seen == commands + 2);
}

// At the rate the receiver boots at, the widest GSV set did not fit in the second the fix rides.
TEST_CASE("l76k: the satellites-in-view burst fits the second only at the raised rate") {
    CHECK(parts::wire_ms(parts::L76k::kSearchingBurstBytes, parts::L76k::kBaudRate) >
          parts::L76k::kSolutionPeriodMs);
    CHECK(parts::L76k::kSearchingBurstMs < parts::L76k::kSolutionPeriodMs);
    CHECK(parts::L76k::kSolutionPeriodMs == parts::L76k::kFixPeriodMs);
}

// A receiver still searching reports satellites in view it is not yet tracking.
TEST_CASE("l76k: a satellite in view with no level is not a satellite at zero dB-Hz") {
    models::L76k chip;
    chip.fix = false;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 4000);
    gnss.request_satellites_in_view(true);
    run(gnss, chip, kBringUpLeadMs + 4010, kBringUpLeadMs + 7000);

    REQUIRE(gnss.sky().count() > 0);
    int tracked = 0, silent = 0;
    for (int i = 0; i < gnss.sky().count(); i++)
        (gnss.sky().at(i).cn0_dbhz > 0 ? tracked : silent)++;
    CHECK(tracked > 0);
    CHECK(silent > 0);
    CHECK(gnss.sky().in_use() == 0);  // nothing solved, so nothing is in a solution
}
