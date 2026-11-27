// The AG3335's L5 band: read from $PAIR021 and $PAIR105, and turned on across a GNSS power cycle.
#include <string>

#include "doctest/doctest.h"
#include "hardware/parts/ag3335/ag3335.h"
#include "hardware/parts/ag3335/model.h"

using namespace skyblip;

namespace {

void run(parts::Ag3335& driver, models::Ag3335& chip, uint32_t from_ms, uint32_t to_ms) {
    for (uint32_t now_ms = from_ms; now_ms <= to_ms; now_ms += 10) {
        chip.tick(now_ms);
        driver.service(now_ms);
        driver.poll();
    }
}

parts::PairReader::Heard feed(parts::PairReader& reader, const char* line) {
    parts::PairReader::Heard heard = parts::PairReader::Heard::Nothing;
    for (const char* at = line; *at; at++) {
        const parts::PairReader::Heard now = reader.feed(*at);
        if (now != parts::PairReader::Heard::Nothing) heard = now;
    }
    return heard;
}

// Twice the hold the driver keeps between two GNSS power switches, and the rest of the sequence.
constexpr uint32_t kSwitchedBringUpMs = 2 * parts::Ag3335::kPowerSwitchHoldMs + 2000;

}  // namespace

TEST_CASE("ag3335: a dual-band build with the switch on is asked once and reads L1 and L5") {
    models::Ag3335 chip;
    parts::Ag3335 gnss(ports::SolutionRate::Second, chip);

    run(gnss, chip, 0, 2000);

    CHECK(gnss.configured());
    CHECK(gnss.band() == ports::GnssBand::L1L5);
    CHECK(chip.power_switches == 0);
    CHECK(chip.tracks_l5());
}

TEST_CASE("ag3335: a dual-band switch found off is turned on with the GNSS system off") {
    models::Ag3335 chip;
    chip.dual_band = false;
    parts::Ag3335 gnss(ports::SolutionRate::Second, chip);

    run(gnss, chip, 0, kSwitchedBringUpMs);

    CHECK(gnss.configured());
    CHECK(gnss.band() == ports::GnssBand::L1L5);
    CHECK(chip.heard().find("PAIR105,PAIR003,PAIR104,PAIR002,PAIR105,PAIR066,") !=
          std::string::npos);
    CHECK(chip.dual_band);
    CHECK(chip.gnss_on);
    CHECK(chip.band_set_while_on == 0);
    CHECK(chip.power_switches == 2);
    CHECK(chip.power_switches_too_soon == 0);
    CHECK(chip.tracks_l5());
}

TEST_CASE(
    "ag3335: a power switch whose acknowledgement is lost is sent again only after the hold") {
    models::Ag3335 chip;
    chip.dual_band = false;
    chip.ack_lost_once = 3;
    parts::Ag3335 gnss(ports::SolutionRate::Second, chip);

    run(gnss, chip, 0, kSwitchedBringUpMs + parts::Ag3335::kPowerSwitchHoldMs);

    CHECK(gnss.configured());
    CHECK(gnss.band() == ports::GnssBand::L1L5);
    CHECK(chip.power_switches == 3);
    CHECK(chip.power_switches_too_soon == 0);
}

TEST_CASE(
    "ag3335: a switch that stays off reads L1, powers the receiver back on and is not retried") {
    models::Ag3335 chip;
    chip.dual_band = false;
    chip.unsupported_command = 104;
    parts::Ag3335 gnss(ports::SolutionRate::Second, chip);

    run(gnss, chip, 0, kSwitchedBringUpMs);

    CHECK(gnss.degraded());
    CHECK(gnss.band() == ports::GnssBand::L1);
    CHECK(chip.gnss_on);
    CHECK(chip.power_switches == 2);
    CHECK_FALSE(chip.tracks_l5());
    CHECK(chip.pps_type == 1);
}

TEST_CASE("ag3335: a dual-band build with no switch leaves the band unknown and is not degraded") {
    models::Ag3335 chip;
    chip.band_switch = false;
    parts::Ag3335 gnss(ports::SolutionRate::Second, chip);

    run(gnss, chip, 0, 2000);

    CHECK(gnss.configured());
    CHECK(gnss.band() == ports::GnssBand::Unknown);
    CHECK(chip.power_switches == 0);
}

TEST_CASE("ag3335: a single-band build reads L1 and is never asked for the switch") {
    models::Ag3335 chip;
    chip.frequency = 'S';
    parts::Ag3335 gnss(ports::SolutionRate::Second, chip);

    run(gnss, chip, 0, 2000);

    CHECK(gnss.configured());
    CHECK(gnss.band() == ports::GnssBand::L1);
    CHECK(chip.heard().find("PAIR105") == std::string::npos);
}

TEST_CASE(
    "ag3335: an unidentified receiver is never asked for the switch, and its band is unknown") {
    models::Ag3335 chip;
    chip.unsupported_command = 21;
    parts::Ag3335 gnss(ports::SolutionRate::Second, chip);

    run(gnss, chip, 0, 6000);

    CHECK(gnss.band() == ports::GnssBand::Unknown);
    CHECK(chip.heard().find("PAIR105") == std::string::npos);
}

TEST_CASE("ag3335: the PAIR reader keeps the band $PAIR021 names and the switch $PAIR105 reports") {
    parts::PairReader reader;

    CHECK(reader.frequency() == 0);
    CHECK(feed(reader,
               "$PAIR021,AG3352Q_V2.1.0.AG3352_20220530,S,N,a94c01d,2204261517,2a7,0,,,e7ae9b61,"
               "2204261515,d01b1f2,2204261517,,,-15.48,-15.48,-14.02,-15.48,0,1,##,0,0*62\r\n") ==
          parts::PairReader::Heard::Setting);
    CHECK(reader.frequency() == 'S');

    CHECK(feed(reader, "$PAIR105,1*23\r\n") == parts::PairReader::Heard::DualBand);
    CHECK(reader.dual_band_on());
    CHECK(feed(reader, "$PAIR105,0*22\r\n") == parts::PairReader::Heard::DualBand);
    CHECK_FALSE(reader.dual_band_on());
}
