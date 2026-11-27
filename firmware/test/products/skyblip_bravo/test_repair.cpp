// A squitter one bit off is repaired in the receiver thread and kept only for an aircraft held.
#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

constexpr uint32_t kHeld = 0x3C6586;
constexpr uint32_t kStranger = 0x4CA2B1;

void hear_one_bit_off(BravoRig& rig, uint32_t address) {
    AdsbAir air = adsb_air(adsb::airborne_position(address, 0, 48.51, 8.5, false, 1800));
    air.chips[10] ^= 0x0C;
    rig.platform.radio().receive_air(air.chips, sizeof(air.chips));
    rig.step();
}

void hear_two_bits_off(BravoRig& rig, uint32_t address) {
    AdsbAir air = adsb_air(adsb::airborne_position(address, 0, 48.51, 8.5, false, 1800));
    air.chips[10] ^= 0xCC;
    rig.platform.radio().receive_air(air.chips, sizeof(air.chips));
    rig.step();
}

}  // namespace

TEST_CASE("bravo adsb: a squitter one bit off from an aircraft held is repaired and folded") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(1);
    rig.hear_aircraft(kHeld, 2000);
    const uint32_t folded = rig.product.adsb().folded();
    hear_one_bit_off(rig, kHeld);
    rig.seconds_with_fix(1);
    CHECK(rig.product.adsb().repaired() == 1);
    CHECK(rig.product.adsb().repairs_refused() == 0);
    CHECK(rig.product.adsb().folded() == folded + 1);
    CHECK(rig.product.adsb().crc() == 0);
}

// DO-260B 2.2.4.3.4.7.3: a repaired frame may refresh a track, never found one.
TEST_CASE("bravo adsb: a squitter one bit off from an aircraft never heard founds nothing") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(1);
    hear_one_bit_off(rig, kStranger);
    rig.seconds_with_fix(1);
    CHECK(rig.product.adsb().repairs_refused() == 1);
    CHECK(rig.product.adsb().repaired() == 0);
    CHECK(rig.product.adsb().assembly().count() == 0);
    CHECK(rig.product.adsb().crc() == 0);
}

TEST_CASE("bravo adsb: a squitter two bits off from an aircraft held is repaired and folded") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(1);
    rig.hear_aircraft(kHeld, 2000);
    const uint32_t folded = rig.product.adsb().folded();
    hear_two_bits_off(rig, kHeld);
    rig.seconds_with_fix(1);
    CHECK(rig.product.adsb().repaired() == 1);
    CHECK(rig.product.adsb().folded() == folded + 1);
    CHECK(rig.product.adsb().crc() == 0);
}
