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
#include "core/util/varint.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/menu.h"

using namespace skyblip;

namespace {
const uint8_t* payload_of(const protocol::AdslPacket& p) { return p.Byte + 5; }

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
    const uint32_t thres = 1u << base_bits;
    int exponent = 0;
    for (int e = 3; e >= 0; e--) {
        const uint32_t floor_value = (thres << e) - thres;
        if (value >= floor_value) {
            exponent = e;
            break;
        }
    }
    const uint32_t base = (value - ((thres << exponent) - thres)) >> exponent;
    return (static_cast<uint32_t>(exponent) << base_bits) | base;
}

uint32_t spec_exponential_decode(uint32_t code, int base_bits) {
    const uint32_t thres = 1u << base_bits;
    const uint32_t exponent = code >> base_bits;
    const uint32_t base = code & (thres - 1);
    return ((thres + base) << exponent) - thres;
}

model::OwnState flying() {
    model::OwnState own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.climb_valid = true;
    own.lat_1e7 = 481234500;
    own.lon_1e7 = 81234500;
    own.alt_mm = 1000000;
    own.speed_mm_s = 120 * 1000;
    own.climb_mm_s = 10000;
    own.track_cdeg = 9000;
    own.hdop_e2 = 90;
    own.vdop_e2 = 150;
    own.flight_state = 2;
    return own;
}

protocol::AdslPacket traffic_packet() {
    protocol::AdslPacket p{};
    protocol::from_own(p, flying(), 0x123456, 6, 4);
    return p;
}

uint32_t alt_code(const protocol::AdslPacket& p) { return field(payload_of(p), 72, 14); }
uint32_t speed_code(const protocol::AdslPacket& p) { return field(payload_of(p), 64, 8); }
uint32_t climb_code(const protocol::AdslPacket& p) { return field(payload_of(p), 86, 9); }
}  // namespace

// The whole layout in one case: move a field by a bit and this is what catches it.
TEST_CASE("ADS-L.4.SRD860.G.1: every field sits at the offset and width the table gives it") {
    protocol::AdslPacket p{};
    p.init();
    p.TimeStamp = 37;
    p.FlightState = 2;
    p.AcftCat = 9;
    p.Emergency = 5;
    p.set_lat_1e7(481234500);
    p.set_lon_1e7(81234500);
    p.set_speed_q(120 * 4);
    p.set_alt_m(1000);
    p.set_climb_e8(80);
    p.set_track_c9(400);
    p.SourceIntegrity = 2;
    p.DesignAssurance = 1;
    p.NavigIntegrity = 11;
    p.HorizAccuracy = 6;
    p.VertAccuracy = 3;
    p.VelAccuracy = 2;
    p.Reserved = 0;

    const uint8_t* payload = payload_of(p);
    CHECK(field(payload, 0, 6) == 37u);
    CHECK(field(payload, 6, 2) == 2u);
    CHECK(field(payload, 8, 5) == 9u);
    CHECK(field(payload, 13, 3) == 5u);
    CHECK(signed_field(payload, 16, 24) == p.lat_cordic() >> 7);
    CHECK(signed_field(payload, 40, 24) == p.lon_cordic() >> 8);
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
    model::OwnState own = flying();
    own.fix_valid = false;
    own.climb_valid = false;
    protocol::AdslPacket p{};
    protocol::from_own(p, own, 0x123456, 6, 4);
    CHECK_FALSE(p.has_position());
    CHECK(p.alt_invalid());
    CHECK_FALSE(p.has_speed());
    CHECK_FALSE(p.has_climb());
}

// Quarter seconds since the hour, modulo 60: a 15-second cycle, never the four unused codes.
TEST_CASE("ADS-L.4.SRD860.G.1.1: the timestamp is a quarter second inside a 15-second cycle") {
    CHECK(protocol::kTimeStampCycleS == 15u);
    CHECK(protocol::kTimeStampQuarterMs == 250u);
    for (uint32_t utc = 1785628800; utc < 1785628860; utc++) {
        for (int32_t lead = -1000; lead <= 1000; lead += 125) {
            const uint8_t code = protocol::timestamp_code(utc, lead);
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
    model::OwnState own = flying();
    own.utc = 1785628800;
    const int32_t into_utc_ms = 613;
    protocol::AdslPacket p{};
    protocol::from_own(p, own, 0x123456, 6, 4,
                       protocol::BurstInstant{own.utc, into_utc_ms, into_utc_ms});
    const int32_t named_ms = static_cast<int32_t>(p.TimeStamp % 4u) *
                             static_cast<int32_t>(protocol::kTimeStampQuarterMs);
    CHECK(named_ms == 500);

    protocol::AdslPacket at_named{};
    at_named.init();
    const flight::Prediction there = flight::extrapolate(own, named_ms);
    at_named.set_lat_1e7(there.lat_1e7);
    at_named.set_lon_1e7(there.lon_1e7);
    CHECK(p.lat_1e7() == at_named.lat_1e7());
    CHECK(p.lon_1e7() == at_named.lon_1e7());
}

// It dates the navigation solution, and the position is carried to it, so the pair is one moment.
TEST_CASE("ADS-L.4.SRD860.G.1.1: the timestamp and the position name the same instant") {
    model::OwnState own = flying();
    own.utc = 1785628800;
    protocol::BurstInstant at{};
    at.utc = own.utc;
    at.into_utc_ms = 500;
    at.since_fix_ms = 500;

    protocol::AdslPacket p{};
    protocol::from_own(p, own, 0x123456, 6, 4, at);
    CHECK(p.TimeStamp == protocol::timestamp_code(own.utc, 500));

    protocol::AdslPacket at_fix{};
    protocol::from_own(at_fix, own, 0x123456, 6, 4, protocol::BurstInstant{own.utc, 0, 0});
    CHECK(at_fix.TimeStamp == protocol::timestamp_code(own.utc, 0));
    // Half a second due east at 120 m/s: the transmitted position moved with the timestamp.
    CHECK(p.lon_1e7() > at_fix.lon_1e7());
}

TEST_CASE("ADS-L.4.SRD860.G.1.2: flight state is 0 unknown, 1 on ground, 2 airborne, never 3") {
    CHECK(static_cast<uint8_t>(flight::FlightState::Unknown) == 0);
    CHECK(static_cast<uint8_t>(flight::FlightState::OnGround) == 1);
    CHECK(static_cast<uint8_t>(flight::FlightState::Airborne) == 2);

    for (uint8_t state : {uint8_t(0), uint8_t(1), uint8_t(2)}) {
        model::OwnState own = flying();
        own.flight_state = state;
        protocol::AdslPacket p{};
        protocol::from_own(p, own, 0x123456, 6, 4);
        CHECK(p.FlightState == state);
        CHECK(p.FlightState != 3);
    }
}

TEST_CASE("ADS-L.4.SRD860.G.1.3: the aircraft category is five bits, and a glider is 4") {
    for (uint8_t cat = 0; cat < 32; cat++) {
        model::OwnState own = flying();
        protocol::AdslPacket p{};
        protocol::from_own(p, own, 0x123456, 6, cat);
        CHECK(p.AcftCat == cat);
        CHECK(field(payload_of(p), 8, 5) == cat);
    }
    protocol::AdslPacket glider{};
    protocol::from_own(glider, flying(), 0x123456, 6, 4);
    CHECK(glider.AcftCat == 4);

    // Every category a pilot can pick here is one issue 1 names too, which is what E.1.1 rests on.
    const int issue1_named_categories = 14;
    CHECK(go::kNamedAircraftTypes <= issue1_named_categories);
}

// No pilot here can declare an emergency, so what goes out is the code for "no emergency".
TEST_CASE("ADS-L.4.SRD860.G.1.4: the emergency status transmitted is 1, no emergency") {
    protocol::AdslPacket p = traffic_packet();
    CHECK(p.Emergency == 1);
    CHECK(field(payload_of(p), 13, 3) == 1u);

    model::AircraftObs obs{};
    REQUIRE(protocol::to_obs(p, events::Stamp{}, -80, model::Source::AdslDirect, obs));
    CHECK(obs.emergency == 1);
}

TEST_CASE("ADS-L.4.SRD860.G.1.5: latitude counts 1/93206 of a degree and longitude 1/46603") {
    protocol::AdslPacket p{};
    p.init();
    p.set_lat_1e7(481234500);
    p.set_lon_1e7(81234500);
    const int32_t lat_raw = signed_field(payload_of(p), 16, 24);
    const int32_t lon_raw = signed_field(payload_of(p), 40, 24);
    CHECK(std::abs(lat_raw - static_cast<int32_t>(48.12345 * 93206)) <= 1);
    CHECK(std::abs(lon_raw - static_cast<int32_t>(8.12345 * 46603)) <= 1);

    // North and East positive, so the southern and western halves of the globe come back negative.
    p.set_lat_1e7(-481234500);
    p.set_lon_1e7(-81234500);
    CHECK(signed_field(payload_of(p), 16, 24) < 0);
    CHECK(signed_field(payload_of(p), 40, 24) < 0);
    CHECK(std::abs(p.lat_1e7() + 481234500) < 200);
    CHECK(std::abs(p.lon_1e7() + 81234500) < 200);
}

TEST_CASE("ADS-L.4.SRD860.G.1.5: 0x800000 means no fix, and no fix is not an aircraft") {
    protocol::AdslPacket p = traffic_packet();
    REQUIRE(p.has_position());

    p.set_position_invalid();
    CHECK(field(payload_of(p), 16, 24) == 0x800000u);
    CHECK(field(payload_of(p), 40, 24) == 0x800000u);
    CHECK_FALSE(p.has_position());

    model::AircraftObs obs{};
    CHECK_FALSE(protocol::to_obs(p, events::Stamp{}, -80, model::Source::AdslDirect, obs));
    CHECK_FALSE(obs.position_valid);
}

// The clause's own algorithm, against the codec the three variable-range fields share.
TEST_CASE("ADS-L.4.SRD860.G.1.6: the exponential encoding is the clause's, over its whole range") {
    for (uint32_t value = 0; value < 960; value++) {
        const uint32_t code = uns_vr_encode<uint32_t, 6>(value);
        CHECK(code == spec_exponential(value, 6));
    }
    for (uint32_t value = 0; value < 61440; value += 7) {
        const uint32_t code = uns_vr_encode<uint32_t, 12>(value);
        CHECK(code == spec_exponential(value, 12));
    }
    // A decoded value names a bucket 2^exponent wide, and this decoder answers from inside it.
    for (uint32_t code = 0; code < 256; code++) {
        const uint32_t floor_value = spec_exponential_decode(code, 6);
        const uint32_t width = 1u << (code >> 6);
        const uint32_t decoded = uns_vr_decode<uint32_t, 6>(code);
        CHECK(decoded >= floor_value);
        CHECK(decoded < floor_value + width);
        CHECK(uns_vr_encode<uint32_t, 6>(decoded) == code);
    }
    // A signed field spends the freed leading bit on the sign, and a zero value is positive.
    CHECK((sign_vr_encode<int16_t, 6>(-80) & 0x100) == 0x100);
    CHECK((sign_vr_encode<int16_t, 6>(80) & 0x100) == 0);
    CHECK(sign_vr_encode<int16_t, 6>(0) == 0);
    const int16_t back = sign_vr_decode<int16_t, 6>(sign_vr_encode<int16_t, 6>(-80));
    CHECK(back <= -80);
    CHECK(back >= -81);
}

// Every row of the clause's example table, read off the wire.
TEST_CASE("ADS-L.4.SRD860.G.1.7: the altitude encodes the clause's worked examples") {
    protocol::AdslPacket p{};
    p.init();
    p.set_alt_m(-320);
    CHECK(alt_code(p) == 0x0000u);
    p.set_alt_m(-400);
    CHECK(alt_code(p) == 0x0000u);
    p.set_alt_m(0);
    CHECK(alt_code(p) == 0x0140u);
    p.set_alt_m(1000);
    CHECK(alt_code(p) == 0x0528u);
    p.set_alt_m(61104);
    CHECK(alt_code(p) == 0x3FFEu);
    p.set_alt_m(100000);
    CHECK(alt_code(p) == 0x3FFEu);
    p.set_alt_invalid();
    CHECK(alt_code(p) == 0x3FFFu);
    CHECK(p.alt_invalid());
    CHECK(protocol::AdslPacket::kAltOffsetM == 320);
}

// The invalid code decoded as 61116 m, and the range gate threw a 2D neighbour 60 km up.
TEST_CASE("ADS-L.4.SRD860.G.1.7: an altitude marked invalid decodes as no altitude, not 61 km") {
    protocol::AdslPacket p = traffic_packet();
    p.set_alt_invalid();
    model::AircraftObs obs{};
    REQUIRE(protocol::to_obs(p, events::Stamp{}, -80, model::Source::AdslDirect, obs));
    CHECK(obs.position_valid);
    CHECK_FALSE(obs.alt_valid);
    CHECK(obs.alt_m == 0);

    REQUIRE(
        protocol::to_obs(traffic_packet(), events::Stamp{}, -80, model::Source::AdslDirect, obs));
    CHECK(obs.alt_valid);
}

// A 2D fix still reports a height, the last one the receiver solved, and it went out as valid.
TEST_CASE("ADS-L.4.SRD860.G.1.7: a 2D fix sends its position, and its altitude as unavailable") {
    model::OwnState own = flying();
    own.vdop_e2 = 0;
    protocol::AdslPacket p{};
    protocol::from_own(p, own, 0x123456, 6, 4);
    CHECK(p.has_position());
    CHECK(p.alt_invalid());
    CHECK(p.has_speed());
}

TEST_CASE("ADS-L.4.SRD860.G.1.8: the ground speed encodes the clause's worked examples") {
    protocol::AdslPacket p{};
    p.init();
    p.set_speed_q(0);
    CHECK(speed_code(p) == 0x00u);
    p.set_speed_q(1);
    CHECK(speed_code(p) == 0x01u);
    p.set_speed_q(3);
    CHECK(speed_code(p) == 0x03u);
    p.set_speed_q(120 * 4);
    CHECK(speed_code(p) == 0xC4u);
    p.set_speed_q(236 * 4);
    CHECK(speed_code(p) == 0xFEu);
    p.set_speed_q(400 * 4);
    CHECK(speed_code(p) == 0xFEu);
    p.set_speed_invalid();
    CHECK(speed_code(p) == 0xFFu);
    CHECK_FALSE(p.has_speed());
}

TEST_CASE("ADS-L.4.SRD860.G.1.9: the vertical rate encodes the clause's worked examples") {
    protocol::AdslPacket p{};
    p.init();
    p.set_climb_e8(0);
    CHECK(climb_code(p) == 0x000u);
    p.set_climb_e8(1);
    CHECK(climb_code(p) == 0x001u);
    p.set_climb_e8(-1);
    CHECK(climb_code(p) == 0x101u);
    p.set_climb_e8(80);
    CHECK(climb_code(p) == 0x048u);
    p.set_climb_e8(-80);
    CHECK(climb_code(p) == 0x148u);
    p.set_climb_e8(119 * 8);
    CHECK(climb_code(p) == 0x0FFu);
    p.set_climb_e8(-118 * 8);
    CHECK(climb_code(p) == 0x1FEu);
    p.set_climb_e8(-32000);
    CHECK(climb_code(p) == 0x1FEu);
    p.set_climb_invalid();
    CHECK(climb_code(p) == 0x1FFu);
    CHECK_FALSE(p.has_climb());
}

TEST_CASE("ADS-L.4.SRD860.G.1.10: the ground track is 512 steps of 0.703125 degrees") {
    protocol::AdslPacket p{};
    p.init();
    for (uint16_t code :
         {uint16_t(0), uint16_t(128), uint16_t(256), uint16_t(384), uint16_t(511)}) {
        p.set_track_c9(code);
        CHECK(p.track_c9() == code);
        CHECK(field(payload_of(p), 95, 9) == code);
    }
    // 512 steps to the turn: a quarter of it is 90 degrees, clockwise from true north.
    CHECK(128 * 360 / 512 == 90);
}

// The clause calls the track invalid when the speed is, so nothing may fly a target along it.
TEST_CASE("ADS-L.4.SRD860.G.1.10: a target with no ground speed is never flown along its track") {
    protocol::AdslPacket p = traffic_packet();
    p.set_speed_invalid();
    p.set_track_c9(256);

    model::AircraftObs obs{};
    REQUIRE(protocol::to_obs(p, events::Stamp{}, -80, model::Source::AdslDirect, obs));
    CHECK_FALSE(obs.speed_valid);
    CHECK(obs.speed_q == 0);

    const flight::Prediction carried = flight::extrapolate(obs, 1000);
    CHECK_FALSE(carried.valid);
    CHECK(carried.lat_1e7 == obs.lat_1e7);
    CHECK(carried.lon_1e7 == obs.lon_1e7);

    // A ground speed of zero is the clause's other invalid track, and it moves no further.
    protocol::AdslPacket parked = traffic_packet();
    parked.set_speed_q(0);
    parked.set_track_c9(256);
    model::AircraftObs still{};
    REQUIRE(protocol::to_obs(parked, events::Stamp{}, -80, model::Source::AdslDirect, still));
    const flight::Prediction stays = flight::extrapolate(still, 1000);
    CHECK(stays.lat_1e7 == still.lat_1e7);
    CHECK(stays.lon_1e7 == still.lon_1e7);
}

TEST_CASE("ADS-L.4.SRD860.G.1.16: Traffic goes out at 1 Hz airborne and 0.1 Hz on the ground") {
    CHECK(timing::Transmitter::period_s(/*airborne=*/true) == 1u);
    CHECK(timing::Transmitter::period_s(/*airborne=*/false) == 10u);
}

// A fix older than half a second is replaced, not transmitted: the clause's own deadline.
TEST_CASE("ADS-L.4.SRD860.G.1.16: no burst carries navigation data older than 500 ms") {
    CHECK(timing::Transmitter::kFixLagMaxMs == 500);

    timing::ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;
    const timing::SlotPlan plan = timing::Scheduler::plan(timing::kSlot0Start, clock);

    timing::Transmitter tx;
    tx.configure(0x123456);
    CHECK(tx.attempt(plan, 100, 1000, true, 499).go);
    CHECK_FALSE(tx.attempt(plan, 100, 1000, true, 501).go);
}
