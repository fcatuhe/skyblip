// What a formation the device found by itself does to the annunciator and the radar's count, and
// what takes it back.
#include "core/traffic/formation.h"
#include "core/traffic/lease.h"
#include "core/units/units.h"
#include "core/util/intmath.h"
#include "doctest/doctest.h"
#include "test/support/glass_text.h"
#include "test/support/screen_rig.h"

using namespace skyblip;

namespace {

model::AircraftObservation target_at(const model::Ownship& own, int north_m, int east_m, int up_m,
                                     int speed_mps, int track_deg, uint32_t at_ms) {
    model::AircraftObservation observation{};
    observation.address = 0x515151;
    observation.address_mapping_table = 6;
    observation.position_valid = true;
    observation.speed_valid = true;
    observation.speed_q = static_cast<uint16_t>(speed_mps * 4);
    observation.track_c9 = to_cordic9(CentiDegrees(track_deg * 100)).value;
    observation.altitude_m = to_metres(Millimetres(own.altitude_hae_mm)).value + up_m;
    observation.latitude_1e7 =
        own.latitude_1e7 + static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    const int16_t latitude_angle =
        static_cast<int16_t>((static_cast<int64_t>(own.latitude_1e7) * 65536) / 3600000000LL);
    const int64_t east_scaled = static_cast<int64_t>(east_m) * kTrigOne / cosine(latitude_angle);
    observation.longitude_1e7 =
        own.longitude_1e7 + static_cast<int32_t>(east_scaled * 1000000 / 11132);
    observation.received.at_s = at_ms / 1000;
    observation.at_ms = at_ms;
    observation.source = model::Source::AdslDirect;
    return observation;
}

struct Flight {
    ScreenRig rig;

    Flight() {
        rig.state.own.fix_valid = true;
        rig.state.own.latitude_1e7 = 481000000;
        rig.state.own.longitude_1e7 = 81000000;
        rig.state.own.altitude_hae_mm = 1000000;
        rig.state.own.speed_mm_s = 40000;
        rig.state.own.track_cdeg = 9000;
    }

    void hear(int north_m, int east_m, int up_m, int speed_mps, int track_deg, uint32_t now_ms,
              flight::FlightState says = flight::FlightState::Unknown,
              uint32_t address = 0x515151) {
        rig.state.own.fix_at_ms = now_ms;
        model::AircraftObservation observation =
            target_at(rig.state.own, north_m, east_m, up_m, speed_mps, track_deg, now_ms);
        observation.address = address;
        observation.flight_state = static_cast<uint8_t>(says);
        rig.state.traffic.update(observation, now_ms / 1000);
        rig.alarm_service.tick(now_ms);
    }

    void own_state(flight::FlightState state, int speed_mps) {
        rig.state.own.flight_state = static_cast<uint8_t>(state);
        rig.state.own.speed_mm_s = speed_mps * 1000;
    }

    bool quiet() {
        return rig.state.alarm_level == traffic::Level::None &&
               rig.alarm_service.announcing_level() == traffic::Level::None;
    }

    const traffic::Target* target() {
        const int at = rig.state.traffic.find(6, 0x515151);
        return at < 0 ? nullptr : rig.state.traffic.at(at);
    }

    uint32_t hold_station(int north_m, int east_m, uint32_t from_ms) {
        uint32_t now_ms = from_ms;
        for (; now_ms <= from_ms + formation::kTogetherHoldMs + 2000; now_ms += 1000)
            hear(north_m, east_m, 10, 40, 90, now_ms);
        return now_ms;
    }

    uint32_t hold_station(uint32_t from_ms) { return hold_station(-60, -120, from_ms); }

    void listen() {
        rig.roles.capabilities = ports::Capability::Display | ports::Capability::Rf;
        rig.state.clock.pps_locked = true;
        rig.state.clock.since_edge_ms = go::ScreenService::kLockedRedrawWindowStartMs;
    }

    bool radar_counts(const char* figure) {
        return reads_in(rig.screen.framebuffer(), figure, 170, 170, 200, 200, 3);
    }

    bool nearby_counts(const char* figure) {
        return reads_in(rig.screen.framebuffer(), figure, 150, 0, 200, go::kNearbyHeadY, 2);
    }
};

}  // namespace

TEST_CASE("formation: a neighbour holding station joins by itself, and takes its level with it") {
    Flight flight;
    const uint32_t after = flight.hold_station(1000);

    CHECK(flight.target()->in_formation);
    CHECK(flight.rig.alarm_service.formation_members() == 1);
    CHECK(flight.rig.state.alarm_level == traffic::Level::None);
    CHECK(flight.target()->alarm_level == traffic::Level::None);

    flight.hear(-60, -120, 10, 40, 90, after);
    CHECK(flight.target()->in_formation);
    CHECK(flight.rig.state.alarm_level == traffic::Level::None);
}

// A pair this close alarms on every fix for as long as it flies, which is the device switched off.
TEST_CASE("formation: a tight pair that settles down stops the alarm its joining raised") {
    Flight flight;
    flight.hear(-20, -30, 10, 40, 90, 1000);
    REQUIRE(flight.rig.state.alarm_level == traffic::Level::Advisory);

    const uint32_t after = flight.hold_station(-20, -30, 2000);
    CHECK(flight.target()->in_formation);
    CHECK(flight.rig.state.alarm_level == traffic::Level::None);
    CHECK(flight.rig.alarm_service.announcing_level() == traffic::Level::None);

    flight.hear(-20, -30, 10, 40, 90, after);
    CHECK(flight.rig.state.alarm_level == traffic::Level::None);
}

TEST_CASE("formation: a member closing on us alarms on the second fix, and is no longer a member") {
    Flight flight;
    uint32_t now_ms = flight.hold_station(1000);
    REQUIRE(flight.target()->in_formation);

    // The same aircraft, now 400 m off the nose coming the other way.
    flight.hear(0, 400, 0, 40, 270, now_ms);
    CHECK(flight.quiet());
    now_ms += 1000;
    flight.hear(0, 320, 0, 40, 270, now_ms);
    CHECK(flight.rig.state.alarm_level == traffic::Level::Advisory);
    CHECK_FALSE(flight.target()->in_formation);
    CHECK(flight.rig.alarm_service.formation_members() == 0);
}

// The closing figure was rounded to whole metres a second, so 2.5 m/s of noise took it back.
TEST_CASE("formation: a member under three metres a second of closure stays a member") {
    Flight flight;
    uint32_t now_ms = flight.hold_station(1000);
    REQUIRE(flight.target()->in_formation);

    // 3.25 m/s faster from 60 m right and 120 m behind: 2.9 m/s on the line of sight.
    for (const uint32_t until = now_ms + 10000; now_ms < until; now_ms += 1000) {
        flight.rig.state.own.fix_at_ms = now_ms;
        model::AircraftObservation observation =
            target_at(flight.rig.state.own, -60, -120, 10, 40, 90, now_ms);
        observation.speed_q = 173;
        flight.rig.state.traffic.update(observation, now_ms / 1000);
        flight.rig.alarm_service.tick(now_ms);
        CHECK(flight.target()->in_formation);
        CHECK(flight.quiet());
    }
}

TEST_CASE("formation: a pair splitting is not alarmed while it separates") {
    Flight flight;
    uint32_t now_ms = flight.hold_station(1000);
    REQUIRE(flight.target()->in_formation);

    for (int east = -300; east >= -900; east -= 200, now_ms += 1000) {
        flight.hear(-60, east, 10, 40, 90, now_ms);
        CHECK(flight.rig.state.alarm_level == traffic::Level::None);
        CHECK(flight.rig.alarm_service.announcing_level() == traffic::Level::None);
    }
    CHECK(flight.rig.alarm_service.formation_members() == 0);
}

TEST_CASE("formation: a split that turns back into a closure is alarmed again") {
    Flight flight;
    uint32_t now_ms = flight.hold_station(1000);
    flight.hear(-60, -300, 10, 40, 90, now_ms);
    now_ms += 1000;
    flight.hear(-60, -500, 10, 40, 90, now_ms);
    now_ms += 1000;
    REQUIRE(flight.rig.state.alarm_level == traffic::Level::None);

    // The one that left is overtaking us from behind at 40 m/s.
    flight.hear(-60, -500, 10, 80, 90, now_ms);
    now_ms += 1000;
    flight.hear(-60, -460, 10, 80, 90, now_ms);
    CHECK(flight.rig.state.alarm_level > traffic::Level::None);
    CHECK_FALSE(flight.target()->in_formation);
}

TEST_CASE("formation: a target nobody has heard from is forgotten, membership and all") {
    Flight flight;
    const uint32_t after = flight.hold_station(1000);
    REQUIRE(flight.rig.alarm_service.formation_members() == 1);

    const uint32_t gone = after + traffic::kAirborneTargetForgetS * 1000 + 1;
    flight.rig.state.traffic.age_out(gone / 1000);
    flight.rig.alarm_service.tick(gone);
    CHECK(flight.rig.alarm_service.formation_members() == 0);
}

// The car trip: every stop was a landing, and every pull-away re-formed the pair with a beep.
TEST_CASE("formation: a pair lifting off together is never announced, and joins") {
    Flight flight;
    flight.own_state(flight::FlightState::Ground, 0);
    uint32_t now_ms = 1000;
    for (; now_ms <= 30000; now_ms += 10000) {
        flight.hear(-20, -30, 0, 0, 90, now_ms, flight::FlightState::Ground);
        CHECK(flight.quiet());
    }

    flight.own_state(flight::FlightState::Airborne, 40);
    for (const uint32_t until = now_ms + formation::kTogetherHoldMs + 2000; now_ms <= until;
         now_ms += 1000) {
        flight.hear(-20, -30, 0, 40, 90, now_ms, flight::FlightState::Airborne);
        CHECK(flight.quiet());
    }
    CHECK(flight.target()->in_formation);
}

// The glider leaves the ground first, so the tug is a ground target until it lifts off itself.
TEST_CASE("formation: a tug lifting off ahead of its glider is never announced, and joins") {
    Flight flight;
    flight.own_state(flight::FlightState::Airborne, 30);
    uint32_t now_ms = 1000;
    for (; now_ms <= 5000; now_ms += 1000) {
        flight.hear(0, 60, 0, 30, 90, now_ms, flight::FlightState::Ground);
        CHECK(flight.quiet());
    }
    for (const uint32_t until = now_ms + formation::kTogetherHoldMs + 2000; now_ms <= until;
         now_ms += 1000) {
        flight.hear(0, 60, 0, 30, 90, now_ms, flight::FlightState::Airborne);
        CHECK(flight.quiet());
    }
    CHECK(flight.target()->in_formation);
}

TEST_CASE("formation: a second aircraft catching up after the takeoff is announced") {
    Flight flight;
    flight.own_state(flight::FlightState::Ground, 0);
    flight.rig.alarm_service.tick(1000);
    flight.own_state(flight::FlightState::Airborne, 40);

    uint32_t now_ms = 2000;
    for (int east = -400; now_ms <= 6000; now_ms += 1000, east += 10)
        flight.hear(-20, east, 0, 50, 90, now_ms, flight::FlightState::Airborne);
    CHECK(flight.rig.state.alarm_level == traffic::Level::Advisory);
    CHECK_FALSE(flight.target()->in_formation);
}

// The square around own ship is the member's depiction, and a count of it too reads as company.
TEST_CASE("formation: a member is not traffic on the radar's count, and is still listed nearby") {
    Flight flight;
    flight.listen();
    uint32_t now_ms = flight.hold_station(1000);
    REQUIRE(flight.target()->in_formation);

    flight.rig.run_seconds(now_ms, 2);
    REQUIRE(flight.rig.screen.page() == go::Page::Radar);
    CHECK(flight.radar_counts("0"));

    flight.hear(6000, 0, 0, 40, 270, now_ms, flight::FlightState::Unknown, 0x626262);
    flight.rig.run_seconds(now_ms, 2);
    CHECK(flight.radar_counts("1"));

    flight.rig.show(now_ms, go::Page::Nearby);
    flight.rig.run_seconds(now_ms, 2);
    CHECK(flight.nearby_counts("2"));
}
