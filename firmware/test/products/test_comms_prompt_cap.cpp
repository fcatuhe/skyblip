// One prompt at a time, thirty seconds at most, and none after the pilot refused one on that link.
#include <string>

#include "core/comms/config.h"
#include "core/events/link.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/config_frame.h"
#include "test/support/product_rig.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {

constexpr const char* kSetType = "{\"command\":\"set\",\"aircraft_type\":4}";

constexpr const char* kPromptCommands[] = {"{\"command\":\"set\",\"aircraft_type\":7}",
                                           "{\"command\":\"set\"}",
                                           "{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}",
                                           "{\"command\":\"recovery\"}",
                                           "{\"command\":\"power_off\"}",
                                           "{\"command\":\"gnss_cold_start\"}"};

std::string refusal(const char* word) {
    return std::string("{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"") +
           word + "\"}";
}

struct Desk {
    platform::host::Link link;
    go::Settings settings = go::defaults();
    go::SettingsStore store{settings, kTestActivation};
    ConfigService config{link, store};
    uint16_t session_id{1};

    explicit Desk(flight::FlightState state = flight::FlightState::Ground) {
        link.raise_link(session_id);
        config.on_link_up(events::LinkUp{session_id, ports::kMinimumLinkPayloadBytes});
        config.set_flight_state(state);
        config.tick(1000);
    }

    std::string say(const char* json) {
        config.on_rx(link_frame(events::Endpoint::Config, session_id, json));
        return last();
    }

    std::string erase() {
        config.request_log_erase();
        return last();
    }

    void reconnect() {
        link.drop_link(session_id);
        config.on_link_down(events::LinkDown{session_id});
        session_id++;
        link.raise_link(session_id);
        config.on_link_up(events::LinkUp{session_id, ports::kMinimumLinkPayloadBytes});
    }

    std::string last() { return link_text(link.last().bytes); }
};

}  // namespace

// The link has no pairing, so a stranger re-sending a set must not hold the glass and the controls.
TEST_CASE("comms prompt cap: a no-op set every 20 s keeps no prompt past 30 s from its staging") {
    for (const flight::FlightState state :
         {flight::FlightState::Ground, flight::FlightState::Unknown}) {
        CAPTURE(static_cast<int>(state));
        Desk desk(state);
        desk.say(kSetType);
        REQUIRE(desk.config.pending() == Pending::Set);

        desk.config.tick(21000);
        CHECK(desk.say("{\"command\":\"set\"}") == refusal("prompt_pending"));
        desk.config.tick(1000 + kPromptCeilingMs - 1);
        REQUIRE(desk.config.pending() == Pending::Set);

        desk.config.tick(1000 + kPromptCeilingMs);
        CHECK(desk.config.pending() == Pending::None);
        CHECK(desk.last() == refusal("expired"));
    }
}

TEST_CASE("comms prompt cap: a prompt command while a prompt stands leaves that prompt untouched") {
    Desk desk;
    desk.say(kSetType);
    REQUIRE(desk.config.pending() == Pending::Set);
    const std::string detail = desk.config.prompt_detail();

    desk.config.tick(11000);
    for (const char* command : kPromptCommands) {
        CAPTURE(command);
        CHECK(desk.say(command) == refusal("prompt_pending"));
    }
    CHECK(desk.erase() == refusal("prompt_pending"));
    CHECK(desk.config.pending() == Pending::Set);
    CHECK(desk.config.prompt_detail() == detail);

    desk.config.tick(1000 + kPromptCeilingMs - 1);
    desk.config.confirm();
    CHECK(int(desk.settings.aircraft_type) == 4);
    CHECK(desk.last() == "{\"command\":\"acknowledgement\",\"acknowledge\":true}");
}

TEST_CASE("comms prompt cap: a prompt that stands still expires 30 s after it was first staged") {
    Desk desk;
    desk.say("{\"command\":\"recovery\"}");
    REQUIRE(desk.config.pending() == Pending::Recovery);
    desk.config.tick(29000);
    CHECK(desk.say("{\"command\":\"recovery\"}") == refusal("prompt_pending"));

    desk.config.tick(1000 + kPromptCeilingMs);
    CHECK(desk.config.pending() == Pending::None);
    CHECK(desk.last() == refusal("expired"));
    desk.config.confirm();
    CHECK_FALSE(desk.config.recovery_requested());
}

// One press is the pilot saying no, and a sender that asks again at once is not taking it.
TEST_CASE("comms prompt cap: after one press refuses, that link raises no prompt until it drops") {
    Desk desk;
    desk.say(kSetType);
    desk.config.cancel();
    REQUIRE(desk.last() == refusal("cancelled"));

    desk.config.tick(1000 + 10u * kPromptCeilingMs);
    for (const char* command : kPromptCommands) {
        CAPTURE(command);
        CHECK(desk.say(command) == refusal("refused_on_device"));
        CHECK(desk.config.pending() == Pending::None);
    }
    CHECK(desk.erase() == refusal("refused_on_device"));
    CHECK(desk.say("{\"command\":\"get\"}").find("\"command\":\"config\"") != std::string::npos);
    CHECK(int(desk.settings.aircraft_type) == 1);

    desk.reconnect();
    CHECK(desk.say(kSetType).find("\"pending\":true") != std::string::npos);
    CHECK(desk.config.pending() == Pending::Set);
}

TEST_CASE("comms prompt cap: an expiry, a take-off and a dropped link start no refusal period") {
    SUBCASE("expiry") {
        Desk desk;
        desk.say(kSetType);
        desk.config.tick(1000 + kPromptCeilingMs);
        REQUIRE(desk.last() == refusal("expired"));
        CHECK(desk.say(kSetType).find("\"pending\":true") != std::string::npos);
    }
    SUBCASE("take-off") {
        Desk desk;
        desk.say(kSetType);
        desk.config.set_flight_state(flight::FlightState::Airborne);
        REQUIRE(desk.config.pending() == Pending::None);
        desk.config.set_flight_state(flight::FlightState::Ground);
        CHECK(desk.say(kSetType).find("\"pending\":true") != std::string::npos);
    }
    SUBCASE("dropped link") {
        Desk desk;
        desk.say(kSetType);
        desk.reconnect();
        REQUIRE(desk.config.pending() == Pending::None);
        CHECK(desk.say(kSetType).find("\"pending\":true") != std::string::npos);
    }
}

// The press that stops an upload answers no prompt: the pilot authorised that upload.
TEST_CASE("comms prompt cap: one press on a running upload leaves the next install free to ask") {
    Desk desk;
    desk.say("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}");
    desk.config.confirm();
    REQUIRE(desk.config.receiving_firmware());
    desk.config.cancel();
    REQUIRE_FALSE(desk.config.receiving_firmware());
    CHECK(desk.say("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}").find("confirm_dfu") !=
          std::string::npos);
}

// The update page asks one question at a time, and each answer opens the next.
TEST_CASE("comms prompt cap: the update page's settings, install and retry run on one link") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.seconds(now_ms, 20, 0, 300);
    ConfigService& config = rig.product.config_link().config();
    REQUIRE(config.flight_state() == flight::FlightState::Ground);

    for (const char* callsign : {"F-JABC", "F-JXYZ"}) {
        const std::string set =
            std::string("{\"command\":\"set\",\"callsign\":\"") + callsign + "\"}";
        rig.send(set.c_str());
        rig.run(now_ms, now_ms + 3000);
        now_ms += 3000;
        REQUIRE(config.pending() == Pending::Set);
        rig.double_press(now_ms);
        rig.run(now_ms, now_ms + 1000);
        now_ms += 1000;
        CHECK(std::string(rig.settings().callsign) == callsign);
    }

    rig.send("{\"command\":\"recovery\"}");
    rig.run(now_ms, now_ms + kPromptCeilingMs + 1000);
    now_ms += kPromptCeilingMs + 1000;
    REQUIRE(rig.last_on(events::Endpoint::Config) == refusal("expired"));

    rig.send("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    REQUIRE(config.pending() == Pending::Dfu);
    rig.double_press(now_ms);
    rig.run(now_ms, now_ms + 1000);
    CHECK(config.receiving_firmware());
}

TEST_CASE("comms prompt cap: on the glass, one press refuses and a new connection asks again") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    rig.seconds(now_ms, 20, 0, 300);
    ConfigService& config = rig.product.config_link().config();

    rig.send(kSetType);
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    REQUIRE(rig.product.screen().prompt() == Pending::Set);
    rig.press(now_ms);
    rig.run(now_ms, now_ms + 1000);
    now_ms += 1000;
    REQUIRE(rig.last_on(events::Endpoint::Config) == refusal("cancelled"));

    rig.send(kSetType);
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    CHECK(rig.last_on(events::Endpoint::Config) == refusal("refused_on_device"));
    CHECK(rig.product.screen().prompt() == Pending::None);

    rig.drop_link();
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    rig.raise_link(2);
    rig.send_from(2, kSetType);
    rig.run(now_ms, now_ms + 3000);
    CHECK(rig.product.screen().prompt() == Pending::Set);
    CHECK(config.claim_holder() == 2);
}
