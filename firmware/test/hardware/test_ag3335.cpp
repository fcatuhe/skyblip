// The AG3335 is configured one acknowledged $PAIR command at a time, and its NMEA reaches a fix.
#include <cstring>
#include <string>

#include "core/events/stamp.h"
#include "doctest/doctest.h"
#include "hardware/parts/ag3335/ag3335.h"
#include "hardware/parts/ag3335/model.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/identity.h"
#include "ports/null.h"
#include "products/common/services/ownship.h"

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

// One own-ship service over null roles, the way the board hands it a solution.
struct OwnshipRig {
    platform::host::Clock clock;
    platform::host::Identity identity{platform::host::Identity::kSerial};
    ports::NullRoles null;
    ports::Roles roles{
        clock,          null.rf,          null.link, null.display,         null.key_value,
        null.log_flash, null.annunciator, null.dfu,  null.die_temperature, null.indicator,
        null.gnss,      identity};
    bus::Bus bus{};
    bus::State state{};
    diagnostics::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    uint8_t aircraft_type{0};
    common::OwnshipService ownship{context, aircraft_type};
};

// The model's date, 1 January 2025, at midnight UTC.
constexpr uint32_t kModelDateUtcS = 1735689600;

struct Timed {
    models::Ag3335 chip;
    parts::Ag3335 gnss{chip};
    OwnshipRig rig;
    uint32_t arrival_ms{0};
    uint32_t utc_s{0};

    gnss::GnssSolution next_fix() {
        for (uint32_t now_ms = 0; now_ms <= 10000; now_ms += 10) {
            chip.tick(now_ms);
            gnss.service(now_ms);
            if (!gnss.poll() || !gnss.solution().fix_valid) continue;
            arrival_ms = now_ms;
            utc_s = kModelDateUtcS + chip.utc_seconds_of_day;
            return gnss.solution();
        }
        FAIL("the model never produced a fix");
        return {};
    }

    uint32_t on_time_edge_ms() const { return arrival_ms - parts::Ag3335::pps_latency_ms(); }
};

constexpr const char* kSequence =
    "PAIR021,PAIR382,PAIR066,PAIR100,PAIR062,PAIR062,PAIR062,PAIR062,PAIR062,PAIR062,PAIR062,"
    "PAIR050,PAIR080,PAIR400,PAIR410,PAIR752,";

}  // namespace

TEST_CASE("ag3335: bring-up identifies the receiver, then sends every setting once it is acked") {
    models::Ag3335 chip;
    parts::Ag3335 gnss(chip);

    run(gnss, chip, 0, 2000);

    CHECK(chip.heard() == kSequence);
    CHECK(chip.commands_rejected == 0);
    CHECK(gnss.configured());
    CHECK(gnss.identified());
    CHECK(std::string(gnss.firmware_version()) == "AG3335M_V3.3.0");
    CHECK(gnss.dual_band());
}

TEST_CASE("ag3335: the configuration tracks L1 and L5 and takes SBAS corrections") {
    models::Ag3335 chip;
    parts::Ag3335 gnss(chip);

    run(gnss, chip, 0, 2000);

    CHECK(chip.tracks_l5());
    CHECK(chip.constellations ==
          (models::Ag3335::kGps | models::Ag3335::kGlonass | models::Ag3335::kGalileo |
           models::Ag3335::kBeidou | models::Ag3335::kQzss));
    CHECK(chip.sbas_enabled);
    CHECK(chip.dgps_mode == 2);
    // SBAS is off in fitness mode (SIM65M §2.3.80), so normal mode is what lets it run.
    CHECK(chip.navigation_mode == 0);
    CHECK(chip.sleep_locked);
}

TEST_CASE("ag3335: the receiver sends GGA, GSA and RMC once a second, NMEA 4.10, and a PPS") {
    models::Ag3335 chip;
    parts::Ag3335 gnss(chip);

    run(gnss, chip, 0, 2000);

    CHECK(chip.sentence_on(models::Ag3335::kGga));
    CHECK(chip.sentence_on(models::Ag3335::kGsa));
    CHECK(chip.sentence_on(models::Ag3335::kRmc));
    CHECK_FALSE(chip.sentence_on(models::Ag3335::kGll));
    CHECK_FALSE(chip.sentence_on(models::Ag3335::kGsv));
    CHECK_FALSE(chip.sentence_on(models::Ag3335::kVtg));
    CHECK_FALSE(chip.sentence_on(models::Ag3335::kZda));
    CHECK(chip.solution_period_ms == parts::Ag3335::kSolutionPeriodMs);
    CHECK(chip.nmea_mode == 1);
    CHECK(chip.pps_type == 1);
    CHECK(chip.pps_width_ms == 100);
}

TEST_CASE("ag3335: every sentence the driver sends checksums and names its PAIR command") {
    const auto valid = [](const char* sentence) {
        const int bytes = static_cast<int>(std::strlen(sentence));
        return bytes > 2 && sentence[bytes - 2] == '\r' && sentence[bytes - 1] == '\n' &&
               gnss::nmea_checksum_ok(sentence, bytes - 2) && parts::pair_command_id(sentence) != 0;
    };
    CHECK(valid(parts::Ag3335::kIdentifyCommand));
    for (const char* command : parts::Ag3335::kCommands) CHECK(valid(command));
    for (const char* command : parts::Ag3335::kRestartCommands) CHECK(valid(command));
}

TEST_CASE("ag3335: a lost acknowledgement is asked again, and the sequence still completes") {
    models::Ag3335 chip;
    chip.acks_to_drop = 2;
    parts::Ag3335 gnss(chip);

    run(gnss, chip, 0, 5000);

    CHECK(gnss.configured());
    CHECK(gnss.refused_commands() == 0);
    // Two dropped answers cost two repeats: the identification and the sleep lock.
    CHECK(chip.commands_seen == parts::Ag3335::kCommandCount + 1 + 2);
}

TEST_CASE("ag3335: what is said while the receiver restarts after $PAIR066 lands on a retry") {
    models::Ag3335 chip;
    chip.restart_ms = 1500;
    parts::Ag3335 gnss(chip);

    run(gnss, chip, 0, 8000);

    CHECK(gnss.configured());
    CHECK(chip.nmea_mode == 1);
    CHECK(chip.pps_type == 1);
    CHECK(chip.tracks_l5());
}

TEST_CASE(
    "ag3335: a refused command is skipped, the rest still land, and the receiver reads degraded") {
    models::Ag3335 chip;
    chip.unsupported_command = 382;
    parts::Ag3335 gnss(chip);

    run(gnss, chip, 0, 5000);

    CHECK(gnss.degraded());
    CHECK(gnss.refused_commands() == 1);
    CHECK_FALSE(chip.sleep_locked);
    CHECK(chip.tracks_l5());
    CHECK(chip.sbas_enabled);
    CHECK(chip.pps_type == 1);
    CHECK(gnss.health().config_stage == ports::GnssConfigStage::Degraded);
}

TEST_CASE("ag3335: a receiver that answers nothing ends degraded and unidentified") {
    models::Ag3335 chip;
    chip.answers = false;
    parts::Ag3335 gnss(chip);

    const uint32_t every_attempt_ms = (parts::Ag3335::kCommandCount + 1) *
                                      parts::Ag3335::kCommandAttempts * parts::Ag3335::kAckWindowMs;
    run(gnss, chip, 0, every_attempt_ms + 1000);

    CHECK(gnss.degraded());
    CHECK(gnss.refused_commands() == parts::Ag3335::kCommandCount);
    CHECK_FALSE(gnss.identified());
}

TEST_CASE(
    "ag3335: a fix reaches the solution, with the height above the ellipsoid from GGA field 11") {
    models::Ag3335 chip;
    chip.altitude_m = 1500;
    chip.geoid_separation_m = 49;
    parts::Ag3335 gnss(chip);

    run(gnss, chip, 0, 4000);

    const gnss::GnssSolution& fix = gnss.solution();
    CHECK(fix.fix_valid);
    CHECK(fix.utc_valid);
    CHECK(fix.geoid_measured);
    CHECK(fix.geoid_separation_mm == 49000);
    CHECK(fix.altitude_msl_mm == 1451000);
    CHECK(fix.altitude_hae_mm == 1500000);
    CHECK(fix.satellites == chip.satellites);
    CHECK(fix.vdop_e2 == chip.vdop_e2);
    CHECK(fix.fix_mode == gnss::kFixMode3D);
    CHECK(gnss.sky().in_use_of(gnss::System::Gps) > 0);
    CHECK(gnss.last_reject() == gnss::FixReject::None);
}

TEST_CASE("ag3335: a GGA without field 11 falls back on the default separation and says so") {
    models::Ag3335 chip;
    chip.emit_geoid_separation = false;
    parts::Ag3335 gnss(chip);

    run(gnss, chip, 0, 4000);

    CHECK(gnss.solution().fix_valid);
    CHECK_FALSE(gnss.solution().geoid_measured);
    CHECK(gnss.solution().geoid_separation_mm == gnss::kDefaultGeoidSeparationMm);
}

TEST_CASE("ag3335: a receiver with no fix publishes none, and still counts its sentences") {
    models::Ag3335 chip;
    chip.fix_valid = false;
    parts::Ag3335 gnss(chip);

    run(gnss, chip, 0, 4000);

    CHECK_FALSE(gnss.solution().fix_valid);
    CHECK(gnss.sentences() > 0);
    CHECK(gnss.last_reject() != gnss::FixReject::None);
}

TEST_CASE("ag3335: with PPS locked the edge dates the fix, and an event after it carries its UTC") {
    Timed timed;
    const gnss::GnssSolution fix = timed.next_fix();
    // 82 ms off the latency estimate, so only the latched edge can date the fix here.
    const uint32_t edge_ms = timed.arrival_ms - 50;
    const uint64_t edge_us = static_cast<uint64_t>(edge_ms) * 1000;
    timed.rig.state.clock.pps_locked = true;
    timed.rig.state.clock.pps_edge_us = edge_us;

    timed.rig.bus.gnss.push(fix);
    timed.rig.ownship.tick(timed.arrival_ms);

    CHECK(timed.rig.state.own.fix_at_ms == edge_ms);
    CHECK(timed.rig.state.own.utc_s == timed.utc_s);
    CHECK(timed.rig.state.clock.utc_s == timed.utc_s);
    CHECK(timed.rig.state.clock.utc_edge_us == edge_us);
    const events::Stamp stamp = events::stamp_of(
        edge_us + 250000, timed.rig.state.clock.pps_edge_us, true, timed.rig.state.clock.utc_s);
    CHECK(stamp.phase_valid);
    CHECK(stamp.at_s == timed.utc_s);
    CHECK(stamp.into_ms == 250);
}

// The arrival less the part's latency lands on the edge the burst would have followed on time.
TEST_CASE(
    "ag3335: without PPS the fix is dated by its arrival less the burst's start and line time") {
    Timed timed;
    const gnss::GnssSolution fix = timed.next_fix();

    timed.rig.bus.gnss.push(fix);
    timed.rig.ownship.tick(timed.arrival_ms);

    CHECK(fix.pps_latency_ms == parts::Ag3335::kBurstStartMs + parts::Ag3335::kBurstMs);
    CHECK(timed.rig.state.own.fix_at_ms == timed.on_time_edge_ms());
    CHECK(timed.rig.state.own.utc_s == timed.utc_s);
    CHECK(timed.rig.state.clock.utc_s == 0);
    const events::Stamp stamp = events::stamp_of(static_cast<uint64_t>(timed.arrival_ms) * 1000, 0,
                                                 false, timed.rig.state.own.utc_s);
    CHECK_FALSE(stamp.phase_valid);
    CHECK(stamp.at_s == timed.utc_s);
}

TEST_CASE("ag3335: a command still processing is waited for, not sent again") {
    models::Ag3335 chip;
    chip.processing_command = 66;
    chip.processing_ms = 1500;
    parts::Ag3335 gnss(chip);

    run(gnss, chip, 0, 5000);

    CHECK(gnss.configured());
    CHECK(chip.commands_seen == parts::Ag3335::kCommandCount + 1);
    CHECK(chip.tracks_l5());
}

TEST_CASE("ag3335: a command that never stops processing is given up, and bring-up still ends") {
    models::Ag3335 chip;
    chip.processing_command = 66;
    chip.processing_ms = UINT32_MAX;
    parts::Ag3335 gnss(chip);

    const uint32_t ceiling_ms =
        parts::Ag3335::kCommandAttempts * parts::Ag3335::kProcessingWindowMs;
    run(gnss, chip, 0, ceiling_ms + 2000);

    CHECK(gnss.degraded());
    CHECK(gnss.refused_commands() == 1);
    CHECK(chip.pps_type == 1);
}

TEST_CASE("ag3335: a factory reset restores the receiver's defaults, and the sequence runs again") {
    models::Ag3335 chip;
    parts::Ag3335 gnss(chip);
    run(gnss, chip, 0, 2000);
    REQUIRE(gnss.configured());

    gnss.request_restart(ports::Restart::Factory);
    run(gnss, chip, 2010, 2020);
    CHECK(chip.factory_resets == 1);
    CHECK(chip.last_restart_kind == 3);

    run(gnss, chip, 2030, 5000);
    CHECK(gnss.configured());
    CHECK(chip.tracks_l5());
    CHECK(chip.pps_type == 1);
    CHECK_FALSE(chip.sentence_on(models::Ag3335::kGsv));
}

TEST_CASE("ag3335: a cold start keeps the configuration") {
    models::Ag3335 chip;
    parts::Ag3335 gnss(chip);
    run(gnss, chip, 0, 2000);
    REQUIRE(gnss.configured());

    gnss.request_restart(ports::Restart::Cold);
    run(gnss, chip, 2010, 3000);

    CHECK(chip.last_restart_kind == 2);
    CHECK(chip.factory_resets == 0);
    CHECK(gnss.configured());
    CHECK(chip.pps_type == 1);
}

// SIM65M §2.3.12 prints this 153-byte reply: the reader must hold it whole to check it.
TEST_CASE("ag3335: the PAIR reader takes the documented $PAIR021 reply and its acknowledgement") {
    parts::PairReader reader;

    CHECK(feed(reader, "$PAIR001,021,0*38\r\n") == parts::PairReader::Heard::Ack);
    CHECK(reader.ack_command() == 21);
    CHECK(reader.ack_result() == parts::PairResult::Success);

    CHECK(feed(reader,
               "$PAIR021,AG3352Q_V2.1.0.AG3352_20220530,S,N,a94c01d,2204261517,2a7,0,,,e7ae9b61,"
               "2204261515,d01b1f2,2204261517,,,-15.48,-15.48,-14.02,-15.48,0,1,##,0,0*62\r\n") ==
          parts::PairReader::Heard::Setting);
    CHECK(std::string(reader.firmware_version()) == "AG3352Q_V2.1.0.AG3352_20220530");
    CHECK_FALSE(reader.dual_band());
}

TEST_CASE("ag3335: the PAIR reader refuses a reply whose checksum does not match") {
    parts::PairReader reader;

    CHECK(feed(reader, "$PAIR001,021,0*39\r\n") == parts::PairReader::Heard::Nothing);
    CHECK(feed(reader, "$PAIR021,AG3335M_V3.3.0,D,N*00\r\n") == parts::PairReader::Heard::Nothing);
    CHECK_FALSE(reader.identified());
}
