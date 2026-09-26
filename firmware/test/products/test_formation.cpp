// What a formation the device found by itself does to the annunciator, and what takes it back.
#include "core/traffic/formation.h"
#include "core/units/units.h"
#include "core/util/intmath.h"
#include "doctest/doctest.h"
#include "test/support/screen_rig.h"

using namespace skyblip;

namespace {

uint16_t c9(int deg) { return static_cast<uint16_t>(((deg % 360 + 360) % 360) * 512 / 360); }

model::AircraftObs contact(const model::OwnState& own, int north_m, int east_m, int up_m, int mps,
                           int track_deg, uint32_t at_ms) {
    model::AircraftObs t{};
    t.addr = 0x515151;
    t.addr_table = 6;
    t.position_valid = true;
    t.speed_valid = true;
    t.speed_q = static_cast<uint16_t>(mps * 4);
    t.track_c9 = c9(track_deg);
    t.alt_m = to_metres(Millimetres(own.alt_mm)).v + up_m;
    t.lat_1e7 = own.lat_1e7 + static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    const int16_t ang =
        static_cast<int16_t>((static_cast<int64_t>(own.lat_1e7) * 65536) / 3600000000LL);
    const int64_t east_scaled = static_cast<int64_t>(east_m) * 16384 / icos(ang);
    t.lon_1e7 = own.lon_1e7 + static_cast<int32_t>(east_scaled * 1000000 / 11132);
    t.received.at_s = at_ms / 1000;
    t.at_ms = at_ms;
    t.source = model::Source::AdslDirect;
    return t;
}

struct Flight {
    Rig rig;

    Flight() {
        rig.state.own.fix_valid = true;
        rig.state.own.lat_1e7 = 481000000;
        rig.state.own.lon_1e7 = 81000000;
        rig.state.own.alt_mm = 1000000;
        rig.state.own.speed_mm_s = 40000;
        rig.state.own.track_cdeg = 9000;
    }

    void hear(int north_m, int east_m, int up_m, int mps, int track_deg, uint32_t now_ms) {
        rig.state.own.fix_ms = now_ms;
        const model::AircraftObs obs =
            contact(rig.state.own, north_m, east_m, up_m, mps, track_deg, now_ms);
        rig.state.traffic.update(obs, now_ms / 1000);
        rig.alarm_service.tick(now_ms);
    }

    const traffic::Target* target() {
        const int at = rig.state.traffic.find(6, 0x515151);
        return at < 0 ? nullptr : rig.state.traffic.at(at);
    }

    uint32_t hold_station(int north_m, int east_m, uint32_t from_ms) {
        uint32_t t = from_ms;
        for (; t <= from_ms + formation::kTogetherHoldMs + 2000; t += 1000)
            hear(north_m, east_m, 10, 40, 90, t);
        return t;
    }

    uint32_t hold_station(uint32_t from_ms) { return hold_station(-60, -120, from_ms); }
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

TEST_CASE("formation: a member closing on us alarms on that fix, and is no longer a member") {
    Flight flight;
    const uint32_t after = flight.hold_station(1000);
    REQUIRE(flight.target()->in_formation);

    // The same aircraft, now 400 m off the nose coming the other way.
    flight.hear(0, 400, 0, 40, 270, after);
    CHECK(flight.rig.state.alarm_level == traffic::Level::Advisory);
    CHECK_FALSE(flight.target()->in_formation);
    CHECK(flight.rig.alarm_service.formation_members() == 0);
}

TEST_CASE("formation: a pair splitting is not alarmed while it separates") {
    Flight flight;
    uint32_t t = flight.hold_station(1000);
    REQUIRE(flight.target()->in_formation);

    for (int east = -300; east >= -900; east -= 200, t += 1000) {
        flight.hear(-60, east, 10, 40, 90, t);
        CHECK(flight.rig.state.alarm_level == traffic::Level::None);
        CHECK(flight.rig.alarm_service.announcing_level() == traffic::Level::None);
    }
    CHECK(flight.rig.alarm_service.formation_members() == 0);
}

TEST_CASE("formation: a split that turns back into a closure is alarmed again") {
    Flight flight;
    uint32_t t = flight.hold_station(1000);
    flight.hear(-60, -300, 10, 40, 90, t);
    t += 1000;
    flight.hear(-60, -500, 10, 40, 90, t);
    t += 1000;
    REQUIRE(flight.rig.state.alarm_level == traffic::Level::None);

    // The one that left is overtaking us from behind at 40 m/s.
    flight.hear(-60, -500, 10, 80, 90, t);
    CHECK(flight.rig.state.alarm_level > traffic::Level::None);
    CHECK_FALSE(flight.target()->in_formation);
}

TEST_CASE("formation: a contact nobody has heard from is forgotten, membership and all") {
    Flight flight;
    const uint32_t after = flight.hold_station(1000);
    REQUIRE(flight.rig.alarm_service.formation_members() == 1);

    const uint32_t gone = after + formation::kContactForgetMs + 1;
    flight.rig.state.traffic.age_out(gone / 1000);
    flight.rig.alarm_service.tick(gone);
    CHECK(flight.rig.alarm_service.formation_members() == 0);
}
