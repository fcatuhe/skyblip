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

adsb::Squitter status(uint8_t subtype, uint8_t version, uint8_t gva) {
    adsb::Squitter frame = adsb::operational_status(kAddress, kAirborneCapability, version, gva);
    put(frame, 6, 8, subtype);
    return frame;
}

OperationalStatus heard_status(const adsb::Squitter& frame) {
    return heard_ok(frame).operational_status;
}

}

TEST_CASE(
    "DO-260B 2.2.3.2.7: of the status and event messages, TYPE 31 is read and the rest pass") {
    CHECK(heard_ok(status(0, 2, 1)).kind == Kind::OperationalStatus);
    for (uint8_t type_code : {23, 24, 25, 26, 27, 28, 29, 30})
        CHECK(hear(squitter(type_code, 1)).status == Status::Unsupported);
}

TEST_CASE("DO-260B 2.2.3.2.7.2: the operational status reads its subtype, version and GVA" *
          doctest::test_suite("main")) {
    const OperationalStatus read = heard_status(status(0, 2, 1));
    CHECK(read.subtype == 0);
    CHECK(read.version == 2);
    CHECK(read.gva_valid);
    CHECK(read.gva == 1);
}

TEST_CASE("DO-260B 2.2.3.2.7.2.1: the operational status is TYPE 31") {
    CHECK(heard_ok(status(0, 2, 1)).type_code == 31);
    adsb::Squitter other = status(0, 2, 1);
    put(other, 1, 5, 30);
    CHECK(hear(other).status == Status::Unsupported);
}

TEST_CASE("DO-260B 2.2.3.2.7.2.2: subtype 0 is airborne, 1 surface and 2 to 7 reserved") {
    CHECK(heard_status(status(0, 2, 1)).subtype == 0);
    CHECK(heard_status(status(1, 2, 1)).subtype == 1);
    for (uint8_t subtype = 2; subtype < 8; subtype++)
        CHECK(hear(status(subtype, 2, 1)).status == Status::Unsupported);
}

TEST_CASE("DO-260B 2.2.3.2.7.2.5: the version is ME bits 41 to 43, 0 to 2 named and 3 to 7 kept") {
    for (uint8_t version = 0; version < 8; version++)
        CHECK(heard_status(status(0, version, 0)).version == version);
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

TEST_CASE("DO-260B 2.2.3.2.7.2.15: the reserved bits change nothing read") {
    adsb::Squitter surface = status(1, 2, 3);
    put(surface, 56, 56, 1);
    const OperationalStatus read = heard_status(surface);
    CHECK(read.version == 2);
    CHECK_FALSE(read.gva_valid);
    adsb::Squitter airborne = status(0, 2, 1);
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

TEST_CASE("DO-260B 2.2.3.2.7.9: TYPE 30 is reserved and never read") {
    CHECK(hear(squitter(30)).status == Status::Unsupported);
}
