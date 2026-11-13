// ADS-L 4 SRD-860 issue 2 G.1.11 to G.1.17: the quality codes a Traffic payload carries.
#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"

using namespace skyblip;

TEST_CASE("ADS-L.4.SRD860.G.1.11: the source integrity level is two bits, and 0 is undefined") {
    adsl::Packet packet{};
    packet.reset();
    for (uint8_t code = 0; code < 4; code++) {
        packet.source_integrity = code;
        CHECK(packet.source_integrity == code);
    }
    packet.set_integrity_unknown();
    CHECK(packet.source_integrity == 0);
}

// The NACp table, in centimetres of 95% horizontal error bound.
TEST_CASE("ADS-L.4.SRD860.G.1.14: the horizontal accuracy code sits on the table's boundaries") {
    CHECK(adsl::Packet::horizontal_accuracy_code(299) == 7);
    CHECK(adsl::Packet::horizontal_accuracy_code(300) == 6);
    CHECK(adsl::Packet::horizontal_accuracy_code(999) == 6);
    CHECK(adsl::Packet::horizontal_accuracy_code(1000) == 5);
    CHECK(adsl::Packet::horizontal_accuracy_code(2999) == 5);
    CHECK(adsl::Packet::horizontal_accuracy_code(3000) == 4);
    CHECK(adsl::Packet::horizontal_accuracy_code(9259) == 4);
    CHECK(adsl::Packet::horizontal_accuracy_code(9260) == 3);
    CHECK(adsl::Packet::horizontal_accuracy_code(18519) == 3);
    CHECK(adsl::Packet::horizontal_accuracy_code(18520) == 2);
    CHECK(adsl::Packet::horizontal_accuracy_code(55559) == 2);
    CHECK(adsl::Packet::horizontal_accuracy_code(55560) == 1);
    CHECK(adsl::Packet::horizontal_accuracy_code(92599) == 1);
    CHECK(adsl::Packet::horizontal_accuracy_code(92600) == 0);
}

// The GVA table, in centimetres of 95% vertical error bound.
TEST_CASE("ADS-L.4.SRD860.G.1.15: the vertical accuracy code sits on the table's boundaries") {
    CHECK(adsl::Packet::vertical_accuracy_code(999) == 3);
    CHECK(adsl::Packet::vertical_accuracy_code(1000) == 2);
    CHECK(adsl::Packet::vertical_accuracy_code(4499) == 2);
    CHECK(adsl::Packet::vertical_accuracy_code(4500) == 1);
    CHECK(adsl::Packet::vertical_accuracy_code(14999) == 1);
    CHECK(adsl::Packet::vertical_accuracy_code(15000) == 0);
}

// The clause's formula for the figure the NACp table is read with.
TEST_CASE("ADS-L.4.SRD860.G.1.14: HFOM is 2 x HDOP x a UERE of 6 m") {
    CHECK(adsl::kUereCm == 600u);
    CHECK(adsl::hfom_cm(100) == 1200u);
    CHECK(adsl::hfom_cm(90) == 1080u);
    CHECK(adsl::hfom_cm(0) == 0u);
    CHECK(adsl::Packet::horizontal_accuracy_code(adsl::hfom_cm(90)) == 5);
}
