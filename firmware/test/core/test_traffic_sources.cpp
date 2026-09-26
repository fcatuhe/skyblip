// Every system that hears an aircraft writes its flight state into one field, and the rules read
// only that.
#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/alptas.h"
#include "core/traffic/alarm.h"
#include "core/traffic/formation.h"
#include "core/traffic/lease.h"
#include "core/traffic/table.h"
#include "core/units/units.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::traffic;

namespace {

constexpr uint32_t kUtc = 1700000000;
constexpr int32_t kOwnLat = 481000000;
constexpr int32_t kOwnLon = 81000000;
constexpr int32_t kOwnAltM = 1000;
constexpr int32_t kFourHundredMetresNorthE7 = 400 * 1000000 / 11132;

model::OwnState own_at(uint32_t at_ms) {
    model::OwnState o{};
    o.fix_valid = true;
    o.fix_ms = at_ms;
    o.lat_1e7 = kOwnLat;
    o.lon_1e7 = kOwnLon;
    o.alt_mm = kOwnAltM * 1000;
    o.speed_mm_s = 30000;
    return o;
}

model::AircraftObs sender(flight::FlightState state) {
    model::AircraftObs o{};
    o.addr = 0x3C4D5E;
    o.addr_table = 5;
    o.aircraft_cat = 4;
    o.flight_state = static_cast<uint8_t>(state);
    o.lat_1e7 = kOwnLat + kFourHundredMetresNorthE7;
    o.lon_1e7 = kOwnLon;
    o.alt_m = kOwnAltM;
    o.speed_valid = true;
    o.position_valid = true;
    return o;
}

model::AircraftObs stamped(model::AircraftObs heard) {
    heard.received.at_s = kUtc;
    heard.at_ms = 1000;
    return heard;
}

model::AircraftObs over_adsl(const model::AircraftObs& sent) {
    protocol::AdslPacket p;
    p.init();
    p.set_address(sent.addr);
    p.set_addr_table(sent.addr_table);
    p.FlightState = sent.flight_state;
    p.AcftCat = sent.aircraft_cat;
    p.Emergency = 1;
    p.set_lat_1e7(sent.lat_1e7);
    p.set_lon_1e7(sent.lon_1e7);
    p.set_alt_m(sent.alt_m);
    p.set_speed_q(sent.speed_q);
    model::AircraftObs heard{};
    REQUIRE(protocol::to_obs(p, events::Stamp{}, 0, model::Source::AdslDirect, heard));
    return stamped(heard);
}

model::AircraftObs over_uplink(const model::AircraftObs& sent) {
    protocol::AdslUplink codec;
    uint8_t frame[protocol::AdslUplink::kFrameBytes] = {0};
    REQUIRE(codec.encode(&sent, 1, 0, frame) == Status::Ok);
    model::AircraftObs heard{};
    protocol::AdslUplink::DecodeStats stats{};
    REQUIRE(codec.decode(frame, &heard, 1, stats) == Status::Ok);
    REQUIRE(stats.targets == 1);
    return stamped(heard);
}

model::AircraftObs over_alptas(const model::AircraftObs& sent) {
    uint8_t frame[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, sent, kUtc, kOwnLat, kOwnLon) == Status::Ok);
    model::AircraftObs heard{};
    REQUIRE(protocol::alptas_decode(frame, kUtc, kOwnLat, kOwnLon, heard) == Status::Ok);
    return stamped(heard);
}

struct System {
    const char* name;
    model::AircraftObs (*heard)(const model::AircraftObs&);
};

constexpr System kSystems[] = {
    {"ADS-L direct", over_adsl},
    {"ADS-L uplink", over_uplink},
    {"ALP-TAS", over_alptas},
};

bool joins_formation(const model::AircraftObs& heard) {
    formation::Tracker tracker;
    for (uint32_t t = 1000; t <= 1000 + formation::kTogetherHoldMs + 1000; t += 1000) {
        model::AircraftObs fix = heard;
        fix.at_ms = t;
        tracker.observe(own_at(t), fix, t);
    }
    return tracker.members() == 1;
}

}  // namespace

TEST_CASE("traffic: every system hands a parked aircraft over as on the ground") {
    for (const System& system : kSystems) {
        CAPTURE(system.name);
        const model::AircraftObs parked = system.heard(sender(flight::FlightState::OnGround));
        CHECK(flight::state_from(parked.flight_state) == flight::FlightState::OnGround);
        const model::AircraftObs airborne = system.heard(sender(flight::FlightState::Airborne));
        CHECK(flight::state_from(airborne.flight_state) == flight::FlightState::Airborne);
    }
}

TEST_CASE("traffic: a parked aircraft is never graded, whichever system heard it") {
    for (const System& system : kSystems) {
        CAPTURE(system.name);
        const model::OwnState own = own_at(1000);
        const model::AircraftObs airborne = system.heard(sender(flight::FlightState::Airborne));
        REQUIRE(assess(own, airborne, 1000).level == Level::Advisory);

        const AlarmAssessment parked =
            assess(own, system.heard(sender(flight::FlightState::OnGround)), 1000);
        CHECK(parked.valid);
        CHECK(parked.level == Level::None);
    }
}

TEST_CASE(
    "traffic: a parked aircraft is held for six of its own reports, whichever system heard it") {
    for (const System& system : kSystems) {
        CAPTURE(system.name);
        const model::AircraftObs parked = system.heard(sender(flight::FlightState::OnGround));
        TrafficTable tbl;
        REQUIRE(tbl.update(parked, kUtc) >= 0);

        tbl.age_out(kUtc + kGroundTargetForgetS);
        CHECK(tbl.find(parked.addr_table, parked.addr) >= 0);
        tbl.age_out(kUtc + kGroundTargetForgetS + 1);
        CHECK(tbl.find(parked.addr_table, parked.addr) == -1);
    }
}

TEST_CASE("traffic: a parked aircraft is never a wingman, whichever system heard it") {
    for (const System& system : kSystems) {
        CAPTURE(system.name);
        CHECK(joins_formation(system.heard(sender(flight::FlightState::Airborne))));
        CHECK_FALSE(joins_formation(system.heard(sender(flight::FlightState::OnGround))));
    }
}
