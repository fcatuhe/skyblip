// ADS-L 4 SRD-860 issue 2 G.1: every Traffic payload field, at the offset and in the code printed.
#include <cstdint>
#include <cstdlib>
#include <initializer_list>

#include "core/flight/extrapolate.h"
#include "core/flight/state.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "core/util/variable_range.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/menu.h"

using namespace skyblip;

namespace {
const uint8_t* payload_of(const protocol::AdslPacket& packet) { return packet.payload + 5; }

// A field, read the way B.3 lays one out: bit 0 of the payload is bit 0 of its first byte.
uint32_t field(const uint8_t* payload, int offset, int width) {
    uint32_t value = 0;
    for (int i = 0; i < width; i++) {
        const int bit = offset + i;
        value |= static_cast<uint32_t>((payload[bit / 8] >> (bit % 8)) & 1u) << i;
    }
    return value;
}

int32_t signed_field(const uint8_t* payload, int offset, int width) {
    const uint32_t raw = field(payload, offset, width);
    const uint32_t sign = 1u << (width - 1);
    return static_cast<int32_t>(raw >= sign ? raw - 2u * sign : raw);
}

// G.1.6's encoder: the largest exponent whose floor the value clears, and then the base.
uint32_t spec_exponential(uint32_t value, int base_bits) {
    const uint32_t threshold = 1u << base_bits;
    int exponent = 0;
    for (int candidate = 3; candidate >= 0; candidate--) {
        const uint32_t floor_value = (threshold << candidate) - threshold;
        if (value >= floor_value) {
            exponent = candidate;
            break;
        }
    }
    const uint32_t base = (value - ((threshold << exponent) - threshold)) >> exponent;
    return (static_cast<uint32_t>(exponent) << base_bits) | base;
}

uint32_t spec_exponential_decode(uint32_t code, int base_bits) {
    const uint32_t threshold = 1u << base_bits;
    const uint32_t exponent = code >> base_bits;
    const uint32_t base = code & (threshold - 1);
    return ((threshold + base) << exponent) - threshold;
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

protocol::AdslPacket traffic_packet() {
    protocol::AdslPacket packet{};
    protocol::from_own(packet, flying(), 0x123456, 6, 4);
    return packet;
}

uint32_t altitude_code(const protocol::AdslPacket& packet) {
    return field(payload_of(packet), 72, 14);
}
uint32_t speed_code(const protocol::AdslPacket& packet) { return field(payload_of(packet), 64, 8); }
uint32_t climb_code(const protocol::AdslPacket& packet) { return field(payload_of(packet), 86, 9); }
}  // namespace

// The whole layout in one case: move a field by a bit and this is what catches it.
TEST_CASE("ADS-L.4.SRD860.G.1: every field sits at the offset and width the table gives it") {
    protocol::AdslPacket packet{};
    packet.reset();
    packet.timestamp = 37;
    packet.flight_state = 2;
    packet.aircraft_category = 9;
    packet.emergency = 5;
    packet.set_latitude_1e7(481234500);
    packet.set_longitude_1e7(81234500);
    packet.set_speed_q(120 * 4);
    packet.set_altitude_m(1000);
    packet.set_climb_e8(80);
    packet.set_track_c9(400);
    packet.source_integrity = 2;
    packet.design_assurance = 1;
    packet.navigation_integrity = 11;
    packet.horizontal_accuracy = 6;
    packet.vertical_accuracy = 3;
    packet.velocity_accuracy = 2;
    packet.reserved = 0;

    const uint8_t* payload = payload_of(packet);
    CHECK(field(payload, 0, 6) == 37u);
    CHECK(field(payload, 6, 2) == 2u);
    CHECK(field(payload, 8, 5) == 9u);
    CHECK(field(payload, 13, 3) == 5u);
    CHECK(signed_field(payload, 16, 24) == packet.latitude_cordic() >> 7);
    CHECK(signed_field(payload, 40, 24) == packet.longitude_cordic() >> 8);
    CHECK(field(payload, 64, 8) == 0xC4u);
    CHECK(field(payload, 72, 14) == 0x0528u);
    CHECK(field(payload, 86, 9) == 0x048u);
    CHECK(field(payload, 95, 9) == 400u);
    CHECK(field(payload, 104, 2) == 2u);
    CHECK(field(payload, 106, 2) == 1u);
    CHECK(field(payload, 108, 4) == 11u);
    CHECK(field(payload, 112, 3) == 6u);
    CHECK(field(payload, 115, 2) == 3u);
    CHECK(field(payload, 117, 2) == 2u);
    CHECK(field(payload, 119, 1) == 0u);
}

// The clause is emphatic: a field nobody measured is transmitted as invalid, never as zero.
TEST_CASE("ADS-L.4.SRD860.G.1: a field with no valid information is transmitted as invalid") {
    model::Ownship own = flying();
    own.fix_valid = false;
    own.climb_valid = false;
    protocol::AdslPacket packet{};
    protocol::from_own(packet, own, 0x123456, 6, 4);
    CHECK_FALSE(packet.has_position());
    CHECK_FALSE(packet.has_altitude());
    CHECK_FALSE(packet.has_speed());
    CHECK_FALSE(packet.has_climb());
}

// Quarter seconds since the hour, modulo 60: a 15-second cycle, never the four unused codes.
TEST_CASE("ADS-L.4.SRD860.G.1.1: the timestamp is a quarter second inside a 15-second cycle") {
    CHECK(protocol::kTimestampCycleS == 15u);
    CHECK(protocol::kTimestampQuarterMs == 250u);
    for (uint32_t utc_s = 1785628800; utc_s < 1785628860; utc_s++) {
        for (int32_t lead = -1000; lead <= 1000; lead += 125) {
            const uint8_t code = protocol::timestamp_code(utc_s, lead);
            CHECK(code < 60);
        }
    }
    CHECK(protocol::timestamp_code(1785628800, 0) ==
          static_cast<uint8_t>((1785628800u % 15u) * 4u));
    CHECK(protocol::timestamp_code(15, 0) == 0);
    CHECK(protocol::timestamp_code(15, 250) == 1);
    CHECK(protocol::timestamp_code(15, 750) == 3);
    CHECK(protocol::timestamp_code(15, -250) == 59);
}

// A burst at 613 ms named 500 while carrying the position to 613: 113 ms apart, 14 m at 120 kt.
TEST_CASE("ADS-L.4.SRD860.G.1.1: the instant the timestamp names is the position's, within 10 ms") {
    model::Ownship own = flying();
    own.utc_s = 1785628800;
    const int32_t into_utc_ms = 613;
    protocol::AdslPacket packet{};
    protocol::from_own(packet, own, 0x123456, 6, 4,
                       protocol::BurstInstant{own.utc_s, into_utc_ms, into_utc_ms});
    const int32_t named_ms = static_cast<int32_t>(packet.timestamp % 4u) *
                             static_cast<int32_t>(protocol::kTimestampQuarterMs);
    CHECK(named_ms == 500);

    protocol::AdslPacket at_named{};
    at_named.reset();
    const flight::Prediction there = flight::extrapolate(own, named_ms);
    at_named.set_latitude_1e7(there.latitude_1e7);
    at_named.set_longitude_1e7(there.longitude_1e7);
    CHECK(packet.latitude_1e7() == at_named.latitude_1e7());
    CHECK(packet.longitude_1e7() == at_named.longitude_1e7());
}

// It dates the navigation solution, and the position is carried to it, so the pair is one moment.
TEST_CASE("ADS-L.4.SRD860.G.1.1: the timestamp and the position name the same instant") {
    model::Ownship own = flying();
    own.utc_s = 1785628800;
    protocol::BurstInstant at{};
    at.utc_s = own.utc_s;
    at.into_utc_ms = 500;
    at.since_fix_ms = 500;

    protocol::AdslPacket packet{};
    protocol::from_own(packet, own, 0x123456, 6, 4, at);
    CHECK(packet.timestamp == protocol::timestamp_code(own.utc_s, 500));

    protocol::AdslPacket at_fix{};
    protocol::from_own(at_fix, own, 0x123456, 6, 4, protocol::BurstInstant{own.utc_s, 0, 0});
    CHECK(at_fix.timestamp == protocol::timestamp_code(own.utc_s, 0));
    // Half a second due east at 120 m/s: the transmitted position moved with the timestamp.
    CHECK(packet.longitude_1e7() > at_fix.longitude_1e7());
}

TEST_CASE("ADS-L.4.SRD860.G.1.2: flight state is 0 unknown, 1 on ground, 2 airborne, never 3") {
    CHECK(static_cast<uint8_t>(flight::FlightState::Unknown) == 0);
    CHECK(static_cast<uint8_t>(flight::FlightState::Ground) == 1);
    CHECK(static_cast<uint8_t>(flight::FlightState::Airborne) == 2);

    for (uint8_t state : {uint8_t(0), uint8_t(1), uint8_t(2)}) {
        model::Ownship own = flying();
        own.flight_state = state;
        protocol::AdslPacket packet{};
        protocol::from_own(packet, own, 0x123456, 6, 4);
        CHECK(packet.flight_state == state);
        CHECK(packet.flight_state != 3);
    }
}

TEST_CASE("ADS-L.4.SRD860.G.1.3: the aircraft category is five bits, and a glider is 4") {
    for (uint8_t category = 0; category < 32; category++) {
        model::Ownship own = flying();
        protocol::AdslPacket packet{};
        protocol::from_own(packet, own, 0x123456, 6, category);
        CHECK(packet.aircraft_category == category);
        CHECK(field(payload_of(packet), 8, 5) == category);
    }
    protocol::AdslPacket glider{};
    protocol::from_own(glider, flying(), 0x123456, 6, 4);
    CHECK(glider.aircraft_category == 4);

    // Every category a pilot can pick here is one issue 1 names too, which is what E.1.1 rests on.
    const int issue1_named_categories = 14;
    CHECK(go::kNamedAircraftTypes <= issue1_named_categories);
}

// No pilot here can declare an emergency, so what goes out is the code for "no emergency".
TEST_CASE("ADS-L.4.SRD860.G.1.4: the emergency status transmitted is 1, no emergency") {
    protocol::AdslPacket packet = traffic_packet();
    CHECK(packet.emergency == 1);
    CHECK(field(payload_of(packet), 13, 3) == 1u);

    model::AircraftObservation observation{};
    REQUIRE(protocol::to_observation(packet, events::Stamp{}, -80, model::Source::AdslDirect,
                                     observation));
    CHECK(observation.emergency == 1);
}

TEST_CASE("ADS-L.4.SRD860.G.1.5: latitude counts 1/93206 of a degree and longitude 1/46603") {
    protocol::AdslPacket packet{};
    packet.reset();
    packet.set_latitude_1e7(481234500);
    packet.set_longitude_1e7(81234500);
    const int32_t latitude_raw = signed_field(payload_of(packet), 16, 24);
    const int32_t longitude_raw = signed_field(payload_of(packet), 40, 24);
    CHECK(std::abs(latitude_raw - static_cast<int32_t>(48.12345 * 93206)) <= 1);
    CHECK(std::abs(longitude_raw - static_cast<int32_t>(8.12345 * 46603)) <= 1);

    // North and East positive, so the southern and western halves of the globe come back negative.
    packet.set_latitude_1e7(-481234500);
    packet.set_longitude_1e7(-81234500);
    CHECK(signed_field(payload_of(packet), 16, 24) < 0);
    CHECK(signed_field(payload_of(packet), 40, 24) < 0);
    CHECK(std::abs(packet.latitude_1e7() + 481234500) < 200);
    CHECK(std::abs(packet.longitude_1e7() + 81234500) < 200);
}

TEST_CASE("ADS-L.4.SRD860.G.1.5: 0x800000 means no fix, and no fix is not an aircraft") {
    protocol::AdslPacket packet = traffic_packet();
    REQUIRE(packet.has_position());

    packet.set_position_invalid();
    CHECK(field(payload_of(packet), 16, 24) == 0x800000u);
    CHECK(field(payload_of(packet), 40, 24) == 0x800000u);
    CHECK_FALSE(packet.has_position());

    model::AircraftObservation observation{};
    CHECK_FALSE(protocol::to_observation(packet, events::Stamp{}, -80, model::Source::AdslDirect,
                                         observation));
    CHECK_FALSE(observation.position_valid);
}

// The clause's own algorithm, against the codec the three variable-range fields share.
TEST_CASE("ADS-L.4.SRD860.G.1.6: the exponential encoding is the clause's, over its whole range") {
    for (uint32_t value = 0; value < 960; value++) {
        const uint32_t code = encode_unsigned_variable_range<uint32_t, 6>(value);
        CHECK(code == spec_exponential(value, 6));
    }
    for (uint32_t value = 0; value < 61440; value += 7) {
        const uint32_t code = encode_unsigned_variable_range<uint32_t, 12>(value);
        CHECK(code == spec_exponential(value, 12));
    }
    // A decoded value names a bucket 2^exponent wide, and this decoder answers from inside it.
    for (uint32_t code = 0; code < 256; code++) {
        const uint32_t floor_value = spec_exponential_decode(code, 6);
        const uint32_t width = 1u << (code >> 6);
        const uint32_t decoded = decode_unsigned_variable_range<uint32_t, 6>(code);
        CHECK(decoded >= floor_value);
        CHECK(decoded < floor_value + width);
        CHECK(encode_unsigned_variable_range<uint32_t, 6>(decoded) == code);
    }
    // A signed field spends the freed leading bit on the sign, and a zero value is positive.
    CHECK((encode_signed_variable_range<int16_t, 6>(-80) & 0x100) == 0x100);
    CHECK((encode_signed_variable_range<int16_t, 6>(80) & 0x100) == 0);
    CHECK(encode_signed_variable_range<int16_t, 6>(0) == 0);
    const int16_t back =
        decode_signed_variable_range<int16_t, 6>(encode_signed_variable_range<int16_t, 6>(-80));
    CHECK(back <= -80);
    CHECK(back >= -81);
}

// Every row of the clause's example table, read off the wire.
TEST_CASE("ADS-L.4.SRD860.G.1.7: the altitude encodes the clause's worked examples") {
    protocol::AdslPacket packet{};
    packet.reset();
    packet.set_altitude_m(-320);
    CHECK(altitude_code(packet) == 0x0000u);
    packet.set_altitude_m(-400);
    CHECK(altitude_code(packet) == 0x0000u);
    packet.set_altitude_m(0);
    CHECK(altitude_code(packet) == 0x0140u);
    packet.set_altitude_m(1000);
    CHECK(altitude_code(packet) == 0x0528u);
    packet.set_altitude_m(61104);
    CHECK(altitude_code(packet) == 0x3FFEu);
    packet.set_altitude_m(100000);
    CHECK(altitude_code(packet) == 0x3FFEu);
    packet.set_altitude_invalid();
    CHECK(altitude_code(packet) == 0x3FFFu);
    CHECK_FALSE(packet.has_altitude());
    CHECK(protocol::AdslPacket::kAltitudeOffsetM == 320);
}

// The invalid code decoded as 61116 m, and the range gate threw a 2D neighbour 60 km up.
TEST_CASE("ADS-L.4.SRD860.G.1.7: an altitude marked invalid decodes as no altitude, not 61 km") {
    protocol::AdslPacket packet = traffic_packet();
    packet.set_altitude_invalid();
    model::AircraftObservation observation{};
    REQUIRE(protocol::to_observation(packet, events::Stamp{}, -80, model::Source::AdslDirect,
                                     observation));
    CHECK(observation.position_valid);
    CHECK_FALSE(observation.altitude_valid);
    CHECK(observation.altitude_m == 0);

    REQUIRE(protocol::to_observation(traffic_packet(), events::Stamp{}, -80,
                                     model::Source::AdslDirect, observation));
    CHECK(observation.altitude_valid);
}

// A 2D fix still reports a height, the last one the receiver solved, and it went out as valid.
TEST_CASE("ADS-L.4.SRD860.G.1.7: a 2D fix sends its position, and its altitude as unavailable") {
    model::Ownship own = flying();
    own.vdop_e2 = 0;
    protocol::AdslPacket packet{};
    protocol::from_own(packet, own, 0x123456, 6, 4);
    CHECK(packet.has_position());
    CHECK_FALSE(packet.has_altitude());
    CHECK(packet.has_speed());
}

TEST_CASE("ADS-L.4.SRD860.G.1.8: the ground speed encodes the clause's worked examples") {
    protocol::AdslPacket packet{};
    packet.reset();
    packet.set_speed_q(0);
    CHECK(speed_code(packet) == 0x00u);
    packet.set_speed_q(1);
    CHECK(speed_code(packet) == 0x01u);
    packet.set_speed_q(3);
    CHECK(speed_code(packet) == 0x03u);
    packet.set_speed_q(120 * 4);
    CHECK(speed_code(packet) == 0xC4u);
    packet.set_speed_q(236 * 4);
    CHECK(speed_code(packet) == 0xFEu);
    packet.set_speed_q(400 * 4);
    CHECK(speed_code(packet) == 0xFEu);
    packet.set_speed_invalid();
    CHECK(speed_code(packet) == 0xFFu);
    CHECK_FALSE(packet.has_speed());
}

TEST_CASE("ADS-L.4.SRD860.G.1.9: the vertical rate encodes the clause's worked examples") {
    protocol::AdslPacket packet{};
    packet.reset();
    packet.set_climb_e8(0);
    CHECK(climb_code(packet) == 0x000u);
    packet.set_climb_e8(1);
    CHECK(climb_code(packet) == 0x001u);
    packet.set_climb_e8(-1);
    CHECK(climb_code(packet) == 0x101u);
    packet.set_climb_e8(80);
    CHECK(climb_code(packet) == 0x048u);
    packet.set_climb_e8(-80);
    CHECK(climb_code(packet) == 0x148u);
    packet.set_climb_e8(119 * 8);
    CHECK(climb_code(packet) == 0x0FFu);
    packet.set_climb_e8(-118 * 8);
    CHECK(climb_code(packet) == 0x1FEu);
    packet.set_climb_e8(-32000);
    CHECK(climb_code(packet) == 0x1FEu);
    packet.set_climb_invalid();
    CHECK(climb_code(packet) == 0x1FFu);
    CHECK_FALSE(packet.has_climb());
}

TEST_CASE("ADS-L.4.SRD860.G.1.10: the ground track is 512 steps of 0.703125 degrees") {
    protocol::AdslPacket packet{};
    packet.reset();
    for (uint16_t code :
         {uint16_t(0), uint16_t(128), uint16_t(256), uint16_t(384), uint16_t(511)}) {
        packet.set_track_c9(code);
        CHECK(packet.track_c9() == code);
        CHECK(field(payload_of(packet), 95, 9) == code);
    }
    // 512 steps to the turn: a quarter of it is 90 degrees, clockwise from true north.
    CHECK(128 * 360 / 512 == 90);
}

// The clause calls the track invalid when the speed is, so nothing may fly a target along it.
TEST_CASE("ADS-L.4.SRD860.G.1.10: a target with no ground speed is never flown along its track") {
    protocol::AdslPacket packet = traffic_packet();
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
    protocol::AdslPacket parked = traffic_packet();
    parked.set_speed_q(0);
    parked.set_track_c9(256);
    model::AircraftObservation still{};
    REQUIRE(
        protocol::to_observation(parked, events::Stamp{}, -80, model::Source::AdslDirect, still));
    const flight::Prediction stays = flight::extrapolate(still, 1000);
    CHECK(stays.latitude_1e7 == still.latitude_1e7);
    CHECK(stays.longitude_1e7 == still.longitude_1e7);
}

TEST_CASE("ADS-L.4.SRD860.G.1.16: Traffic goes out at 1 Hz airborne and 0.1 Hz on the ground") {
    CHECK(timing::Transmitter::period_s(/*airborne=*/true) == 1u);
    CHECK(timing::Transmitter::period_s(/*airborne=*/false) == 10u);
    CHECK(flight::report_period_s(flight::FlightState::Airborne) == 1u);
    CHECK(flight::report_period_s(flight::FlightState::Ground) == 10u);
}

// An emitter that does not say which state it is in is heard at the airborne rate or not at all.
TEST_CASE("ADS-L.4.SRD860.G.1.16: a target of unknown state is held to the airborne interval") {
    CHECK(flight::report_period_s(flight::FlightState::Unknown) == 1u);
}

// A fix older than half a second is replaced, not transmitted: the clause's own deadline.
TEST_CASE("ADS-L.4.SRD860.G.1.16: no burst carries navigation data older than 500 ms") {
    CHECK(timing::Transmitter::kFixMaxAgeMs == 500);

    timing::ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;
    const timing::SlotPlan plan = timing::Scheduler::plan(timing::kSlot0StartMs, clock);

    timing::Transmitter tx;
    tx.configure(0x123456);
    CHECK(tx.attempt(plan, 100, 1000, true, 499).go);
    CHECK_FALSE(tx.attempt(plan, 100, 1000, true, 501).go);
}
