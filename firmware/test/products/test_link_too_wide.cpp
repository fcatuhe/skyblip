// What a link too narrow for a reply hears.
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "core/events/link.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/config_frame.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {

go::Settings callsigned_settings() {
    go::Settings settings = go::defaults();
    std::memcpy(settings.callsign, "ABCDEFGHI", 10);
    return settings;
}

}  // namespace

// The push races the MTU exchange at every connect, so a refusal there would be noise.
TEST_CASE("comms: an update pushed at link-up that does not fit is counted, never refused") {
    platform::host::Link link;
    link.declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    link.raise_link(1);
    go::Settings settings = callsigned_settings();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_image_state(dfu::ImageState::Probation,
                           dfu::UpdateRecord{dfu::kWidestVersion, dfu::kWidestVersion});
    config.on_link_up(events::LinkUp{1, link.payload_bytes()});
    CHECK(link.sent.empty());
    CHECK(config.link_drops() == 1);
}

TEST_CASE(
    "comms: a link that came up at the BLE minimum hears what fits, and small_mtu for what does "
    "not") {
    platform::host::Link link;
    link.raise_link(1);
    link.raise_link(1);
    link.declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    go::Settings settings = callsigned_settings();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);

    // The status and a refusal fit twenty bytes; the config reply is counted, never sent short.
    config.on_rx(config_frame("{\"command\":\"status\"}"));
    config.on_rx(config_frame("{\"command\":\"get\"}"));
    config.on_rx(config_frame("{\"command\":99}"));
    REQUIRE(link.sent.size() == 3);
    CHECK(link_text(link.sent[0].bytes).find("{\"command\":\"status\",") == 0);
    CHECK(link_text(link.sent[1].bytes) == kSmallMtuRefusal);
    CHECK(link.sent[1].bytes.size() == 8);
    CHECK(link_text(link.sent[2].bytes) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"unknown_command\"}");
    CHECK(config.link_drops() == 1);
    // The refusal happened above the port: nothing was ever handed down.
    CHECK(link.refused_oversize == 0);
}
