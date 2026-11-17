// A whole Go grades the traffic for a whole Bravo, which plays the Go's calls at the Go's alarm.
#include <cstdint>
#include <string>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"
#include "test/support/log_transfer.h"
#include "test/support/paired_link.h"
#include "test/support/paired_products.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

constexpr uint32_t kAircraft = 0x3C6586;
constexpr uint32_t kSecondAircraft = 0x4CA2D1;
constexpr uint32_t kGoAdslAircraft = 0x3D0A11;
constexpr int32_t kSpeedMmS = 25000;
// INFO: fc 12nov26 PairedProducts::run_for(0) is one pass of the Go, and one of Bravo with it
constexpr uint32_t kGoPassMs = 50;
// INFO: fc 17nov26 the Go's claim leaves on its pass, and Bravo plays it two passes later at most
constexpr uint32_t kLatencyCeilingMs = 3 * kGoPassMs;
// INFO: fc 12nov26 both barometers read sea level, so an aircraft at 100 ft is level with both
constexpr int32_t kNearSeaLevelFt = 100;
// INFO: fc 17nov26 a latitude step of 1e-7 degree is 1.1 cm, so 90 of them make a metre
constexpr int32_t kLatitude1e7PerMetre = 90;

const ports::Capabilities kPlainGo = ports::without(
    platform::host::Platform::kFullyFitted, ports::Capability::Buzzer | ports::Capability::Haptic);

struct Flight {
    PairedProducts pair;
    bool go_fix{true};

    explicit Flight(ports::Capabilities go_fitted = platform::host::Platform::kFullyFitted,
                    bravo::Alarm bravo_alarm = bravo::Alarm::Loud)
        : pair(go_fitted) {
        pair.bravo.platform.barometer().set_altitude_mm(0);
        pair.bravo.product.settings().alarm = bravo_alarm;
        pair.pair_and_store();
        REQUIRE(pair.go_pinned());
        REQUIRE(pair.peer().paired());
    }

    // The Go's receiver where Bravo's is, both flying north at the same speed.
    void push_go_fix() {
        gnss::GnssSolution solution{};
        solution.fix_valid = true;
        solution.utc_valid = true;
        solution.utc_s = Rig::kBaseUtcS + pair.go.utc_offset_s;
        solution.latitude_1e7 = BravoRig::kLatitude1e7;
        solution.longitude_1e7 = BravoRig::kLongitude1e7;
        solution.altitude_msl_mm = 0;
        solution.altitude_hae_mm = gnss::kDefaultGeoidSeparationMm;
        solution.geoid_measured = true;
        solution.speed_mm_s = kSpeedMmS;
        solution.satellites = 10;
        solution.hdop_e2 = 100;
        solution.vdop_e2 = 150;
        solution.sentences = ++pair.go.fix_updates;
        pair.go.product.bus().gnss.push(solution);
    }

    void push_fixes() {
        if (go_fix) push_go_fix();
        pair.bravo.push_fix(kSpeedMmS);
        pair.go.utc_offset_s++;
        pair.bravo.utc_offset_s++;
    }

    void second() {
        push_fixes();
        pair.run_for(950);
    }

    void seconds(uint32_t count) {
        for (uint32_t i = 0; i < count; i++) second();
    }

    // An ADS-L aircraft only the Go hears, level with it and north of it.
    void go_hears_adsl(int32_t north_m) {
        const model::Ownship& own = pair.go.state().own;
        model::AircraftObservation target{};
        target.address = kGoAdslAircraft;
        target.address_mapping_table = 6;
        target.source = model::Source::AdslDirect;
        target.position_valid = true;
        target.latitude_1e7 = own.latitude_1e7 + north_m * kLatitude1e7PerMetre;
        target.longitude_1e7 = own.longitude_1e7;
        target.altitude_valid = true;
        target.altitude_m = own.altitude_hae_mm / 1000;
        target.at_ms = pair.now_ms;
        target.received.at_s = own.utc_s;
        REQUIRE(pair.go.state().traffic.update(target,
                                               pair.go.state().traffic_now_s(pair.now_ms)) >= 0);
    }

    // Steps a pass at a time until both units have sounded since the counts given.
    void onsets(uint32_t go_before, uint32_t bravo_before, uint32_t& go_ms, uint32_t& bravo_ms) {
        const uint32_t from_ms = pair.now_ms;
        while ((go_ms == 0 || bravo_ms == 0) && pair.now_ms - from_ms < 5000) {
            pair.run_for(0);
            if (go_ms == 0 && go_tones() > go_before) go_ms = pair.now_ms;
            if (bravo_ms == 0 && bravo_tones() > bravo_before) bravo_ms = pair.now_ms;
        }
    }

    // Bravo hears an aircraft ahead, the Go folds it, and both units call it.
    void both_call(uint32_t address = kAircraft, double north_m = 800) {
        const uint32_t go_before = go_tones();
        const uint32_t bravo_before = bravo_tones();
        pair.bravo.hear_aircraft(address, north_m, kNearSeaLevelFt);
        push_fixes();
        uint32_t go_ms = 0, bravo_ms = 0;
        onsets(go_before, bravo_before, go_ms, bravo_ms);
        REQUIRE(go_ms != 0);
        REQUIRE(bravo_ms != 0);
        CHECK(bravo_ms - go_ms <= kLatencyCeilingMs);
    }

    void out_of_reach() {
        pair.air.set_in_range(false);
        pair.air.drop();
        pair.run_for(100);
        REQUIRE_FALSE(pair.bravo.state().followed_call.has_value());
    }

    uint32_t go_tones() { return pair.go.platform.annunciator().tone_commands(); }
    uint32_t bravo_tones() { return pair.bravo.platform.annunciator().tone_commands(); }
    traffic::Level go_standing() { return pair.go.state().alarm_undismissed; }
    traffic::Level bravo_standing() { return pair.bravo.state().alarm_undismissed; }
};

}  // namespace

TEST_CASE("bravo alarm: a Go+ and its Bravo both sound the Go's call, within the link's latency") {
    Flight flight;
    flight.seconds(20);
    REQUIRE(flight.pair.go.state().bravo == comms::BravoState::Live);
    REQUIRE(flight.pair.bravo.state().followed_call.has_value());
    flight.both_call();
    CHECK(flight.go_standing() == traffic::Level::Advisory);
    CHECK(flight.bravo_standing() == traffic::Level::Advisory);
    CHECK(flight.pair.bravo.platform.annunciator().motor_starts() > 0);
}

TEST_CASE("bravo alarm: a second aircraft under a standing alarm is called again on both units") {
    Flight flight;
    flight.seconds(20);
    flight.both_call();
    flight.seconds(3);
    flight.both_call(kSecondAircraft, 700);
}

TEST_CASE("bravo alarm: a press on Bravo dismisses the alarm on both units") {
    Flight flight;
    flight.seconds(20);
    flight.both_call();
    flight.pair.press_bravo();
    CHECK(flight.bravo_standing() == traffic::Level::None);
    flight.pair.run_for(200);
    CHECK(flight.go_standing() == traffic::Level::None);

    const uint32_t go_tones = flight.go_tones();
    const uint32_t bravo_tones = flight.bravo_tones();
    flight.seconds(3);
    CHECK(flight.go_tones() == go_tones);
    CHECK(flight.bravo_tones() == bravo_tones);
    CHECK(flight.go_standing() == traffic::Level::None);
    CHECK(flight.bravo_standing() == traffic::Level::None);
}

TEST_CASE("bravo alarm: a long touch on the Go dismisses the alarm on both units") {
    Flight flight;
    flight.seconds(20);
    flight.both_call();
    flight.pair.go.long_touch(flight.pair.now_ms);
    flight.pair.run_for(200);
    CHECK(flight.go_standing() == traffic::Level::None);
    CHECK(flight.bravo_standing() == traffic::Level::None);
    CHECK_FALSE(flight.pair.bravo.platform.annunciator().sounding());
}

TEST_CASE("bravo alarm: with the link down, a press on either unit silences only that unit") {
    SUBCASE("on Bravo") {
        Flight flight;
        flight.seconds(20);
        flight.both_call();
        flight.out_of_reach();
        flight.pair.press_bravo();
        CHECK(flight.bravo_standing() == traffic::Level::None);
        CHECK(flight.go_standing() == traffic::Level::Advisory);
    }
    SUBCASE("on the Go") {
        Flight flight;
        flight.seconds(20);
        flight.both_call();
        flight.out_of_reach();
        flight.pair.go.long_touch(flight.pair.now_ms);
        CHECK(flight.go_standing() == traffic::Level::None);
        CHECK(flight.bravo_standing() == traffic::Level::Advisory);
    }
}

TEST_CASE("bravo alarm: Bravo keeps the Go's alarm through a lost link and in flash, and uses it") {
    Flight flight;
    flight.pair.go.settings().alarm = go::Alarm::Soft;
    flight.seconds(5);
    REQUIRE(flight.pair.bravo.product.alarm().alarm_in_force() == bravo::Alarm::Soft);
    const PairedProducts::Stored stored = flight.pair.stored();
    bravo::Settings kept{};
    REQUIRE(bravo::from_blob(reinterpret_cast<const uint8_t*>(stored.bravo.data()),
                             stored.bravo.size(), kept) == Status::Ok);
    CHECK(kept.go_alarm == bravo::Alarm::Soft);
    CHECK(kept.alarm == bravo::Alarm::Loud);

    flight.seconds(15);
    flight.out_of_reach();
    const uint32_t tones = flight.bravo_tones();
    flight.pair.bravo.hear_aircraft(kAircraft, 800, kNearSeaLevelFt);
    flight.seconds(2);
    CHECK(flight.bravo_tones() > tones);
    CHECK(flight.pair.bravo.platform.annunciator().loudness() == ports::Loudness::Soft);
}

TEST_CASE("bravo alarm: unpairing gives Bravo back the alarm it had before the pairing") {
    Flight flight(platform::host::Platform::kFullyFitted, bravo::Alarm::Visual);
    flight.seconds(3);
    REQUIRE(flight.pair.bravo.product.alarm().alarm_in_force() == bravo::Alarm::Loud);
    flight.pair.service().unpair();
    flight.pair.run_for(3000);
    CHECK_FALSE(flight.pair.peer().paired());
    CHECK(flight.pair.bravo.product.alarm().alarm_in_force() == bravo::Alarm::Visual);
    CHECK(flight.pair.bravo.product.settings().alarm == bravo::Alarm::Visual);
}

// A plain Go has no buzzer and no motor, so BRAVO LOUD is a Go's ADS-L traffic heard on Bravo.
TEST_CASE(
    "tango alarm: a plain Go on BRAVO LOUD sounds its ADS-L traffic on Bravo, and none itself") {
    Flight flight(kPlainGo);
    flight.pair.go.settings().alarm = go::Alarm::Loud;
    flight.seconds(20);
    REQUIRE(flight.pair.go.product.alarm().alarm_in_force() == go::Alarm::Visual);
    REQUIRE(flight.pair.bravo.product.alarm().alarm_in_force() == bravo::Alarm::Loud);

    const uint32_t tones = flight.bravo_tones();
    for (int i = 0; i < 2; i++) {
        flight.go_hears_adsl(800);
        flight.second();
    }
    CHECK(flight.go_standing() == traffic::Level::Advisory);
    CHECK(flight.bravo_standing() == traffic::Level::Advisory);
    CHECK(flight.bravo_tones() > tones);
    CHECK(flight.pair.bravo.platform.annunciator().loudness() == ports::Loudness::Loud);
    CHECK(flight.go_tones() == 0);
    CHECK_FALSE(flight.pair.go.product.alarm().sounding());
}

TEST_CASE("bravo alarm: Bravo plays its first-fix tune only when the pair's alarm sounds") {
    SUBCASE("a plain Go on VISUAL") {
        Flight flight(kPlainGo);
        flight.pair.go.settings().alarm = go::Alarm::Visual;
        flight.pair.run_for(2000);
        flight.seconds(5);
        CHECK(flight.pair.bravo.state().own.fix_valid);
        CHECK(flight.bravo_tones() == 0);
        CHECK(flight.go_tones() == 0);
    }
    SUBCASE("a plain Go on BRAVO SOFT") {
        Flight flight(kPlainGo);
        flight.pair.go.settings().alarm = go::Alarm::Soft;
        flight.pair.run_for(2000);
        flight.seconds(5);
        CHECK(flight.pair.bravo.state().own.fix_valid);
        CHECK(flight.bravo_tones() > 0);
        CHECK(flight.go_tones() == 0);
    }
}

// A plain Go has no buzzer for the lost-Bravo tone, so Bravo's own note is all its pilot hears.
TEST_CASE("bravo alarm: a plain Go on BRAVO LOUD out of reach in flight leaves one note on Bravo") {
    Flight flight(kPlainGo);
    flight.pair.go.settings().alarm = go::Alarm::Loud;
    flight.seconds(20);
    REQUIRE(flight.pair.bravo.state().flight.confirmed_state == flight::FlightState::Airborne);
    const uint32_t tones = flight.bravo_tones();
    flight.out_of_reach();
    flight.seconds(3);
    CHECK(flight.bravo_tones() == tones + 1);
    CHECK(flight.go_tones() == 0);
}
