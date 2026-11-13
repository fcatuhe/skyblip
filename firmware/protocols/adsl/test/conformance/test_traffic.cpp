// ADS-L 4 SRD-860 issue 2 G.1: every Traffic payload field, at the offset and in the code printed.
#include <cstdint>
#include <cstdlib>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/adsl/flight_state.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/test/conformance/reference.h"
#include "protocols/adsl/transmit.h"
#include "protocols/common/exponential.h"

using namespace skyblip;
using namespace skyblip::adsl::conformance;

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

uint32_t altitude_code(const adsl::Packet& packet) { return field(payload_of(packet), 72, 14); }

uint32_t speed_code(const adsl::Packet& packet) { return field(payload_of(packet), 64, 8); }

uint32_t climb_code(const adsl::Packet& packet) { return field(payload_of(packet), 86, 9); }
}  // namespace

// The whole layout in one case: move a field by a bit and this is what catches it.
TEST_CASE("ADS-L.4.SRD860.G.1: every field sits at the offset and width the table gives it") {
    adsl::Packet packet{};
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
    CHECK(signed_field(payload, 16, 24) == packet.latitude_steps());
    CHECK(signed_field(payload, 40, 24) == packet.longitude_steps());
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

// Quarter seconds since the hour, modulo 60: a 15-second cycle, never the four unused codes.
TEST_CASE("ADS-L.4.SRD860.G.1.1: the timestamp is a quarter second inside a 15-second cycle") {
    CHECK(adsl::kTimestampCycleS == 15u);
    CHECK(adsl::kTimestampQuarterMs == 250u);
    for (uint32_t utc_s = 1785628800; utc_s < 1785628860; utc_s++) {
        for (int32_t lead = -1000; lead <= 1000; lead += 125) {
            const uint8_t code = adsl::timestamp_code(utc_s, lead);
            CHECK(code < 60);
        }
    }
    CHECK(adsl::timestamp_code(1785628800, 0) == static_cast<uint8_t>((1785628800u % 15u) * 4u));
    CHECK(adsl::timestamp_code(15, 0) == 0);
    CHECK(adsl::timestamp_code(15, 250) == 1);
    CHECK(adsl::timestamp_code(15, 750) == 3);
    CHECK(adsl::timestamp_code(15, -250) == 59);
}

// No pilot here can declare an emergency, so what goes out is the code for "no emergency".
TEST_CASE("ADS-L.4.SRD860.G.1.4: the emergency status transmitted is 1, no emergency") {
    adsl::Packet packet = traffic_packet();
    CHECK(packet.emergency == 1);
    CHECK(field(payload_of(packet), 13, 3) == 1u);

    adsl::Traffic observation;
    REQUIRE(heard(packet, observation));
    CHECK(observation.emergency == 1);
}

TEST_CASE("ADS-L.4.SRD860.G.1.5: latitude counts 1/93206 of a degree and longitude 1/46603") {
    adsl::Packet packet{};
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
    adsl::Packet packet = traffic_packet();
    REQUIRE(packet.has_position());

    packet.set_position_invalid();
    CHECK(field(payload_of(packet), 16, 24) == 0x800000u);
    CHECK(field(payload_of(packet), 40, 24) == 0x800000u);
    CHECK_FALSE(packet.has_position());

    adsl::Traffic observation;
    CHECK_FALSE(heard(packet, observation));
    CHECK_FALSE(observation.position_valid);
}

// The clause's own algorithm, against the codec the three variable-range fields share.
TEST_CASE("ADS-L.4.SRD860.G.1.6: the exponential encoding is the clause's, over its whole range") {
    const wire::Exponential six{6, 2, false};
    const wire::Exponential twelve{12, 2, false};
    const wire::Exponential signed_six{6, 2, true};
    for (uint32_t value = 0; value < 960; value++) {
        const uint32_t code = six.encode(static_cast<int32_t>(value));
        CHECK(code == spec_exponential(value, 6));
    }
    for (uint32_t value = 0; value < 61440; value += 7) {
        const uint32_t code = twelve.encode(static_cast<int32_t>(value));
        CHECK(code == spec_exponential(value, 12));
    }
    // A decoded value names a bucket 2^exponent wide, and this decoder answers from inside it.
    for (uint32_t code = 0; code < 256; code++) {
        const uint32_t floor_value = spec_exponential_decode(code, 6);
        const uint32_t width = 1u << (code >> 6);
        const uint32_t decoded = static_cast<uint32_t>(six.decode_middle(code));
        CHECK(decoded >= floor_value);
        CHECK(decoded < floor_value + width);
        CHECK(six.encode(static_cast<int32_t>(decoded)) == code);
        CHECK(static_cast<uint32_t>(six.decode(code)) == floor_value);
    }
    // A signed field spends the freed leading bit on the sign, and a zero value is positive.
    CHECK((signed_six.encode(-80) & 0x100) == 0x100);
    CHECK((signed_six.encode(80) & 0x100) == 0);
    CHECK(signed_six.encode(0) == 0);
    const int32_t back = signed_six.decode_middle(signed_six.encode(-80));
    CHECK(back <= -80);
    CHECK(back >= -81);
}

// Every row of the clause's example table, read off the wire.
TEST_CASE("ADS-L.4.SRD860.G.1.7: the altitude encodes the clause's worked examples") {
    adsl::Packet packet{};
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
    CHECK(adsl::Packet::kAltitudeOffsetM == 320);
}

// The invalid code decoded as 61116 m, and the range gate threw a 2D neighbour 60 km up.
TEST_CASE("ADS-L.4.SRD860.G.1.7: an altitude marked invalid decodes as no altitude, not 61 km") {
    adsl::Packet packet = traffic_packet();
    packet.set_altitude_invalid();
    adsl::Traffic observation;
    REQUIRE(heard(packet, observation));
    CHECK(observation.position_valid);
    CHECK_FALSE(observation.altitude_valid);
    CHECK(observation.altitude_m == 0);

    REQUIRE(heard(traffic_packet(), observation));
    CHECK(observation.altitude_valid);
}

TEST_CASE("ADS-L.4.SRD860.G.1.8: the ground speed encodes the clause's worked examples") {
    adsl::Packet packet{};
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
    adsl::Packet packet{};
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
    adsl::Packet packet{};
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

// The clause gives media access priority over these rates, so an integrator may space them
// unevenly.
TEST_CASE("ADS-L.4.SRD860.G.1.16: Traffic goes out at 1 Hz airborne and 0.1 Hz on the ground") {
    CHECK(adsl::period_s(/*airborne=*/true) == 1u);
    CHECK(adsl::period_s(/*airborne=*/false) == 10u);
    CHECK(adsl::report_period_s(adsl::FlightState::Airborne) == 1u);
    CHECK(adsl::report_period_s(adsl::FlightState::Ground) == 10u);
}

// An emitter that does not say which state it is in is heard at the airborne rate or not at all.
TEST_CASE("ADS-L.4.SRD860.G.1.16: a target of unknown state is held to the airborne interval") {
    CHECK(adsl::report_period_s(adsl::FlightState::Unknown) == 1u);
}
