// DO-260B 2.2.3.2.7: the status and event-driven messages, of which the operational status is read.
#include <cstdint>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/adsb/message.h"
#include "protocols/adsb/squitter.h"
#include "protocols/adsb/test/conformance/reference.h"

using namespace skyblip;
using namespace skyblip::adsb;
using namespace skyblip::adsb::conformance;
using skyblip::wire::Status;

namespace {

AdsbFrame status(uint8_t subtype, uint8_t version, uint8_t gva) {
    AdsbFrame frame = adsb_operational_status(kAddress, kAirborneCapability, version, gva);
    put(frame, 6, 8, subtype);
    return frame;
}

OperationalStatus heard_status(const AdsbFrame& frame) {
    return heard_ok(frame).operational_status;
}

}

TEST_CASE(
    "DO-260B 2.2.3.2.7: of the status and event messages, TYPE 31 is read and the rest pass") {
    CHECK(heard_ok(status(0, 2, 1)).kind == Kind::OperationalStatus);
    for (uint8_t type_code : {23, 24, 25, 26, 27, 28, 29, 30})
        CHECK(hear(squitter(type_code, 1)).status == Status::Unsupported);
}

// TODO: fc 17nov26 TYPE 29 is not decoded, until an application shows a target's selected altitude
TEST_CASE(
    "DO-260B 2.2.3.2.7.1: the target state and status message gives the selected altitude and "
    "heading" *
    doctest::skip()) {
    FAIL("not implemented: TYPE 29 comes back Unsupported");
}

TEST_CASE("DO-260B 2.2.3.2.7.2: the operational status reads its subtype, version and GVA") {
    const OperationalStatus read = heard_status(status(0, 2, 1));
    CHECK(read.subtype == 0);
    CHECK(read.version == 2);
    CHECK(read.gva_valid);
    CHECK(read.gva == 1);
}

TEST_CASE("DO-260B 2.2.3.2.7.2.1: the operational status is TYPE 31") {
    CHECK(heard_ok(status(0, 2, 1)).type_code == 31);
    AdsbFrame other = status(0, 2, 1);
    put(other, 1, 5, 30);
    CHECK(hear(other).status == Status::Unsupported);
}

TEST_CASE("DO-260B 2.2.3.2.7.2.2: subtype 0 is airborne, 1 surface and 2 to 7 reserved") {
    CHECK(heard_status(status(0, 2, 1)).subtype == 0);
    CHECK(heard_status(status(1, 2, 1)).subtype == 1);
    for (uint8_t subtype = 2; subtype < 8; subtype++)
        CHECK(hear(status(subtype, 2, 1)).status == Status::Unsupported);
}

// TODO: fc 17nov26 no capability class is reported, until an application asks what a sender can do
TEST_CASE("DO-260B 2.2.3.2.7.2.3: the capability class codes say what the sender can do" *
          doctest::skip()) {
    FAIL("not implemented: ME bits 9 to 24 are not read");
}

// TODO: fc 17nov26 no operational mode is reported, until an application shows an IDENT or an RA
TEST_CASE("DO-260B 2.2.3.2.7.2.4: the operational mode codes give TCAS RA, IDENT, antenna and SDA" *
          doctest::skip()) {
    FAIL("not implemented: ME bits 25 to 40 are not read");
}

TEST_CASE("DO-260B 2.2.3.2.7.2.5: the version is ME bits 41 to 43, 0 to 2 named and 3 to 7 kept") {
    for (uint8_t version = 0; version < 8; version++)
        CHECK(heard_status(status(0, version, 0)).version == version);
}

// TODO: fc 17nov26 no NIC is reported, until a consumer weighs a position's integrity
TEST_CASE("DO-260B 2.2.3.2.7.2.6: NIC supplement A completes the position's NIC" *
          doctest::skip()) {
    FAIL("not implemented: ME bit 44 is not read");
}

// TODO: fc 17nov26 no NACp is reported, until a consumer weighs a position's accuracy
TEST_CASE("DO-260B 2.2.3.2.7.2.7: the NACp bounds the position's accuracy" * doctest::skip()) {
    FAIL("not implemented: ME bits 45 to 48 are not read");
}

TEST_CASE("DO-260B 2.2.3.2.7.2.8: GVA is ME bits 49 and 50 of an airborne version 2 status") {
    for (uint8_t gva = 0; gva < 4; gva++) {
        const OperationalStatus read = heard_status(status(0, 2, gva));
        CHECK(read.gva_valid);
        CHECK(read.gva == gva);
    }
    CHECK_FALSE(heard_status(status(1, 2, 1)).gva_valid);
    CHECK_FALSE(heard_status(status(0, 1, 1)).gva_valid);
}

// TODO: fc 17nov26 no SIL is reported, until a consumer weighs a position's integrity
TEST_CASE(
    "DO-260B 2.2.3.2.7.2.9: the SIL bounds the chance the position is outside its containment" *
    doctest::skip()) {
    FAIL("not implemented: ME bits 51 and 52 are not read");
}

// TODO: fc 17nov26 no NICbaro is reported, until a consumer weighs an altitude's integrity
TEST_CASE("DO-260B 2.2.3.2.7.2.10: NICbaro says whether the pressure altitude was cross-checked" *
          doctest::skip()) {
    FAIL("not implemented: ME bit 53 of subtype 0 is not read");
}

// TODO: fc 17nov26 no size is reported, until a surface picture draws one
TEST_CASE("DO-260B 2.2.3.2.7.2.11: the length and width code sizes a surface participant" *
          doctest::skip()) {
    FAIL("not implemented: ME bits 21 to 24 of subtype 1 are not read");
}

// TODO: fc 17nov26 no surface heading type is reported, until a surface picture draws one
TEST_CASE(
    "DO-260B 2.2.3.2.7.2.12: the track angle/heading bit says which a surface participant sends" *
    doctest::skip()) {
    FAIL("not implemented: ME bit 53 of subtype 1 is not read");
}

// TODO: fc 17nov26 no heading reference is reported, until an airspeed heading is drawn as a track
TEST_CASE("DO-260B 2.2.3.2.7.2.13: the HRD says whether headings are true or magnetic" *
          doctest::skip()) {
    FAIL("not implemented: ME bit 54 is not read");
}

// TODO: fc 17nov26 no SIL is reported, until a consumer weighs a position's integrity
TEST_CASE(
    "DO-260B 2.2.3.2.7.2.14: the SIL supplement says whether SIL counts per hour or per sample" *
    doctest::skip()) {
    FAIL("not implemented: ME bit 55 is not read");
}

TEST_CASE("DO-260B 2.2.3.2.7.2.15: the reserved bits change nothing read") {
    AdsbFrame surface = status(1, 2, 3);
    put(surface, 56, 56, 1);
    const OperationalStatus read = heard_status(surface);
    CHECK(read.version == 2);
    CHECK_FALSE(read.gva_valid);
    AdsbFrame airborne = status(0, 2, 1);
    put(airborne, 56, 56, 1);
    CHECK(heard_status(airborne).gva == 1);
}

TEST_CASE("DO-260B 2.2.3.2.7.3: a TYPE 23 test message never becomes a report") {
    for (uint8_t subtype = 0; subtype < 8; subtype++)
        CHECK(hear(squitter(23, subtype)).status == Status::Unsupported);
}

TEST_CASE(
    "DO-260B 2.2.3.2.7.4: a TYPE 24 surface system status is left to the system that sent it") {
    for (uint8_t subtype = 0; subtype < 8; subtype++)
        CHECK(hear(squitter(24, subtype)).status == Status::Unsupported);
}

TEST_CASE("DO-260B 2.2.3.2.7.5: TYPE 25 is reserved and never read") {
    CHECK(hear(squitter(25)).status == Status::Unsupported);
}

TEST_CASE("DO-260B 2.2.3.2.7.6: TYPE 26 is reserved and never read") {
    CHECK(hear(squitter(26)).status == Status::Unsupported);
}

TEST_CASE("DO-260B 2.2.3.2.7.7: TYPE 27 is reserved and never read") {
    CHECK(hear(squitter(27)).status == Status::Unsupported);
}

// TODO: fc 17nov26 TYPE 28 is not decoded, until the glass can show an emergency or a TCAS RA
TEST_CASE(
    "DO-260B 2.2.3.2.7.8: the aircraft status gives the emergency, the Mode A code and the "
    "RA" *
    doctest::skip()) {
    FAIL("not implemented: TYPE 28 comes back Unsupported");
}

TEST_CASE("DO-260B 2.2.3.2.7.9: TYPE 30 is reserved and never read") {
    CHECK(hear(squitter(30)).status == Status::Unsupported);
}
