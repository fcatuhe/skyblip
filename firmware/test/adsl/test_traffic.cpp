// ADS-L 4 SRD-860 issue 2 G.1: every Traffic payload field, at the offset and in the code printed.
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <vector>

#include "core/flight/extrapolate.h"
#include "core/flight/state.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/menu.h"
#include "simulator/simulator.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {
const uint8_t* payload_of(const adsl::Packet& packet) { return packet.payload + 5; }

// A field, read the way B.3 lays one out: bit 0 of the payload is bit 0 of its first byte.
uint32_t field(const uint8_t* payload, int offset, int width) {
    uint32_t value = 0;
    for (int i = 0; i < width; i++) {
        const int bit = offset + i;
        value |= static_cast<uint32_t>((payload[bit / 8] >> (bit % 8)) & 1u) << i;
    }
    return value;
}

model::Ownship flying() {
    model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.climb_valid = true;
    own.latitude_1e7 = 481234500;
    own.longitude_1e7 = 81234500;
    own.altitude_hae_mm = 1000000;
    own.speed_mm_s = 120 * 1000;
    own.climb_mm_s = 10000;
    own.track_cdeg = 9000;
    own.hdop_e2 = 90;
    own.vdop_e2 = 150;
    own.flight_state = 2;
    return own;
}

adsl::Packet traffic_packet() {
    adsl::Packet packet{};
    protocol::from_own(packet, flying(), 0x123456, 6, 4);
    return packet;
}

}  // namespace

// The clause is emphatic: a field nobody measured is transmitted as invalid, never as zero.
TEST_CASE("ADS-L.4.SRD860.G.1: a field with no valid information is transmitted as invalid") {
    model::Ownship own = flying();
    own.fix_valid = false;
    own.climb_valid = false;
    adsl::Packet packet{};
    protocol::from_own(packet, own, 0x123456, 6, 4);
    CHECK_FALSE(packet.has_position());
    CHECK_FALSE(packet.has_altitude());
    CHECK_FALSE(packet.has_speed());
    CHECK_FALSE(packet.has_climb());
}

// The receiver solves on the quarter second, and the payload carries that solution as it stands.
TEST_CASE("ADS-L.4.SRD860.G.1.1: the timestamp names the solution's own quarter second") {
    model::Ownship own = flying();
    own.utc_s = 1785628800;
    own.utc_ms = 750;
    adsl::Packet packet{};
    protocol::from_own(packet, own, 0x123456, 6, 4);
    CHECK(packet.timestamp == adsl::timestamp_code(own.utc_s, 750));

    adsl::Packet measured{};
    measured.reset();
    measured.set_latitude_1e7(own.latitude_1e7);
    measured.set_longitude_1e7(own.longitude_1e7);
    CHECK(packet.latitude_1e7() == measured.latitude_1e7());
    CHECK(packet.longitude_1e7() == measured.longitude_1e7());
}

// G.1.1's 10 ms: a stamp a few ms off a quarter names it, a stamp between quarters builds nothing.
TEST_CASE("ADS-L.4.SRD860.G.1.1: a solution off the quarter seconds builds no Traffic") {
    model::Ownship own = flying();
    own.utc_s = 1785628800;
    adsl::Packet packet{};
    own.utc_ms = 600;
    CHECK_FALSE(protocol::from_own(packet, own, 0x123456, 6, 4));
    own.utc_ms = 745;
    REQUIRE(protocol::from_own(packet, own, 0x123456, 6, 4));
    CHECK(packet.timestamp == adsl::timestamp_code(own.utc_s, 750));
    own.utc_ms = 995;
    REQUIRE(protocol::from_own(packet, own, 0x123456, 6, 4));
    CHECK(packet.timestamp == adsl::timestamp_code(own.utc_s + 1, 0));
}

// TODO: fc 18nov26 the L76K's quarters are unmeasured against its PPS, until a bench capture
TEST_CASE("ADS-L.4.SRD860.G.1.1: the L76K solves on the quarter its stamp names, within 10 ms" *
          doctest::skip()) {
    FAIL("unmeasured: no bench has timed the L76K's 2 Hz solutions against its PPS edge");
}

TEST_CASE("ADS-L.4.SRD860.G.1.2: flight state is 0 unknown, 1 on ground, 2 airborne, never 3") {
    CHECK(static_cast<uint8_t>(flight::FlightState::Unknown) == 0);
    CHECK(static_cast<uint8_t>(flight::FlightState::Ground) == 1);
    CHECK(static_cast<uint8_t>(flight::FlightState::Airborne) == 2);

    for (uint8_t state : {uint8_t(0), uint8_t(1), uint8_t(2)}) {
        model::Ownship own = flying();
        own.flight_state = state;
        adsl::Packet packet{};
        protocol::from_own(packet, own, 0x123456, 6, 4);
        CHECK(packet.flight_state == state);
        CHECK(packet.flight_state != 3);
    }
}

// Our reading, put to EASA: with no ground speed to tell, a craft that can hover is Undefined.
TEST_CASE("ADS-L.4.SRD860.G.1.2: a paraglider standing still is Undefined, and goes at 1 Hz") {
    const auto flown = [](uint8_t category, uint8_t& state) {
        simulator::Simulator simulator;
        REQUIRE(simulator.setup() == Status::Ok);
        simulator.product().settings().aircraft_type = category;
        simulator.world().set_fix(true);
        simulator.world().set_speed_kt(0);
        run_on(simulator, past_settling(simulator), 15000);
        int sent = 0;
        for (const simulator::AirRecord& record :
             records_of(simulator.world().air(), adsl::Packet::kPayloadTypePosition)) {
            adsl::Packet packet{};
            REQUIRE(packet_of(record, packet));
            state = packet.flight_state;
            sent++;
        }
        return sent;
    };
    for (const uint8_t hovers : {3, 5, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17}) {
        CAPTURE(int(hovers));
        model::Ownship parked = flying();
        parked.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
        adsl::Packet packet{};
        REQUIRE(protocol::from_own(packet, parked, 0x123456, 6, hovers));
        CHECK(packet.flight_state == static_cast<uint8_t>(flight::FlightState::Unknown));
    }
    uint8_t paraglider_state = 3;
    CHECK(flown(7, paraglider_state) >= 14);
    CHECK(paraglider_state == static_cast<uint8_t>(flight::FlightState::Unknown));
    uint8_t glider_state = 3;
    CHECK(flown(4, glider_state) <= 2);
    CHECK(glider_state == static_cast<uint8_t>(flight::FlightState::Ground));
}

TEST_CASE("ADS-L.4.SRD860.G.1.3: the aircraft category is five bits, and a glider is 4") {
    for (uint8_t category = 0; category < 32; category++) {
        model::Ownship own = flying();
        adsl::Packet packet{};
        protocol::from_own(packet, own, 0x123456, 6, category);
        CHECK(packet.aircraft_category == category);
        CHECK(field(payload_of(packet), 8, 5) == category);
    }
    adsl::Packet glider{};
    protocol::from_own(glider, flying(), 0x123456, 6, 4);
    CHECK(glider.aircraft_category == 4);

    // A pilot can pick 15, which Issue 1 does not name, and E.1.1 dates that payload to Issue 2.
    adsl::Packet heavy{};
    protocol::from_own(heavy, flying(), 0x123456, 6, 15);
    CHECK(heavy.aircraft_category == 15);
    CHECK(heavy.protocol_version() == 1);
    CHECK(glider.protocol_version() == 0);
}

// Every type the panel offers is a G.1.3 code a person flies seated in, and goes out as that code.
TEST_CASE("ADS-L.4.SRD860.G.1.3: every type a pilot picks goes out as its own code") {
    for (const go::AircraftType& type : go::kAircraftTypes) {
        CAPTURE(int(type.code));
        CHECK((type.code <= 10 || (type.code >= 15 && type.code <= 17)));
        adsl::Packet packet{};
        protocol::from_own(packet, flying(), 0x123456, 6, type.code);
        CHECK(packet.aircraft_category == type.code);
        CHECK(packet.protocol_version() == (type.code >= 15 ? 1 : 0));
    }
}

// A Go one step south of the equator sends Issue 2, or 0xFFFFFE, one step on, to an Issue 1 client.
TEST_CASE("ADS-L.4.SRD860.G.1.5: own-ship never sends Issue 1 a 0xFFFFFF it would read as no fix") {
    model::Ownship own = flying();
    own.latitude_1e7 = -100;
    adsl::Packet packet{};
    protocol::from_own(packet, own, 0x123456, 6, 4);
    REQUIRE(field(payload_of(packet), 16, 24) == 0xFFFFFFu);
    CHECK(packet.protocol_version() == 1);
    REQUIRE(protocol::issue1_form(packet, own));
    CHECK(field(payload_of(packet), 16, 24) == 0xFFFFFEu);
    CHECK(packet.protocol_version() == 0);
    CHECK(packet.has_position());
}

// Issue 2 split broader Issue 1 classes, so each new category has a truthful Issue 1 reading.
TEST_CASE("ADS-L.4.SRD860.G.1.3: for an Issue 1 client 14 to 17 read as Issue 1's own classes") {
    const struct {
        uint8_t issue2;
        uint8_t issue1;
    } kClasses[] = {{14, 0}, {15, 3}, {16, 7}, {17, 7}, {3, 3}, {4, 4}, {7, 7}, {13, 13}};
    for (const auto& pair : kClasses) {
        CAPTURE(int(pair.issue2));
        adsl::Packet packet{};
        protocol::from_own(packet, flying(), 0x123456, 6, pair.issue2);
        REQUIRE(protocol::issue1_form(packet, flying()));
        CHECK(packet.aircraft_category == pair.issue1);
        CHECK(packet.protocol_version() == 0);
    }
}

// The adapter decides the code, so the clause is checked on what own-ship sends and what a receiver
// keeps.
TEST_CASE(
    "ADS-L.4.SRD860.G.1.4: own-ship sends emergency status 1, no emergency, and a target keeps "
    "it") {
    const adsl::Packet packet = traffic_packet();
    CHECK(packet.emergency == adsl::kEmergencyNone);
    model::AircraftObservation observation{};
    REQUIRE(protocol::to_observation(packet, events::Stamp{}, -80, model::Source::AdslDirect,
                                     observation));
    CHECK(observation.emergency == adsl::kEmergencyNone);
}

// skyBlip's gate, not the stack's: a frame whose position is 0x800000 is never handed on as
// traffic.
TEST_CASE(
    "ADS-L.4.SRD860.G.1.5: own-ship without a fix sends 0x800000, which is never an aircraft") {
    model::Ownship own = flying();
    own.fix_valid = false;
    adsl::Packet packet{};
    REQUIRE(protocol::from_own(packet, own, 0x123456, 6, 4));
    CHECK(packet.field(adsl::Packet::kLatitudeField) == adsl::Packet::kPositionInvalidCode);
    CHECK(packet.field(adsl::Packet::kLongitudeField) == adsl::Packet::kPositionInvalidCode);
    model::AircraftObservation observation{};
    CHECK_FALSE(protocol::to_observation(packet, events::Stamp{}, -80, model::Source::AdslDirect,
                                         observation));
    CHECK_FALSE(observation.position_valid);

    REQUIRE(protocol::to_observation(traffic_packet(), events::Stamp{}, -80,
                                     model::Source::AdslDirect, observation));
    CHECK(observation.position_valid);
}

// A 2D fix still reports a height, the last one the receiver solved, and it went out as valid.
TEST_CASE("ADS-L.4.SRD860.G.1.7: a 2D fix sends its position, and its altitude as unavailable") {
    model::Ownship own = flying();
    own.vdop_e2 = 0;
    adsl::Packet packet{};
    protocol::from_own(packet, own, 0x123456, 6, 4);
    CHECK(packet.has_position());
    CHECK_FALSE(packet.has_altitude());
    CHECK(packet.has_speed());
}

// The clause calls the track invalid when the speed is, so nothing may fly a target along it.
TEST_CASE("ADS-L.4.SRD860.G.1.10: a target with no ground speed is never flown along its track") {
    adsl::Packet packet = traffic_packet();
    packet.set_speed_invalid();
    packet.set_track_c9(256);

    model::AircraftObservation observation{};
    REQUIRE(protocol::to_observation(packet, events::Stamp{}, -80, model::Source::AdslDirect,
                                     observation));
    CHECK_FALSE(observation.speed_valid);
    CHECK(observation.speed_q == 0);

    const flight::Prediction carried = flight::extrapolate(observation, 1000);
    CHECK_FALSE(carried.valid);
    CHECK(carried.latitude_1e7 == observation.latitude_1e7);
    CHECK(carried.longitude_1e7 == observation.longitude_1e7);

    // A ground speed of zero is the clause's other invalid track, and it moves no further.
    adsl::Packet parked = traffic_packet();
    parked.set_speed_q(0);
    parked.set_track_c9(256);
    model::AircraftObservation still{};
    REQUIRE(
        protocol::to_observation(parked, events::Stamp{}, -80, model::Source::AdslDirect, still));
    const flight::Prediction stays = flight::extrapolate(still, 1000);
    CHECK(stays.latitude_1e7 == still.latitude_1e7);
    CHECK(stays.longitude_1e7 == still.longitude_1e7);
}

// The age runs to the instant the burst goes on air, not to the top of the second it goes in.
TEST_CASE("ADS-L.4.SRD860.G.1.16: a solution over 500 ms old at the burst's instant is not armed") {
    CHECK(timing::Transmitter::kFixMaxAgeMs == 500);

    timing::ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;
    const timing::SlotPlan plan = timing::Scheduler::plan(timing::kWindowAMs.start_ms, clock);

    timing::Transmitter tx;
    tx.configure(0x123456);
    const int at_ms = tx.attempt(plan, 100, 1000, true, -1000).at_ms;
    const int32_t ceiling_ms = 500 - timing::Transmitter::kFixAgeAllowanceMs;
    CHECK(tx.attempt(plan, 100, 1000, true, ceiling_ms - at_ms).go);
    CHECK_FALSE(tx.attempt(plan, 100, 1000, true, ceiling_ms + 1 - at_ms).go);
}

// skyBlip reads "0.1 Hz (once every 10 seconds)" as an average: C.2.5 and the parity rule make it
// 9 then 11 s.
TEST_CASE(
    "ADS-L.4.SRD860.G.1.16: on the ground Traffic averages exactly 0.1 Hz, 9 then 11 s apart") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        timing::Transmitter tx;
        tx.configure(serial);
        std::vector<uint32_t> sent;
        for (uint32_t utc_s = 2000; utc_s < 2200; utc_s++)
            if (tx.turn(timing::Transmitter::Payload::Position, utc_s, /*airborne=*/false))
                sent.push_back(utc_s);
        CHECK(sent.size() == 20);
        for (size_t i = 2; i < sent.size(); i++) {
            CHECK(sent[i] - sent[i - 2] == 20);
            CHECK((sent[i] - sent[i - 1] == 9 || sent[i] - sent[i - 1] == 11));
        }
        for (uint32_t utc_s = 2000; utc_s < 2020; utc_s++)
            CHECK(tx.turn(timing::Transmitter::Payload::Position, utc_s, true));
    }
}
