// The identity frame: who the unit is and what it runs, the same keys from every product.
#include <optional>
#include <string>

#include "core/comms/config.h"
#include "core/comms/identity_link.h"
#include "core/dfu/update.h"
#include "core/identity/unit.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "hardware/platform/host/link.h"
#include "hardware/platform/host/platform.h"
#include "hardware/platform/host/watchdog.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/bravo_rig.h"
#include "test/support/config_frame.h"
#include "test/support/product_rig.h"

using namespace skyblip;
using platform::host::Identity;

namespace {

constexpr uint16_t kChromePayloadBytes = 495;
const ports::ImageVersion kRunning{0, 3, 0, 1234};
const char* const kAsk = "{\"command\":\"identity\"}";

struct Desk {
    platform::host::Link link;
    platform::host::Watchdog watchdog;
    platform::host::Dfu dfu{watchdog};
    go::Settings settings = go::defaults();
    go::SettingsStore store{settings, kTestActivation, kNoOwnTransponder};
    comms::ConfigService config{link, store, &dfu};
    Identity port{std::nullopt};
    platform::host::Random random;
    identity::Unit unit{port, random};
    comms::IdentityLink identity{unit};

    Desk() {
        link.declare_payload_bytes(kChromePayloadBytes);
        link.raise_link(1);
        dfu.running_version_valid = true;
        dfu.running_image_version = kRunning;
        dfu.release = true;
        unit.load();
        config.set_identity(&identity);
    }

    std::string ask() {
        config.on_rx(config_frame(kAsk));
        return link_text(link.last().bytes);
    }
};

std::string refusal(const char* word) {
    return std::string("{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"") +
           word + "\"}";
}

constexpr ports::Capabilities without(ports::Capability part) {
    return static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(part));
}

std::string go_identity(ports::Capabilities fitted) {
    Rig rig(fitted, Identity::kSerial);
    rig.platform.dfu().running_version_valid = true;
    rig.platform.dfu().running_image_version = kRunning;
    rig.platform.link().declare_payload_bytes(kChromePayloadBytes);
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 500);
    rig.send(kAsk);
    rig.run(550, 850);
    std::string answer;
    for (const auto& frame : rig.platform.link().sent)
        if (frame.endpoint == events::Endpoint::Config &&
            link_text(frame.bytes).rfind("{\"command\":\"identity\"", 0) == 0)
            answer = link_text(frame.bytes);
    return answer;
}

}  // namespace

TEST_CASE("identity: the unit names its product, variant, firmware, chip and tag" *
          doctest::test_suite("main")) {
    Desk desk;
    CHECK(
        desk.ask() ==
        "{\"command\":\"identity\",\"product\":\"go\",\"variant\":\"standard\",\"version\":\"0."
        "3.0+1234\",\"release\":true,\"device_id\":\"8f3a1c2e9b7d4f60\",\"bluetooth_tag\":16170}");
    CHECK(desk.config.pending() == comms::Pending::None);
}

TEST_CASE("identity: the product and the variant are the ones the product hands the service") {
    Desk desk;
    desk.config.set_product(dfu::Product::Bravo);
    desk.config.set_variant(identity::Variant::Plus);
    CHECK(desk.ask().find("\"product\":\"bravo\",\"variant\":\"plus\"") != std::string::npos);
}

TEST_CASE("identity: answered in flight, since it changes nothing on the unit") {
    Desk desk;
    desk.config.set_flight_state(flight::FlightState::Airborne);
    CHECK(desk.ask().rfind("{\"command\":\"identity\"", 0) == 0);
}

TEST_CASE("identity: a unit that cannot read its running version refuses no_version") {
    Desk desk;
    desk.dfu.running_version_valid = false;
    CHECK(desk.ask() == refusal("no_version"));
}

// The widest version and a tag past 255 make the widest frame, the size the buffer holds.
TEST_CASE(
    "identity: the frame at its widest is the size kept for it, and a narrower link is told") {
    Desk widest;
    widest.dfu.running_image_version = dfu::kWidestVersion;
    widest.ask();
    CHECK(widest.link.last().bytes.size() == size_t(comms::IdentityLink::kIdentityFrameBytes));

    Desk narrow;
    narrow.dfu.running_image_version = dfu::kWidestVersion;
    narrow.link.declare_payload_bytes(comms::IdentityLink::kIdentityFrameBytes - 1);
    CHECK(narrow.ask() == refusal("small_mtu"));
}

TEST_CASE("identity: a product that hands the config service no identity answers unknown_command") {
    platform::host::Link link;
    platform::host::Watchdog watchdog;
    platform::host::Dfu dfu{watchdog};
    go::Settings settings = go::defaults();
    go::SettingsStore store{settings, kTestActivation, kNoOwnTransponder};
    comms::ConfigService config{link, store, &dfu};
    link.declare_payload_bytes(kChromePayloadBytes);
    link.raise_link(1);
    config.on_rx(config_frame(kAsk));
    CHECK(link_text(link.last().bytes) == refusal("unknown_command"));
}

TEST_CASE("identity: a Go is plus with inclinometer, buzzer and haptic fitted, standard without") {
    CHECK(go_identity(platform::host::Platform::kFullyFitted).find("\"variant\":\"plus\"") !=
          std::string::npos);
    CHECK(go_identity(without(ports::Capability::Haptic))
              .find("\"product\":\"go\",\"variant\":\"standard\"") != std::string::npos);
}

TEST_CASE("identity: a Bravo, keyless and never activated, names itself and its chip") {
    BravoRig rig;
    rig.platform.dfu().running_version_valid = true;
    rig.platform.dfu().running_image_version = kRunning;
    rig.platform.link().declare_payload_bytes(kChromePayloadBytes);
    REQUIRE(rig.setup() == Status::Ok);
    rig.send(kAsk);
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) ==
          "{\"command\":\"identity\",\"product\":\"bravo\",\"variant\":\"standard\",\"version\":"
          "\"0.3.0+1234\",\"release\":false,\"device_id\":\"8f3a1c2e9b7d4f60\",\"bluetooth_tag\":"
          "16170}");
}
