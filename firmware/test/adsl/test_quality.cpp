// ADS-L 4 SRD-860 issue 2 G.1.11 to G.1.17: the quality a Traffic payload claims for its position.
#include <cstdint>

#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {
adsl::Packet from_dop(uint16_t hdop_e2, uint16_t vdop_e2, bool fix = true) {
    model::Ownship own{};
    own.fix_valid = fix;
    own.utc_valid = true;
    own.latitude_1e7 = 481234500;
    own.longitude_1e7 = 81234500;
    own.altitude_hae_mm = 1000000;
    own.speed_mm_s = 100000;
    own.hdop_e2 = hdop_e2;
    own.vdop_e2 = vdop_e2;
    own.flight_state = 2;
    adsl::Packet packet{};
    protocol::from_own(packet, own, 0x123456, 6, 4);
    return packet;
}

// The clause's formula: HFOM = 2 * HDOP * UERE, and the UERE it names is 6 m.
constexpr uint32_t kSpecUereM = 6;
}  // namespace

// No design assurance credit is held for this firmware, and the clause has a code that says so.
TEST_CASE("ADS-L.4.SRD860.G.1.12: the design assurance transmitted is 0, none") {
    CHECK(adsl::Packet::kDesignAssuranceNone == 0);
    CHECK(from_dop(90, 150).design_assurance == 0);
    CHECK(from_dop(0, 0, /*fix=*/false).design_assurance == 0);
}

// The table asks for a protection level, and a receiver that reports none leaves it at 0 (G.1.17).
TEST_CASE("ADS-L.4.SRD860.G.1.13: without a protection level the navigation integrity is 0") {
    CHECK(from_dop(90, 150).navigation_integrity == 0);
    CHECK(from_dop(900, 1500).navigation_integrity == 0);
    CHECK(from_dop(0, 0, /*fix=*/false).navigation_integrity == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.1.14: HFOM is derived from HDOP as 2 * HDOP * 6 m") {
    const uint16_t hdop_e2 = 90;
    const uint32_t spec_hfom_cm = 2u * hdop_e2 * kSpecUereM;
    CHECK(from_dop(hdop_e2, 150).horizontal_accuracy ==
          adsl::Packet::horizontal_accuracy_code(spec_hfom_cm));
}

// A 2D solution reports no VDOP, and a height nothing measured the quality of gets no claim at all.
TEST_CASE("ADS-L.4.SRD860.G.1.15: a solution with no VDOP claims no vertical accuracy") {
    CHECK(from_dop(90, 0).vertical_accuracy == 0);
    CHECK(from_dop(90, 150).vertical_accuracy > 0);
}

// Every code in the block has a zero meaning "no fix", and without one that is the honest answer.
TEST_CASE("ADS-L.4.SRD860.G.1.17: with no fix the whole quality block claims nothing") {
    adsl::Packet packet = from_dop(90, 150, /*fix=*/false);
    CHECK(packet.source_integrity == 0);
    CHECK(packet.design_assurance == 0);
    CHECK(packet.navigation_integrity == 0);
    CHECK(packet.horizontal_accuracy == 0);
    CHECK(packet.vertical_accuracy == 0);
    CHECK(packet.velocity_accuracy == 0);
}

// A receiver that reports no HDOP is not a receiver reporting a good one.
TEST_CASE("ADS-L.4.SRD860.G.1.17: a fix with no reported HDOP claims no accuracy either") {
    adsl::Packet packet = from_dop(0, 0);
    CHECK(packet.horizontal_accuracy == 0);
    CHECK(packet.navigation_integrity == 0);
    CHECK(packet.velocity_accuracy == 0);
}

// The accuracy claim moves with the fix, which is what the clause asks an uncertified device for.
TEST_CASE("ADS-L.4.SRD860.G.1.17: the accuracy codes are dynamic, not a static zero") {
    const adsl::Packet good = from_dop(90, 150);
    const adsl::Packet poor = from_dop(800, 800);
    CHECK(good.horizontal_accuracy > poor.horizontal_accuracy);
    CHECK(good.vertical_accuracy >= poor.vertical_accuracy);
    CHECK(good.velocity_accuracy > poor.velocity_accuracy);
}

TEST_CASE("ADS-L.4.SRD860.G.1.17: an uncertified device claims no integrity and no assurance") {
    const adsl::Packet packet = from_dop(90, 150);
    CHECK(packet.source_integrity == 0);
    CHECK(packet.design_assurance == 0);
    CHECK(packet.navigation_integrity == 0);
}

// The clause's bounds are 1, 3 and 10 m/s, and no receiver here reports one, so NACp stands in.
TEST_CASE("ADS-L.4.SRD860.G.1.16: with no velocity error reported, the code follows NACp down") {
    CHECK(protocol::velocity_accuracy_code(7) == 3);
    CHECK(protocol::velocity_accuracy_code(6) == 2);
    CHECK(protocol::velocity_accuracy_code(5) == 1);
    CHECK(protocol::velocity_accuracy_code(4) == 0);
    CHECK(protocol::velocity_accuracy_code(3) == 0);
    CHECK(protocol::velocity_accuracy_code(0) == 0);
}
