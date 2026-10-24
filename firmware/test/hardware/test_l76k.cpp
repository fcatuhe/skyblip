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
#include "test/support/l76k_rig.h"

using namespace skyblip;

namespace {
// The wire rather than the receiver: a burst arrives one sentence at a time.
class SentenceWire : public io::Uart {
   public:
    explicit SentenceWire(models::L76k& chip) : chip_(chip) {}

    size_t write(const uint8_t* data, size_t bytes) override { return chip_.write(data, bytes); }

    size_t read(uint8_t* out, size_t capacity) override {
        if (buffered_.empty()) {
            uint8_t buffer[1024];
            const size_t bytes = chip_.read(buffer, sizeof(buffer));
            buffered_.assign(reinterpret_cast<const char*>(buffer), bytes);
        }
        if (buffered_.empty()) return 0;
        const size_t line = buffered_.find('\n');
        size_t take = line == std::string::npos ? buffered_.size() : line + 1;
        if (take > capacity) take = capacity;
        for (size_t i = 0; i < take; i++) out[i] = static_cast<uint8_t>(buffered_[i]);
        buffered_.erase(0, take);
        return take;
    }

    size_t available() override { return buffered_.size(); }

   private:
    models::L76k& chip_;
    std::string buffered_;
};

}

TEST_CASE("l76k: a new fix is reported exactly once") {
    models::L76k chip;
    chip.fix_valid = true;
    chip.satellites = 9;
    chip.altitude_m = 1200;
    chip.latitude_1e7 = 485000000;
    parts::L76k gnss(chip);

    CHECK_FALSE(gnss.poll());  // the receiver has said nothing yet
    chip.tick(0);              // arms the 1 Hz cadence, emits nothing
    CHECK_FALSE(gnss.poll());

    chip.tick(1000);  // one $GPRMC + $GPGGA burst, >64 B so the chunked read wraps
    CHECK(gnss.poll());
    CHECK(gnss.solution().fix_valid);
    CHECK(gnss.solution().satellites == 9);
    CHECK(gnss.solution().altitude_hae_mm == 1200000);
    CHECK(gnss.solution().latitude_1e7 == doctest::Approx(485000000).epsilon(0.0001));

    CHECK_FALSE(gnss.poll());  // no new bytes: the same fix is not re-delivered
}

TEST_CASE("l76k: a wired but silent receiver never reports a fix") {
    models::L76k chip;  // never ticked: powered, connected, saying nothing
    parts::L76k gnss(chip);
    for (uint32_t now_ms = 0; now_ms <= 5000; now_ms += 100) CHECK_FALSE(gnss.poll());
    CHECK(gnss.sentences() == 0);
}

TEST_CASE("l76k: losing the fix is reported like any other update") {
    models::L76k chip;
    chip.fix_valid = true;
    parts::L76k gnss(chip);
    chip.tick(0);
    chip.tick(1000);
    REQUIRE(gnss.poll());
    REQUIRE(gnss.solution().fix_valid);

    chip.fix_valid = false;
    chip.tick(2000);
    CHECK(gnss.poll());
    CHECK_FALSE(gnss.solution().fix_valid);
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
    CHECK_FALSE(chip.gsv_enabled());
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
    CHECK(parts::L76k::kSolutionPeriodMs == 1000);
    CHECK(static_cast<int32_t>(parts::L76k::kSolutionPeriodMs) > timing::Transmitter::kFixMaxAgeMs);

    models::L76k chip;
    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 1000);

    uint32_t fixes = 0;
    for (uint32_t now_ms = kBringUpLeadMs + 1010; now_ms <= kBringUpLeadMs + 5010; now_ms += 10) {
        chip.tick(now_ms);
        gnss.service(now_ms);
        if (gnss.poll()) fixes++;
    }
    CHECK(fixes >= 4000 / parts::L76k::kSolutionPeriodMs);
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
    chip.altitude_m = 1500;
    chip.tick(at);

    int published = 0;
    for (int drain = 0; drain < 8; drain++)
        if (gnss.poll(at)) published++;

    CHECK(published == 1);
    CHECK(gnss.solution().altitude_hae_mm == 1500000);
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
    CHECK(parts::L76k::kFactoryBaudRate == 9600);
    CHECK(parts::L76k::kBaudCandidates[0] == parts::L76k::kFactoryBaudRate);
}

// The fix is refused as too old unless it is parsed before the radio's first transmit instant.
TEST_CASE("l76k: the whole burst closes before the direct slot opens, satellites in view and all") {
    // GGA, three GSA and RMC: 333 ms of 9600 baud line, 28 ms of 115200.
    CHECK(parts::wire_ms(parts::L76k::kBurstBytes, parts::L76k::kFactoryBaudRate) ==
          doctest::Approx(333).epsilon(0.02));
    CHECK(parts::L76k::kBurstMs == doctest::Approx(28).epsilon(0.04));

    CHECK(parts::L76k::kBurstStartMs + parts::L76k::kBurstMs < uint32_t(timing::kDirectStartMs));
    CHECK(parts::L76k::kBurstStartMs + parts::L76k::kSearchingBurstMs <
          uint32_t(timing::kDirectStartMs));
    // The same burst at the rate the receiver boots at: past the slot, and past the second.
    CHECK(parts::L76k::kBurstStartMs +
              parts::wire_ms(parts::L76k::kSearchingBurstBytes, parts::L76k::kFactoryBaudRate) >
          uint32_t(timing::kDirectStartMs));
}

// The gyroscope and the barometer beside this part sample far faster than it reports.
TEST_CASE("l76k: the receiver solves continuously and reports on its cadence") {
    models::L76k chip;
    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    chip.altitude_m = 1000;
    chip.track_deg = 90;
    chip.climb_mm_s = 1500;
    chip.turn_dps = 3;

    chip.tick(0);
    chip.tick(100);

    CHECK(chip.altitude_mm() == 1000 * 1000 + 150);
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
    const int32_t latitude_1e7 = chip.latitude_1e7;
    const int32_t longitude_1e7 = chip.longitude_1e7;

    chip.tick(995);

    CHECK(chip.latitude_1e7 == latitude_1e7);
    CHECK(chip.longitude_1e7 == longitude_1e7);
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
    const int32_t start_latitude = chip.latitude_1e7;
    const int32_t start_longitude = chip.longitude_1e7;

    int32_t east_latitude = 0, east_longitude = 0;
    for (uint32_t now_ms = 0; now_ms <= 27700; now_ms += 100) {
        chip.tick(now_ms);
        // A quarter of the way round a right turn from north, own-ship is due
        // east of where it started by one radius and level with the centre.
        if (now_ms == 6900) {
            east_latitude = chip.latitude_1e7;
            east_longitude = chip.longitude_1e7;
        }
    }

    const double north_m = (east_latitude - start_latitude) * 11132 / 1e6;
    const double east_m = (east_longitude - start_longitude) * 11132 / 1e6 * 0.6626;
    MESSAGE("quarter circle: " << north_m << " m north, " << east_m << " m east");
    CHECK(east_m == doctest::Approx(102).epsilon(0.05));
    CHECK(north_m == doctest::Approx(102).epsilon(0.05));

    // A full circle later the track is back where it started and so is the
    // aircraft: the integration closes the loop rather than spiralling.
    CHECK((chip.track_deg <= 3 || chip.track_deg >= 357));
    const double closed_m = (chip.latitude_1e7 - start_latitude) * 11132 / 1e6;
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
    for (uint32_t now_ms = quiet_from + 10; now_ms <= quiet_from + 10000; now_ms += 10) {
        gnss.service(now_ms);
        if (gnss.poll()) {
            updates++;
            if (!gnss.solution().fix_valid && withdrawn_at == 0) withdrawn_at = now_ms;
        }
    }

    CHECK(withdrawn_at > quiet_from);
    CHECK(withdrawn_at - quiet_from <= gnss::kSentenceMaxAgeMs + 20);
    CHECK(gnss.last_reject() == gnss::FixReject::Stale);
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
    CHECK(gnss.sentences() > 0);             // and it is talking
    CHECK_FALSE(gnss.solution().fix_valid);  // and none of that is a fix
    CHECK(gnss.last_reject() == gnss::FixReject::NoDate);

    // The almanac lands and the date becomes real: the fix follows, with no
    // reconfiguration and no reset.
    chip.date = "010125";
    run(gnss, chip, kBringUpLeadMs + 5010, kBringUpLeadMs + 6000);
    CHECK(gnss.solution().fix_valid);
}
