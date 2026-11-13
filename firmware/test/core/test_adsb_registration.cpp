// The registration a transponder's flight identification spells, as a pilot would type it.
#include <string>

#include "core/protocol/adsb_registration.h"
#include "doctest/doctest.h"

using namespace skyblip::protocol;

namespace {

std::string registration(const char* callsign, uint32_t address, size_t capacity = 10) {
    char out[16] = {};
    const int written = adsb_registration(out, capacity, callsign, address);
    CHECK(written == static_cast<int>(std::string(out).size()));
    return out;
}

}  // namespace

TEST_CASE("adsb registration: the address block names where the nationality mark ends") {
    CHECK(registration("FJABC", 0x3A4B5C) == "F-JABC");
    CHECK(registration("DEABC", 0x3C6586) == "D-EABC");
    CHECK(registration("GABCD", 0x400F00) == "G-ABCD");
    CHECK(registration("HBABC", 0x4B1234) == "HB-ABC");
    CHECK(registration("ECABC", 0x345678) == "EC-ABC");
}

// Gliders are registered by number in Germany, Switzerland and the Netherlands.
TEST_CASE("adsb registration: a mark of digits is a registration as well as one of letters") {
    CHECK(registration("D1234", 0x3D0000) == "D-1234");
    CHECK(registration("HB1234", 0x4B0001) == "HB-1234");
}

TEST_CASE("adsb registration: a flight number or a mark of another state is copied as heard") {
    CHECK(registration("DLH4AB", 0x3C6586) == "DLH4AB");
    CHECK(registration("AFR123", 0x3A0000) == "AFR123");
    CHECK(registration("GABCD", 0x3A4B5C) == "GABCD");
    CHECK(registration("FJ", 0x3A4B5C) == "FJ");
    CHECK(registration("FABCDEF", 0x3A4B5C) == "FABCDEF");
}

// The United States writes N905NA without a dash, so its block is left out on purpose.
TEST_CASE("adsb registration: an address outside the blocks known copies the callsign") {
    CHECK(registration("N905NA", 0xAC82EC) == "N905NA");
    CHECK(registration("FJABC", 0x000000) == "FJABC");
}

TEST_CASE("adsb registration: the dash is added only when the result fits, never cut") {
    CHECK(registration("FJABC", 0x3A4B5C, 7) == "F-JABC");
    CHECK(registration("FJABC", 0x3A4B5C, 6) == "FJABC");
    CHECK(registration("FJABC", 0x3A4B5C, 4) == "FJA");
}
