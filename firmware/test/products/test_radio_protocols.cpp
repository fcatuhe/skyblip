// A unit names the radio protocol versions it carries, to hold it to its declaration of conformity.
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include "core/comms/radio_protocols.h"
#include "doctest/doctest.h"
#include "products/skyblip_bravo/radio_protocols.h"
#include "products/skyblip_go/radio_protocols.h"
#include "protocols/adsb/version.h"
#include "protocols/adsl/version.h"
#include "protocols/alptas/version.h"
#include "test/support/bravo_rig.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

constexpr const char* kAsk = "{\"command\":\"protocols\"}";
constexpr const char* kSmallMtu =
    "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"small_mtu\"}";

const char* stem(comms::RadioProtocol protocol) {
    switch (protocol) {
        case comms::RadioProtocol::Adsb: return "adsb";
        case comms::RadioProtocol::Adsl: return "adsl";
        case comms::RadioProtocol::Alptas: return "alptas";
    }
    return "";
}

template <size_t N>
std::vector<std::string> stems(const comms::RadioProtocol (&protocols)[N]) {
    std::vector<std::string> out;
    for (const comms::RadioProtocol protocol : protocols) out.push_back(stem(protocol));
    return out;
}

std::vector<std::string> listed_in(const char* path) {
    std::vector<std::string> out;
    std::ifstream file(path);
    REQUIRE_MESSAGE(file.is_open(), path);
    for (std::string stack; std::getline(file, stack);) {
        stack.erase(stack.find_last_not_of(" \t\r") + 1);
        stack.erase(0, stack.find_first_not_of(" \t"));
        if (!stack.empty()) out.push_back(stack);
    }
    return out;
}

bool answered(const std::string& frame) {
    return frame.rfind("{\"command\":\"protocols\"", 0) == 0;
}

std::string pair(const char* stack, const char* version, const char* specification) {
    return std::string(",\"") + stack + "_version\":\"" + version + "\",\"" + stack +
           "_specification\":\"" + specification + "\"";
}

std::string go_answer_on(uint16_t payload_bytes) {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    rig.platform.link().declare_payload_bytes(payload_bytes);
    rig.send(kAsk);
    rig.run(0, 200);
    return rig.last_on(events::Endpoint::Config);
}

std::string bravo_answer_on(uint16_t payload_bytes) {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    rig.platform.link().declare_payload_bytes(payload_bytes);
    rig.send(kAsk);
    rig.run_for(200);
    return rig.last_on(events::Endpoint::Config);
}

}  // namespace

// The release notes read products/<product>/PROTOCOLS and the unit its own list, so the two agree.
TEST_CASE("radio protocols: each product carries the stacks its PROTOCOLS file lists") {
    CHECK(stems(go::kRadioProtocols) == listed_in("products/skyblip_go/PROTOCOLS"));
    CHECK(stems(bravo::kRadioProtocols) == listed_in("products/skyblip_bravo/PROTOCOLS"));
}

TEST_CASE("radio protocols: a Go names its ADS-L and ALP-TAS versions and what each implements") {
    CHECK(go_answer_on(244) == "{\"command\":\"protocols\"" +
                                   pair("adsl", adsl::kVersion, adsl::kSpecification) +
                                   pair("alptas", alptas::kVersion, alptas::kSpecification) + "}");
}

TEST_CASE("radio protocols: a Bravo names its ADS-B version and what it implements") {
    CHECK(bravo_answer_on(244) ==
          "{\"command\":\"protocols\"" + pair("adsb", adsb::kVersion, adsb::kSpecification) + "}");
}

// schemas/link.v1.json states these widths, at this release's versions and specifications.
TEST_CASE("radio protocols: a Go answers in 143 bytes and a Bravo in 102, small_mtu a byte short") {
    CHECK(answered(go_answer_on(143)));
    CHECK(go_answer_on(142) == kSmallMtu);
    CHECK(answered(bravo_answer_on(102)));
    CHECK(bravo_answer_on(101) == kSmallMtu);
}
