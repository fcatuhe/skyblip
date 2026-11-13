// Every system that hears an aircraft writes its flight state into one field, and the rules read
// only that.
#include <initializer_list>

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsb_observation.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/alptas.h"
#include "core/protocol/nmea_out.h"
#include "core/traffic/alarm.h"
#include "core/traffic/formation.h"
#include "core/traffic/lease.h"
#include "core/traffic/table.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "protocols/adsb/assembly.h"
#include "protocols/adsb/message.h"
#include "protocols/alptas/frame.h"
#include "test/support/adsb_frames.h"

using namespace skyblip;
using namespace skyblip::traffic;

namespace {

constexpr uint32_t kUtcS = 1700000000;
constexpr int32_t kOwnLatitude1e7 = 481000000;
constexpr int32_t kOwnLongitude1e7 = 81000000;
constexpr int32_t kOwnAltitudeM = 1000;
constexpr int32_t kFourHundredMetresNorth1e7 = 400 * 1000000 / 11132;

model::Ownship own_at(uint32_t at_ms) {
    model::Ownship own{};
    own.fix_valid = true;
    own.fix_at_ms = at_ms;
    own.latitude_1e7 = kOwnLatitude1e7;
    own.longitude_1e7 = kOwnLongitude1e7;
    own.altitude_hae_mm = kOwnAltitudeM * 1000;
    own.speed_mm_s = 30000;
    return own;
}

model::AircraftObservation sender(flight::FlightState state) {
    model::AircraftObservation observation{};
    observation.address = 0x3C4D5E;
    observation.address_mapping_table = 5;
    observation.aircraft_category = 4;
    observation.flight_state = static_cast<uint8_t>(state);
    observation.latitude_1e7 = kOwnLatitude1e7 + kFourHundredMetresNorth1e7;
    observation.longitude_1e7 = kOwnLongitude1e7;
    observation.altitude_m = kOwnAltitudeM;
    observation.speed_valid = true;
    observation.position_valid = true;
    return observation;
}

model::AircraftObservation stamped(model::AircraftObservation heard) {
    heard.received.at_s = kUtcS;
    heard.at_ms = 1000;
    return heard;
}

model::AircraftObservation over_adsl(const model::AircraftObservation& sent) {
    adsl::Packet packet;
    packet.reset();
    packet.set_address(sent.address);
    packet.set_address_mapping_table(sent.address_mapping_table);
    packet.flight_state = sent.flight_state;
    packet.aircraft_category = sent.aircraft_category;
    packet.emergency = 1;
    packet.set_latitude_1e7(sent.latitude_1e7);
    packet.set_longitude_1e7(sent.longitude_1e7);
    packet.set_altitude_m(sent.altitude_m);
    packet.set_speed_q(sent.speed_q);
    model::AircraftObservation heard{};
    REQUIRE(protocol::to_observation(packet, events::Stamp{}, 0, model::Source::AdslDirect, heard));
    return stamped(heard);
}

// G.1.7 lets a position say its altitude is invalid, and the decoder keeps saying so.
model::AircraftObservation over_adsl_without_altitude(const model::AircraftObservation& sent) {
    adsl::Packet packet;
    packet.reset();
    packet.set_address(sent.address);
    packet.set_address_mapping_table(sent.address_mapping_table);
    packet.flight_state = sent.flight_state;
    packet.aircraft_category = sent.aircraft_category;
    packet.set_latitude_1e7(sent.latitude_1e7);
    packet.set_longitude_1e7(sent.longitude_1e7);
    packet.set_altitude_invalid();
    packet.set_speed_q(sent.speed_q);
    model::AircraftObservation heard{};
    REQUIRE(protocol::to_observation(packet, events::Stamp{}, 0, model::Source::AdslDirect, heard));
    return stamped(heard);
}

model::AircraftObservation over_uplink(const model::AircraftObservation& sent) {
    protocol::AdslUplink codec;
    uint8_t packet[protocol::AdslUplink::kPacketBytes] = {0};
    REQUIRE(codec.encode(&sent, 1, kUtcS, 300, 0x00FFFE, adsl::kAddressTableManufacturerFirst,
                         packet) == Status::Ok);
    model::AircraftObservation heard{};
    protocol::AdslUplink::DecodeStatistics statistics{};
    REQUIRE(codec.decode(packet, sizeof(packet), kUtcS, 300, &heard, 1, statistics) == Status::Ok);
    REQUIRE(statistics.targets == 1);
    return stamped(heard);
}

model::AircraftObservation over_alptas(const model::AircraftObservation& sent) {
    uint8_t frame[alptas::kFrameBytes];
    REQUIRE(protocol::alptas_encode(frame, sent, kUtcS) == Status::Ok);
    model::AircraftObservation heard{};
    REQUIRE(protocol::alptas_decode(frame, kUtcS, kOwnLatitude1e7, kOwnLongitude1e7, heard) ==
            Status::Ok);
    return stamped(heard);
}

// INFO: fc 10nov26 the sender is a glider, which ADS-B names B1: type code 3, category 1
model::AircraftObservation over_adsb(const model::AircraftObservation& sent) {
    const double latitude = sent.latitude_1e7 / 1e7;
    const double longitude = sent.longitude_1e7 / 1e7;
    const int32_t altitude_ft = to_feet(Metres(sent.altitude_m)).value;
    const bool parked = flight::on_ground(sent.flight_state);
    const uint8_t capability = parked ? 4 : 5;
    AdsbFrame frames[4] = {adsb_identification(sent.address, capability, 3, 1, "D1234")};
    int count = 1;
    for (bool odd : {false, true})
        frames[count++] =
            parked ? adsb_surface_position(sent.address, capability, latitude, longitude, odd)
                   : adsb_airborne_position(sent.address, capability, latitude, longitude, odd,
                                            altitude_ft, true);
    if (!parked) frames[count++] = adsb_ground_velocity(sent.address, capability, 0, 0, 0, 0);

    adsb::Assembly assembly;
    adsb::Reference reference;
    reference.position = {kOwnLatitude1e7, kOwnLongitude1e7};
    reference.valid = true;
    model::AircraftObservation heard{};
    for (int i = 0; i < count; i++) {
        adsb::Message message;
        REQUIRE(adsb_hear(frames[i], message) == Status::Ok);
        REQUIRE(protocol::adsb_fold(assembly, message, static_cast<uint32_t>(500 * i),
                                    events::Stamp{}, reference, heard));
    }
    REQUIRE(heard.position_valid);
    REQUIRE(heard.aircraft_category == sent.aircraft_category);
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
    {"ADS-B", over_adsb},
};

bool joins_formation(const model::AircraftObservation& heard) {
    formation::Tracker tracker;
    for (uint32_t now_ms = 1000; now_ms <= 1000 + formation::kTogetherHoldMs + 1000;
         now_ms += 1000) {
        model::AircraftObservation fix = heard;
        fix.at_ms = now_ms;
        fix.received.at_s = now_ms / 1000;
        tracker.observe(own_at(now_ms), fix, {}, now_ms);
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
        TrafficTable table;
        REQUIRE(table.update(parked, kUtcS) >= 0);

        table.age_out(kUtcS + kGroundTargetForgetS);
        CHECK(table.find(parked.address_mapping_table, parked.address) >= 0);
        table.age_out(kUtcS + kGroundTargetForgetS + 1);
        CHECK(table.find(parked.address_mapping_table, parked.address) == -1);
    }
}

TEST_CASE("traffic: a parked aircraft is never a wingman, whichever system heard it") {
    for (const System& system : kSystems) {
        CAPTURE(system.name);
        CHECK(joins_formation(system.heard(sender(flight::FlightState::Airborne))));
        CHECK_FALSE(joins_formation(system.heard(sender(flight::FlightState::Ground))));
    }
}

// Unknown is a state, not 0 m: nothing downstream may read the target as level with us.
TEST_CASE(
    "traffic: an ADS-L target with no altitude is graded on distance, never a wingman, no PFLAA") {
    const model::AircraftObservation heard =
        over_adsl_without_altitude(sender(flight::FlightState::Airborne));
    REQUIRE_FALSE(heard.altitude_valid);

    const model::Ownship own = own_at(1000);
    const AlarmAssessment graded = assess(own, heard, 1000);
    CHECK(graded.valid);
    CHECK(graded.level == Level::Advisory);
    CHECK_FALSE(graded.relative_altitude_valid);

    CHECK_FALSE(joins_formation(heard));

    char sentence[128];
    CHECK(protocol::format_pflaa(sentence, sizeof(sentence), own, heard, 1, nullptr) == 0);
}
