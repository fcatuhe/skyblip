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

model::Ownship own_at(uint32_t at_ms) {
    model::Ownship o{};
    o.fix_valid = true;
    o.fix_ms = at_ms;
    o.lat_1e7 = kOwnLat;
    o.lon_1e7 = kOwnLon;
    o.alt_mm = kOwnAltM * 1000;
    o.speed_mm_s = 30000;
    return o;
}

model::AircraftObservation sender(flight::FlightState state) {
    model::AircraftObservation o{};
    o.address = 0x3C4D5E;
    o.address_mapping_table = 5;
    o.aircraft_cat = 4;
    o.flight_state = static_cast<uint8_t>(state);
    o.lat_1e7 = kOwnLat + kFourHundredMetresNorthE7;
    o.lon_1e7 = kOwnLon;
    o.alt_m = kOwnAltM;
    o.speed_valid = true;
    o.position_valid = true;
    return o;
}

model::AircraftObservation stamped(model::AircraftObservation heard) {
    heard.received.at_s = kUtc;
    heard.at_ms = 1000;
    return heard;
}

model::AircraftObservation over_adsl(const model::AircraftObservation& sent) {
    protocol::AdslPacket p;
    p.init();
    p.set_address(sent.address);
    p.set_address_mapping_table(sent.address_mapping_table);
    p.FlightState = sent.flight_state;
    p.AcftCat = sent.aircraft_cat;
    p.Emergency = 1;
    p.set_lat_1e7(sent.lat_1e7);
    p.set_lon_1e7(sent.lon_1e7);
    p.set_alt_m(sent.alt_m);
    p.set_speed_q(sent.speed_q);
    model::AircraftObservation heard{};
    REQUIRE(protocol::to_observation(p, events::Stamp{}, 0, model::Source::AdslDirect, heard));
    return stamped(heard);
}

model::AircraftObservation over_uplink(const model::AircraftObservation& sent) {
    protocol::AdslUplink codec;
    uint8_t frame[protocol::AdslUplink::kFrameBytes] = {0};
    REQUIRE(codec.encode(&sent, 1, 0, frame) == Status::Ok);
    model::AircraftObservation heard{};
    protocol::AdslUplink::DecodeStats stats{};
    REQUIRE(codec.decode(frame, &heard, 1, stats) == Status::Ok);
    REQUIRE(stats.targets == 1);
    return stamped(heard);
}

model::AircraftObservation over_alptas(const model::AircraftObservation& sent) {
    uint8_t frame[protocol::kAlptasFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, sent, kUtc, kOwnLat, kOwnLon) == Status::Ok);
    model::AircraftObservation heard{};
    REQUIRE(protocol::alptas_decode(frame, kUtc, kOwnLat, kOwnLon, heard) == Status::Ok);
    return stamped(heard);
}

struct System {
    const char* name;
    model::AircraftObservation (*heard)(const model::AircraftObservation&);
};

constexpr System kSystems[] = {
    {"ADS-L direct", over_adsl},
    {"ADS-L uplink", over_uplink},
    {"ALP-TAS", over_alptas},
};

bool joins_formation(const model::AircraftObservation& heard) {
    formation::Tracker tracker;
    for (uint32_t t = 1000; t <= 1000 + formation::kTogetherHoldMs + 1000; t += 1000) {
        model::AircraftObservation fix = heard;
        fix.at_ms = t;
        fix.received.at_s = t / 1000;
        tracker.observe(own_at(t), fix, {}, t);
    }
    return tracker.members() == 1;
}

}  // namespace

TEST_CASE("traffic: every system hands a parked aircraft over as on the ground") {
    for (const System& system : kSystems) {
        CAPTURE(system.name);
        const model::AircraftObservation parked = system.heard(sender(flight::FlightState::Ground));
        CHECK(flight::state_from(parked.flight_state) == flight::FlightState::Ground);
        const model::AircraftObservation airborne =
            system.heard(sender(flight::FlightState::Airborne));
        CHECK(flight::state_from(airborne.flight_state) == flight::FlightState::Airborne);
    }
}

TEST_CASE("traffic: a parked aircraft is never graded, whichever system heard it") {
    for (const System& system : kSystems) {
        CAPTURE(system.name);
        const model::Ownship own = own_at(1000);
        const model::AircraftObservation airborne =
            system.heard(sender(flight::FlightState::Airborne));
        REQUIRE(assess(own, airborne, 1000).level == Level::Advisory);

        const AlarmAssessment parked =
            assess(own, system.heard(sender(flight::FlightState::Ground)), 1000);
        CHECK(parked.valid);
        CHECK(parked.level == Level::None);
    }
}

TEST_CASE(
    "traffic: a parked aircraft is held for six of its own reports, whichever system heard it") {
    for (const System& system : kSystems) {
        CAPTURE(system.name);
        const model::AircraftObservation parked = system.heard(sender(flight::FlightState::Ground));
        TrafficTable tbl;
        REQUIRE(tbl.update(parked, kUtc) >= 0);

        tbl.age_out(kUtc + kGroundTargetForgetS);
        CHECK(tbl.find(parked.address_mapping_table, parked.address) >= 0);
        tbl.age_out(kUtc + kGroundTargetForgetS + 1);
        CHECK(tbl.find(parked.address_mapping_table, parked.address) == -1);
    }
}

TEST_CASE("traffic: a parked aircraft is never a wingman, whichever system heard it") {
    for (const System& system : kSystems) {
        CAPTURE(system.name);
        CHECK(joins_formation(system.heard(sender(flight::FlightState::Airborne))));
        CHECK_FALSE(joins_formation(system.heard(sender(flight::FlightState::Ground))));
    }
}
