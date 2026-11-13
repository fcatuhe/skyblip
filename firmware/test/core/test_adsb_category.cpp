// Every cell of the four ADS-B emitter category sets, pinned to the G.1.3 category it becomes.
#include "core/protocol/adsb_category.h"
#include "core/protocol/nmea_out.h"
#include "doctest/doctest.h"

using namespace skyblip::protocol;

namespace {

constexpr uint8_t kSetD = 1, kSetC = 2, kSetB = 3, kSetA = 4;

}  // namespace

// A1 is under 15 500 lb, which is the boundary G.1.3 draws between its categories 1 and 2.
TEST_CASE("adsb category: set A splits light fixed wing from the rest at G.1.3's own boundary") {
    const uint8_t expected[8] = {0, 1, 2, 2, 2, 2, 2, 3};
    for (uint8_t emitter = 0; emitter < 8; emitter++) {
        CAPTURE(int(emitter));
        CHECK(int(adsb_emitter_to_adsl_category(kSetA, emitter)) == int(expected[emitter]));
    }
}

TEST_CASE("adsb category: set B names the airborne kinds G.1.3 has a value for, and no more") {
    const uint8_t expected[8] = {0, 4, 5, 8, 6, 0, 11, 0};
    for (uint8_t emitter = 0; emitter < 8; emitter++) {
        CAPTURE(int(emitter));
        CHECK(int(adsb_emitter_to_adsl_category(kSetB, emitter)) == int(expected[emitter]));
    }
}

TEST_CASE("adsb category: surface vehicles, obstacles, reserved and bad codes are no category") {
    for (uint8_t emitter = 0; emitter < 8; emitter++) {
        CAPTURE(int(emitter));
        CHECK(adsb_emitter_to_adsl_category(kSetC, emitter) == 0);
        CHECK(adsb_emitter_to_adsl_category(kSetD, emitter) == 0);
        CHECK(adsb_emitter_to_adsl_category(0, emitter) == 0);
        CHECK(adsb_emitter_to_adsl_category(5, emitter) == 0);
    }
    CHECK(adsb_emitter_to_adsl_category(kSetA, 8) == 0);
    CHECK(adsb_emitter_to_adsl_category(kSetB, 0xFF) == 0);
}

TEST_CASE("adsb category: a glider, rotorcraft, skydiver, balloon and UAV reach PFLAA as such") {
    CHECK(adsl_category_to_alptas_type(adsb_emitter_to_adsl_category(kSetB, 1)) == 0x1);
    CHECK(adsl_category_to_alptas_type(adsb_emitter_to_adsl_category(kSetA, 7)) == 0x3);
    CHECK(adsl_category_to_alptas_type(adsb_emitter_to_adsl_category(kSetB, 3)) == 0x4);
    CHECK(adsl_category_to_alptas_type(adsb_emitter_to_adsl_category(kSetB, 2)) == 0xB);
    CHECK(adsl_category_to_alptas_type(adsb_emitter_to_adsl_category(kSetB, 6)) == 0xD);
}

TEST_CASE("adsb category: an airliner reaches PFLAA as a jet, and a light aircraft as a piston") {
    for (uint8_t emitter = 2; emitter <= 6; emitter++) {
        CAPTURE(int(emitter));
        CHECK(adsl_category_to_alptas_type(adsb_emitter_to_adsl_category(kSetA, emitter)) == 0x9);
    }
    CHECK(adsl_category_to_alptas_type(adsb_emitter_to_adsl_category(kSetA, 1)) == 0x8);
    CHECK(adsl_category_to_alptas_type(adsb_emitter_to_adsl_category(kSetB, 4)) == 0x8);
}
