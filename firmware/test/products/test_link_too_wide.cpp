// What a link too narrow for a reply hears, and what each connection leaves behind to measure it.
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

events::LinkDown ended(platform::host::Link& link, uint16_t session_id) {
    events::LinkEvent event{};
    while (link.pop_event(event)) {
    }
    link.drop_link(session_id);
    REQUIRE(link.pop_event(event));
    REQUIRE(event.type == events::LinkEventType::Down);
    return events::LinkDown{event.session_id, event.payload_bytes, event.exchange,
                            event.replies_too_wide};
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

TEST_CASE("comms: each small_mtu is counted against the connection it answered") {
    platform::host::Link link;
    link.declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    link.raise_link(1);
    link.raise_link(2);
    go::Settings settings = callsigned_settings();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);

    config.on_rx(config_frame("{\"command\":\"get\"}"));
    config.on_rx(config_frame("{\"command\":\"get\"}"));
    REQUIRE(link.sent.size() == 2);
    CHECK(link_text(link.last().bytes) == kSmallMtuRefusal);

    const events::LinkDown holder = ended(link, 1);
    CHECK(holder.replies_too_wide == 2);
    CHECK(holder.payload_bytes == ports::kMinimumLinkPayloadBytes);
    CHECK(ended(link, 2).replies_too_wide == 0);
    config.on_link_down(holder);
    CHECK(config.diagnostics().replies_too_wide == 2);
}

TEST_CASE("comms: the dump counts how each connection that ended exchanged its MTU") {
    platform::host::Link link;
    link.declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    for (uint16_t session = 1; session <= 3; session++) link.raise_link(session);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);

    link.declare_payload_bytes(1, 182);
    link.exchange_mtu_by_unit(2, 244);
    link.settle_mtu_request(3, events::MtuRequest::Refused, 0x0E);
    for (uint16_t session = 1; session <= 3; session++) config.on_link_down(ended(link, session));

    const Diagnostics& counted = config.diagnostics();
    CHECK(counted.mtu_exchanged_by_central == 1);
    CHECK(counted.mtu_exchanged_by_unit == 1);
    CHECK(counted.mtu_never_exchanged == 1);
    CHECK(counted.mtu_requests_failed == 1);
    CHECK(counted.replies_too_wide == 0);
}
