// A unit names the radio protocol versions it carries, to hold it to its declaration of conformity.
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include "core/comms/config.h"
#include "core/comms/radio_protocols.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "ports/link.h"
#include "products/skyblip_bravo/radio_protocols.h"
#include "products/skyblip_go/radio_protocols.h"
#include "products/skyblip_go/settings_store.h"
#include "protocols/adsb/version.h"
#include "protocols/adsl/version.h"
#include "protocols/alptas/version.h"
#include "test/support/bravo_rig.h"
#include "test/support/config_frame.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

constexpr const char* kAsk = "{\"command\":\"protocols\"}";

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

std::string version(const char* stack, const char* text) {
    return std::string(",\"") + stack + "_version\":\"" + text + "\"";
}

std::string last_config_frame(const platform::host::Link& link) {
    for (auto it = link.sent.rbegin(); it != link.sent.rend(); ++it)
        if (it->endpoint == events::Endpoint::Config) return it->bytes;
    return std::string();
}

std::string go_answer() {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    rig.platform.link().declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    rig.send(kAsk);
    rig.run(0, 200);
    return last_config_frame(rig.platform.link());
}

std::string bravo_answer() {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    rig.platform.link().declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    rig.send(kAsk);
    rig.run_for(200);
    return last_config_frame(rig.platform.link());
}

}  // namespace

// The release notes read products/<product>/PROTOCOLS and the unit its own list, so the two agree.
TEST_CASE("radio protocols: each product carries the stacks its PROTOCOLS file lists") {
    CHECK(stems(go::kRadioProtocols) == listed_in("products/skyblip_go/PROTOCOLS"));
    CHECK(stems(bravo::kRadioProtocols) == listed_in("products/skyblip_bravo/PROTOCOLS"));
}

TEST_CASE("radio protocols: a Go names its ADS-L and ALP-TAS versions") {
    CHECK(link_text(go_answer()) == "{\"command\":\"protocols\"" + version("adsl", adsl::kVersion) +
                                        version("alptas", alptas::kVersion) + "}");
}

TEST_CASE("radio protocols: a Bravo names its ADS-B version") {
    CHECK(link_text(bravo_answer()) ==
          "{\"command\":\"protocols\"" + version("adsb", adsb::kVersion) + "}");
}

// schemas/link.v1.json states these widths at this release's versions.
TEST_CASE("radio protocols: a Go answers in 18 bytes and a Bravo in 11, on the narrowest link") {
    CHECK(go_answer().size() == 18);
    CHECK(bravo_answer().size() == 11);
}

TEST_CASE("radio protocols: a config service handed no stacks answers unknown_command") {
    platform::host::Link link;
    go::Settings settings = go::defaults();
    go::SettingsStore store{settings, identity::Activation{}, kNoOwnTransponder};
    comms::ConfigService config{link, store, nullptr};
    link.raise_link(1);
    config.on_rx(config_frame(kAsk));
    CHECK(link_text(link.last().bytes) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":"
          "\"unknown_command\"}");
}
