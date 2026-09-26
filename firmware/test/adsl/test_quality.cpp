// ADS-L 4 SRD-860 issue 2 G.1.11 to G.1.17: the quality a Traffic payload claims for its position.
#include <cstdint>

#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {
protocol::AdslPacket from_dop(uint16_t hdop_e2, uint16_t vdop_e2, bool fix = true) {
    model::OwnState own{};
    own.fix_valid = fix;
    own.utc_valid = true;
    own.lat_1e7 = 481234500;
    own.lon_1e7 = 81234500;
    own.alt_mm = 1000000;
    own.speed_mm_s = 100000;
    own.hdop_e2 = hdop_e2;
    own.vdop_e2 = vdop_e2;
    own.flight_state = 2;
    protocol::AdslPacket p{};
    protocol::from_own(p, own, 0x123456, 6, 4);
    return p;
}

// The clause's formula: HFOM = 2 * HDOP * UERE, and the UERE it names is 6 m.
constexpr uint32_t kSpecUereM = 6;
}  // namespace

TEST_CASE("ADS-L.4.SRD860.G.1.11: the source integrity level is two bits, and 0 is undefined") {
    protocol::AdslPacket p{};
    p.init();
    for (uint8_t code = 0; code < 4; code++) {
        p.SourceIntegrity = code;
        CHECK(p.SourceIntegrity == code);
    }
    p.set_integrity_unknown();
    CHECK(p.SourceIntegrity == 0);
}

// No design assurance credit is held for this firmware, and the clause has a code that says so.
TEST_CASE("ADS-L.4.SRD860.G.1.12: the design assurance transmitted is 0, none") {
    CHECK(protocol::AdslPacket::kDesignAssuranceNone == 0);
    CHECK(from_dop(90, 150).DesignAssurance == 0);
    CHECK(from_dop(0, 0, /*fix=*/false).DesignAssurance == 0);
}

// The table asks for a protection level, and a receiver that reports none leaves it at 0 (G.1.17).
TEST_CASE("ADS-L.4.SRD860.G.1.13: without a protection level the navigation integrity is 0") {
    CHECK(from_dop(90, 150).NavigIntegrity == 0);
    CHECK(from_dop(900, 1500).NavigIntegrity == 0);
    CHECK(from_dop(0, 0, /*fix=*/false).NavigIntegrity == 0);
}

// The NACp table, in centimetres of 95% horizontal error bound.
TEST_CASE("ADS-L.4.SRD860.G.1.14: the horizontal accuracy code sits on the table's boundaries") {
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(299) == 7);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(300) == 6);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(999) == 6);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(1000) == 5);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(2999) == 5);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(3000) == 4);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(9259) == 4);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(9260) == 3);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(18519) == 3);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(18520) == 2);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(55559) == 2);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(55560) == 1);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(92599) == 1);
    CHECK(protocol::AdslPacket::horizontal_accuracy_code(92600) == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.1.14: HFOM is derived from HDOP as 2 * HDOP * 6 m") {
    const uint16_t hdop_e2 = 90;
    const uint32_t spec_hfom_cm = 2u * hdop_e2 * kSpecUereM;
    CHECK(from_dop(hdop_e2, 150).HorizAccuracy ==
          protocol::AdslPacket::horizontal_accuracy_code(spec_hfom_cm));
}

// The GVA table, in centimetres of 95% vertical error bound.
TEST_CASE("ADS-L.4.SRD860.G.1.15: the vertical accuracy code sits on the table's boundaries") {
    CHECK(protocol::AdslPacket::vertical_accuracy_code(999) == 3);
    CHECK(protocol::AdslPacket::vertical_accuracy_code(1000) == 2);
    CHECK(protocol::AdslPacket::vertical_accuracy_code(4499) == 2);
    CHECK(protocol::AdslPacket::vertical_accuracy_code(4500) == 1);
    CHECK(protocol::AdslPacket::vertical_accuracy_code(14999) == 1);
    CHECK(protocol::AdslPacket::vertical_accuracy_code(15000) == 0);
}

// A 2D solution reports no VDOP, and a height nothing measured the quality of gets no claim at all.
TEST_CASE("ADS-L.4.SRD860.G.1.15: a solution with no VDOP claims no vertical accuracy") {
    CHECK(from_dop(90, 0).VertAccuracy == 0);
    CHECK(from_dop(90, 150).VertAccuracy > 0);
}

// The clause's bounds are 1, 3 and 10 m/s, and no receiver here reports one, so NACp stands in.
TEST_CASE("ADS-L.4.SRD860.G.1.16: with no velocity error reported, the code follows NACp down") {
    CHECK(protocol::AdslPacket::velocity_accuracy_code(7) == 3);
    CHECK(protocol::AdslPacket::velocity_accuracy_code(6) == 2);
    CHECK(protocol::AdslPacket::velocity_accuracy_code(5) == 1);
    CHECK(protocol::AdslPacket::velocity_accuracy_code(4) == 0);
    CHECK(protocol::AdslPacket::velocity_accuracy_code(3) == 0);
    CHECK(protocol::AdslPacket::velocity_accuracy_code(0) == 0);
}

// Every code in the block has a zero meaning "no fix", and without one that is the honest answer.
TEST_CASE("ADS-L.4.SRD860.G.1.17: with no fix the whole quality block claims nothing") {
    protocol::AdslPacket p = from_dop(90, 150, /*fix=*/false);
    CHECK(p.SourceIntegrity == 0);
    CHECK(p.DesignAssurance == 0);
    CHECK(p.NavigIntegrity == 0);
    CHECK(p.HorizAccuracy == 0);
    CHECK(p.VertAccuracy == 0);
    CHECK(p.VelAccuracy == 0);
}

// A receiver that reports no HDOP is not a receiver reporting a good one.
TEST_CASE("ADS-L.4.SRD860.G.1.17: a fix with no reported HDOP claims no accuracy either") {
    protocol::AdslPacket p = from_dop(0, 0);
    CHECK(p.HorizAccuracy == 0);
    CHECK(p.NavigIntegrity == 0);
    CHECK(p.VelAccuracy == 0);
}

// The accuracy claim moves with the fix, which is what the clause asks an uncertified device for.
TEST_CASE("ADS-L.4.SRD860.G.1.17: the accuracy codes are dynamic, not a static zero") {
    const protocol::AdslPacket good = from_dop(90, 150);
    const protocol::AdslPacket poor = from_dop(800, 800);
    CHECK(good.HorizAccuracy > poor.HorizAccuracy);
    CHECK(good.VertAccuracy >= poor.VertAccuracy);
    CHECK(good.VelAccuracy > poor.VelAccuracy);
}

TEST_CASE("ADS-L.4.SRD860.G.1.17: an uncertified device claims no integrity and no assurance") {
    const protocol::AdslPacket p = from_dop(90, 150);
    CHECK(p.SourceIntegrity == 0);
    CHECK(p.DesignAssurance == 0);
    CHECK(p.NavigIntegrity == 0);
}
