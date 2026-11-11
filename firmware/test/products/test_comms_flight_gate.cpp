// What the flight state gates on the config endpoint: Airborne refuses, and the ground or no fix
// since boot stages.
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "core/events/link.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "ports/dfu.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/config_frame.h"
#include "test/support/screen_rig.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {

constexpr const char* kInFlight =
    "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"in_flight\"}";

constexpr flight::FlightState kNotAirborne[] = {flight::FlightState::Ground,
                                                flight::FlightState::Unknown};

constexpr const char* kPromptRequests[] = {
    "{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}", "{\"command\":\"recovery\"}",
    "{\"command\":\"power_off\"}", "{\"command\":\"gnss_cold_start\"}"};

struct Bench {
    platform::host::Link link;
    go::Settings settings = go::defaults();
    go::SettingsStore store{settings, kTestActivation};
    ConfigService config{link, store};

    explicit Bench(flight::FlightState state) {
        link.raise_link(1);
        config.set_flight_state(state);
    }

    std::string last() { return link_text(link.last().bytes); }
};

struct FinishedUpload : ports::Dfu {
    ports::ImageVersion version{0, 2, 0, 15};
    void trigger() override {}
    bool confirm() override { return true; }
    bool staged_version(ports::ImageVersion& out) override {
        out = version;
        return true;
    }
    ports::RecoveryPath enter_recovery() override { return ports::RecoveryPath::Rebooted; }
    bool upload_finished() override { return true; }
    void forget_upload() override {}
};

}  // namespace

// Two presses on the device stand in for pairing, and Unknown is a unit with no fix since boot.
TEST_CASE("comms flight gate: a set with no fix since boot is staged, confirmed and stored") {
    Bench bench(flight::FlightState::Unknown);
    bench.config.on_rx(config_frame("{\"command\":\"set\",\"aircraft_type\":4}"));
    REQUIRE(bench.config.pending() == Pending::Set);
    CHECK(int(bench.settings.aircraft_type) == 1);

    bench.config.confirm();
    CHECK(int(bench.settings.aircraft_type) == 4);
    CHECK(bench.config.settings_dirty());
    CHECK(bench.last() == "{\"command\":\"acknowledgement\",\"acknowledge\":true}");
}

// A prompt takes the glass and both controls, so an unauthenticated link must never raise one
// aloft.
TEST_CASE("comms flight gate: a set in flight is refused in_flight and raises no prompt") {
    Bench bench(flight::FlightState::Airborne);
    bench.config.on_rx(config_frame("{\"command\":\"set\",\"alarm_volume\":1}"));
    CHECK(bench.config.pending() == Pending::None);
    CHECK(bench.last() == kInFlight);
    bench.config.confirm();
    CHECK(bench.settings.alarm_volume == go::AlarmVolume::Loud);
}

TEST_CASE("comms flight gate: a take-off cancels a staged set, from the ground or with no fix") {
    for (const flight::FlightState before :
         {flight::FlightState::Ground, flight::FlightState::Unknown}) {
        Bench bench(before);
        bench.config.on_rx(config_frame("{\"command\":\"set\",\"alarm_volume\":1}"));
        REQUIRE(bench.config.pending() == Pending::Set);

        bench.config.set_flight_state(flight::FlightState::Airborne);
        CHECK(bench.config.pending() == Pending::None);
        CHECK(bench.last() == kInFlight);
        bench.config.confirm();
        CHECK(bench.settings.alarm_volume == go::AlarmVolume::Loud);
    }
}

TEST_CASE(
    "comms flight gate: a set carrying a bench trim is staged with no fix, and refused aloft") {
    for (const char* set : {"{\"command\":\"set\",\"battery_offset_mv\":40}",
                            "{\"command\":\"set\",\"frequency_trim_e1_ppm\":-25}"}) {
        Bench grounded(flight::FlightState::Unknown);
        grounded.config.on_rx(config_frame(set));
        CHECK(grounded.config.pending() == Pending::Set);

        Bench aloft(flight::FlightState::Airborne);
        aloft.config.on_rx(config_frame(set));
        CHECK(aloft.config.pending() == Pending::None);
        CHECK(aloft.last() == kInFlight);
    }
}

// A fix lost on the ground is no take-off, so the prompt stands and the press confirms it.
TEST_CASE(
    "comms flight gate: a set staged on the ground is stored at the press once the fix is lost") {
    Bench bench(flight::FlightState::Ground);
    bench.config.on_rx(config_frame("{\"command\":\"set\",\"battery_offset_mv\":40}"));
    REQUIRE(bench.config.pending() == Pending::Set);
    bench.config.set_flight_state(flight::FlightState::Unknown);
    bench.config.confirm();
    CHECK(bench.settings.battery_offset_mv == 40);
    CHECK(bench.last() == "{\"command\":\"acknowledgement\",\"acknowledge\":true}");
}

TEST_CASE(
    "comms flight gate: install, recovery, power-off, cold start and erase are refused aloft") {
    for (const char* request : kPromptRequests) {
        Bench bench(flight::FlightState::Airborne);
        bench.config.on_rx(config_frame(request));
        CHECK(bench.config.pending() == Pending::None);
        CHECK(bench.last() == kInFlight);
    }
    Bench erase(flight::FlightState::Airborne);
    REQUIRE(erase.config.claim_link(events::Peer{1, kFirstConnection}));
    erase.config.request_log_erase();
    CHECK(erase.config.pending() == Pending::None);
    CHECK(erase.last() == kInFlight);
}

// Unknown is a unit with no fix since boot: it sends no position and grades no traffic.
TEST_CASE(
    "comms flight gate: install, recovery, power-off, cold start and erase ask on the ground or "
    "with no fix") {
    for (const flight::FlightState state : kNotAirborne) {
        for (const char* request : kPromptRequests) {
            Bench bench(state);
            bench.config.on_rx(config_frame(request));
            CHECK(bench.config.pending() != Pending::None);
        }
        Bench erase(state);
        REQUIRE(erase.config.claim_link(events::Peer{1, kFirstConnection}));
        erase.config.request_log_erase();
        CHECK(erase.config.pending() == Pending::EraseLog);
    }
}

TEST_CASE(
    "comms flight gate: an upload finished after the fix is lost installs, and a take-off shuts "
    "it") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    FinishedUpload dfu;
    ConfigService config(link, store, &dfu);
    config.on_rx(config_frame("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    config.confirm();
    REQUIRE(config.receiving_firmware());
    config.set_flight_state(flight::FlightState::Airborne);
    CHECK_FALSE(config.upload_allowed());
    config.tick(100);
    CHECK_FALSE(config.install_requested());

    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    config.confirm();
    config.set_flight_state(flight::FlightState::Unknown);
    CHECK(config.upload_allowed());
    config.tick(200);
    CHECK(config.install_requested());
    CHECK(link_text(link.last().bytes) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":true,\"reason\":\"install\"}");
}

// A twin unit nobody sent a set to is what the glass must still show, byte for byte.
TEST_CASE("comms flight gate: a set in flight leaves the radar and an advisory on the glass") {
    ScreenRig sent;
    ScreenRig untouched;
    uint32_t now_ms = 0;
    for (ScreenRig* rig : {&sent, &untouched}) {
        uint32_t at_ms = 0;
        rig->config.set_flight_state(flight::FlightState::Airborne);
        rig->run_seconds(at_ms, 3);
        rig->threat(traffic::Level::Advisory, 200, 100, at_ms);
        rig->run_seconds(at_ms, 2);
        now_ms = at_ms;
    }

    for (int i = 0; i < 6; i++) {
        sent.config.on_rx(config_frame("{\"command\":\"set\",\"units\":1}"));
        sent.config.on_rx(config_frame("{\"command\":\"set\"}"));
        uint32_t at_ms = now_ms;
        sent.run_seconds(at_ms, 20);
        untouched.run_seconds(now_ms, 20);
        CHECK(sent.config.pending() == comms::Pending::None);
        CHECK(sent.screen.prompt() == comms::Pending::None);
    }
    REQUIRE(sent.state.alarm_undismissed == traffic::Level::Advisory);
    CHECK(sent.screen.page() == go::Page::Radar);
    CHECK(std::memcmp(sent.chip.framebuffer().data(), untouched.chip.framebuffer().data(),
                      go::Glass::kBytes) == 0);
    CHECK(sent.settings.units == go::Units::Nautical);
}
