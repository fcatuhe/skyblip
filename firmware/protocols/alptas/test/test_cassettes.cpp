// Ten frames synthesized over the French Alps, replayed byte for byte beside what each sender said.
#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/alptas/frame.h"

using namespace skyblip;
using namespace skyblip::alptas;
using wire::Status;

namespace {

struct Cassette {
    const char* scene;
    uint32_t heard_utc_s;
    int32_t receiver_latitude_1e7;
    int32_t receiver_longitude_1e7;
    uint8_t frame[kFrameBytes];
    uint8_t address_type;
    uint32_t address;
    uint8_t random;
    uint16_t address_extension;
    uint8_t urgency;
    bool stealth;
    bool no_track;
    uint8_t max_version;
    uint8_t fix_quarters;
    uint8_t aircraft_type;
    uint8_t airborne;
    int32_t latitude_1e7;
    int32_t longitude_1e7;
    int32_t altitude_m;
    int32_t turn_rate_cdps;
    int32_t speed_e1;
    int32_t climb_e1;
    uint16_t track_deg;
    int32_t horizontal_accuracy_dm;
    int32_t vertical_accuracy_qm;
    int32_t velocity_accuracy_dmps;
    uint8_t sil;
    uint8_t sda;
    uint8_t nic;
};

const Cassette kCassettes[] = {
    {"a competition glider circling right in stealth above Saint-Crépin",
     1781990417u,
     447030000,
     66000000,
     {0x21, 0x3a, 0xdf, 0x22, 0x00, 0x00, 0x40, 0x33, 0xbe, 0x25, 0x39, 0x6a, 0x7e,
      0x62, 0xa4, 0x98, 0xb1, 0xc0, 0xbb, 0xce, 0x37, 0x1d, 0xe5, 0x24, 0x84, 0xce},
     2,
     0x00df3a21u,
     0x00,
     0x0000,
     0,
     true,
     false,
     3,
     4,
     1,
     3,
     446749992,
     65300033,
     2846,
     1500,
     312,
     31,
     243,
     24,
     12,
     4,
     0,
     0,
     0},
    {"a paraglider in privacy mode above Annecy, sinking, asking not to be tracked",
     1781990422u,
     459000000,
     61300000,
     {0x59, 0xe2, 0xc4, 0x02, 0x02, 0xa1, 0x80, 0xf3, 0x74, 0x4e, 0xdf, 0x40, 0xc3,
      0xee, 0x36, 0x1d, 0x1b, 0x2f, 0xda, 0x28, 0xd2, 0x36, 0xa3, 0xbb, 0xe4, 0xe9},
     0,
     0xa1c4e259u,
     0x02,
     0x0000,
     0,
     false,
     true,
     15,
     24,
     7,
     2,
     458099980,
     62300008,
     1810,
     -900,
     98,
     -18,
     87,
     40,
     16,
     6,
     0,
     0,
     0},
    {"a tow plane with a certified source and an ICAO address above Grenoble-Le Versoud",
     1781990433u,
     453100000,
     58900000,
     {0x17, 0x5a, 0x3e, 0x12, 0x00, 0x00, 0x00, 0x33, 0x1a, 0x6b, 0x4c, 0xac, 0x10,
      0x7e, 0x8a, 0xc4, 0x40, 0x84, 0xbd, 0x4d, 0xf1, 0xfe, 0x82, 0xdf, 0x8e, 0xd0},
     1,
     0x003e5a17u,
     0x00,
     0x0000,
     0,
     false,
     false,
     3,
     4,
     2,
     2,
     452499996,
     58800030,
     1220,
     0,
     394,
     42,
     11,
     16,
     8,
     3,
     3,
     2,
     8},
    {"an eVTOL with an extended address and a high urgency over Albertville",
     1781990441u,
     456800000,
     64000000,
     {0x92, 0x0b, 0x6c, 0x72, 0x44, 0x1d, 0x00, 0x33, 0x41, 0x93, 0x4a, 0x75, 0xcc,
      0x31, 0x1d, 0xf1, 0x8a, 0x40, 0x63, 0xae, 0x96, 0x8f, 0x01, 0x6b, 0xaf, 0x6a},
     3,
     0x006c0b92u,
     0x00,
     0x1d44,
     1,
     false,
     false,
     3,
     36,
     16,
     2,
     456650012,
     63900036,
     652,
     0,
     214,
     -61,
     179,
     32,
     20,
     14,
     2,
     1,
     9},
    {"an open-category UAS along the cliffs of the Vercors",
     1781990458u,
     450700000,
     55500000,
     {0x08, 0x2f, 0x4d, 0x22, 0x00, 0x00, 0x00, 0x33, 0xc7, 0x5a, 0xf8, 0xce, 0x44,
      0x08, 0x10, 0x34, 0x32, 0x11, 0xb6, 0xde, 0x60, 0x76, 0xed, 0x48, 0x0d, 0xc0},
     2,
     0x004d2f08u,
     0x00,
     0x0000,
     0,
     false,
     false,
     3,
     40,
     17,
     2,
     449600008,
     55399992,
     1402,
     0,
     76,
     0,
     300,
     20,
     12,
     6,
     1,
     1,
     10},
    {"a helicopter in distress turning left above Saint-Gervais, below Mont Blanc",
     1781990466u,
     459400000,
     66300000,
     {0x3d, 0x0c, 0xe8, 0xd2, 0x00, 0x00, 0x00, 0x33, 0x12, 0xb2, 0xe5, 0x36, 0xc5,
      0xf5, 0x85, 0xb2, 0xf9, 0x19, 0x1a, 0xbb, 0x83, 0xa4, 0x2c, 0x3b, 0x64, 0x03},
     1,
     0x00e80c3du,
     0x00,
     0x0000,
     3,
     false,
     false,
     3,
     8,
     3,
     2,
     458999996,
     67100018,
     1780,
     -2000,
     466,
     -52,
     62,
     64,
     24,
     10,
     2,
     2,
     7},
    {"a certified UAS on a Pan-Pan above Saint-Jean-de-Maurienne",
     1781990477u,
     452700000,
     63500000,
     {0xe4, 0x11, 0x7b, 0xa2, 0x00, 0x00, 0x00, 0x33, 0x16, 0x33, 0x07, 0x71, 0xcb,
      0xdf, 0xcc, 0x34, 0x52, 0x42, 0x2d, 0x8e, 0x13, 0x07, 0xd2, 0x4a, 0xda, 0x5b},
     2,
     0x007b11e4u,
     0x00,
     0x0000,
     2,
     false,
     false,
     3,
     52,
     19,
     2,
     452759996,
     63449968,
     1040,
     600,
     180,
     12,
     211,
     24,
     16,
     6,
     3,
     3,
     11},
    {"a balloon drifting above Gap in stealth, asking not to be tracked",
     1781990486u,
     444550000,
     60370000,
     {0x07, 0x1f, 0xc8, 0x12, 0x00, 0x00, 0xc0, 0x33, 0xd7, 0xba, 0x20, 0xd3, 0x63,
      0x64, 0x74, 0x8b, 0xae, 0x29, 0x18, 0x4a, 0x7c, 0x5a, 0xda, 0xed, 0x16, 0x79},
     1,
     0x00c81f07u,
     0x00,
     0x0000,
     0,
     true,
     true,
     3,
     24,
     11,
     2,
     445600012,
     60800021,
     3402,
     0,
     52,
     8,
     95,
     48,
     20,
     4,
     0,
     0,
     0},
    {"a gyrocopter standing on the strip at Barcelonnette",
     1781990491u,
     443880000,
     66200000,
     {0xc3, 0x77, 0x0a, 0x22, 0x00, 0x00, 0x00, 0x33, 0x79, 0x1e, 0x46, 0x03, 0xe0,
      0x47, 0xb9, 0xbb, 0x09, 0x7b, 0xaa, 0xbc, 0xf9, 0x1e, 0x03, 0x3c, 0xe8, 0x98},
     2,
     0x000a77c3u,
     0x00,
     0x0000,
     0,
     false,
     false,
     3,
     44,
     10,
     1,
     443870024,
     66099967,
     1132,
     0,
     0,
     0,
     0,
     40,
     16,
     4,
     0,
     0,
     0},
    {"a business jet turning left high over the Écrins",
     1781990503u,
     448500000,
     64900000,
     {0x34, 0x19, 0x4b, 0x12, 0x00, 0x00, 0x00, 0x33, 0x6d, 0x05, 0x9e, 0x79, 0x8a,
      0x81, 0x09, 0xed, 0xe4, 0x27, 0xd4, 0xbc, 0x0e, 0x93, 0x04, 0x1c, 0x16, 0xe5},
     1,
     0x004b1934u,
     0x00,
     0x0000,
     0,
     false,
     false,
     3,
     28,
     9,
     2,
     449200024,
     63600009,
     10848,
     -760,
     2264,
     160,
     288,
     32,
     16,
     8,
     2,
     2,
     8}};

Position replayed(const Cassette& cassette) {
    Position position;
    REQUIRE(decode(cassette.frame, cassette.heard_utc_s, cassette.receiver_latitude_1e7,
                   cassette.receiver_longitude_1e7, position) == Status::Ok);
    return position;
}

bool rated(const Cassette& cassette) {
    return cassette.sil != 0 || cassette.sda != 0 || cassette.nic != 0;
}

}

TEST_CASE("alptas: cassette frames replay to the position, motion and altitude their senders sent" *
          doctest::test_suite("main")) {
    for (const Cassette& cassette : kCassettes) {
        CAPTURE(cassette.scene);
        CHECK(crc_ok(cassette.frame));
        const Position position = replayed(cassette);
        CHECK(position.latitude_1e7 == cassette.latitude_1e7);
        CHECK(position.longitude_1e7 == cassette.longitude_1e7);
        CHECK(position.altitude_m == cassette.altitude_m);
        CHECK(position.turn_rate_cdps == cassette.turn_rate_cdps);
        CHECK(position.speed_e1 == cassette.speed_e1);
        CHECK(position.climb_e1 == cassette.climb_e1);
        CHECK(position.track_deg == cassette.track_deg);
        CHECK(int(position.airborne) == int(cassette.airborne));
    }
}

TEST_CASE("alptas: cassette frames name their sender, its type, its urgency and its flags" *
          doctest::test_suite("main")) {
    for (const Cassette& cassette : kCassettes) {
        CAPTURE(cassette.scene);
        const Position position = replayed(cassette);
        CHECK(int(position.address_type) == int(cassette.address_type));
        CHECK(position.address == cassette.address);
        CHECK(address(cassette.frame) == (cassette.address & 0xFFFFFFu));
        CHECK(int(position.random) == int(cassette.random));
        CHECK(position.address_extension == cassette.address_extension);
        CHECK(int(position.aircraft_type) == int(cassette.aircraft_type));
        CHECK(int(position.urgency) == int(cassette.urgency));
        CHECK(position.stealth == cassette.stealth);
        CHECK(position.no_track == cassette.no_track);
        CHECK(no_track(cassette.frame) == cassette.no_track);
        CHECK(int(position.max_version) == int(cassette.max_version));
        CHECK(int(position.fix_quarters) == int(cassette.fix_quarters));
    }
}

TEST_CASE("alptas: cassette frames carry their senders' accuracy and integrity") {
    for (const Cassette& cassette : kCassettes) {
        CAPTURE(cassette.scene);
        const Position position = replayed(cassette);
        CHECK(position.horizontal_accuracy_dm == cassette.horizontal_accuracy_dm);
        CHECK(position.vertical_accuracy_qm == cassette.vertical_accuracy_qm);
        CHECK(position.velocity_accuracy_dmps == cassette.velocity_accuracy_dmps);
        CHECK(int(position.sil) == int(cassette.sil));
        CHECK(int(position.sda) == int(cassette.sda));
        CHECK(int(position.nic) == int(cassette.nic));
    }
}

// An unrated frame checks its key with a zero byte and may be a second off, a rated one may not.
TEST_CASE("alptas: a cassette frame heard a second late is kept only if it is unrated") {
    for (const Cassette& cassette : kCassettes) {
        CAPTURE(cassette.scene);
        Position position;
        const Status late =
            decode(cassette.frame, cassette.heard_utc_s + 1, cassette.receiver_latitude_1e7,
                   cassette.receiver_longitude_1e7, position);
        CHECK((late == Status::Ok) == !rated(cassette));
    }
}
