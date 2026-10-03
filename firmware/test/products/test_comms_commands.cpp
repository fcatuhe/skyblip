// The commands that take the device out of service, each behind a confirmation on the device:
// dfu with the upload window it opens and the install it ends in, recovery and power off.
#include <cstring>
#include <initializer_list>
#include <string>

#include "core/comms/config.h"
#include "core/events/link.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "ports/dfu.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/config_frame.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {
struct SpyDfu : ports::Dfu {
    int triggered = 0;
    int confirmed = 0;
    int recovery = 0;
    int forgotten = 0;
    bool staged = true;
    bool finished = false;
    ports::ImageVersion version{0, 2, 0, 15};
    void trigger() override { triggered++; }
    bool confirm() override {
        confirmed++;
        return true;
    }
    bool staged_version(ports::ImageVersion& out) override {
        out = version;
        return staged;
    }
    ports::RecoveryPath enter_recovery() override {
        recovery++;
        return ports::RecoveryPath::Rebooted;
    }
    bool upload_finished() override { return finished; }
    void forget_upload() override {
        forgotten++;
        finished = false;
    }
};
}  // namespace

constexpr const char* kDfu = "{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}";

ConfigService& upload_opened(ConfigService& cs, SpyDfu& dfu) {
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame(kDfu));
    cs.confirm();
    dfu.finished = true;
    return cs;
}

TEST_CASE("comms: dfu opens an upload window only after on-screen confirmation") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, &dfu);
    cs.set_flight_state(flight::FlightState::Ground);

    CHECK_FALSE(cs.upload_allowed());
    cs.on_rx(frame(kDfu));
    CHECK(cs.pending() == Pending::Dfu);
    CHECK_FALSE(cs.upload_allowed());  // a remote request alone authorises nothing

    cs.confirm();
    CHECK(cs.upload_allowed());
    CHECK(cs.receiving_firmware());
    CHECK(dfu.triggered == 0);
}

// The press is the whole authorisation, so the glass names what it authorises.
TEST_CASE("comms: the firmware prompt names the version the phone is about to send") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame(kDfu));
    REQUIRE(cs.pending() == Pending::Dfu);
    CHECK(std::string(cs.prompt_detail()) == "INSTALL 0.2.0+15 FROM THE PHONE");

    cs.on_rx(frame("{\"cmd\":\"power_off\"}"));
    CHECK(std::string(cs.prompt_detail()) == pending_detail(Pending::PowerOff));
}

TEST_CASE("comms: a dfu that does not name its version is refused at the door") {
    for (const char* json : {"{\"cmd\":\"dfu\"}", "{\"cmd\":\"dfu\",\"version\":\"0.2\"}"}) {
        platform::host::Link link;
        link.raise_link(1);
        go::Settings s = go::defaults();
        go::SettingsStore store_cs(s, kTestAddr);
        ConfigService cs(link, store_cs);
        cs.set_flight_state(flight::FlightState::Ground);
        cs.on_rx(frame(json));
        CHECK(cs.pending() == Pending::None);
        CHECK(link.last().bytes.find("no_version") != std::string::npos);
    }
}

// The swap is not started from here: the product parks the radio and paints the
// panel first, so a landed upload is a latch the sequencer spends.
TEST_CASE("comms: an upload that lands whole latches the install with no second prompt") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, &dfu);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame(kDfu));
    cs.confirm();
    cs.tick(100);
    CHECK_FALSE(cs.install_requested());

    dfu.finished = true;
    cs.tick(200);
    CHECK(cs.pending() == Pending::None);
    CHECK(cs.install_requested());
    CHECK_FALSE(cs.receiving_firmware());
    CHECK(dfu.triggered == 0);
    CHECK(link.last().bytes.find("\"ack\":true,\"reason\":\"install\"") != std::string::npos);

    cs.clear_install_request();
    cs.tick(300);
    CHECK_FALSE(cs.install_requested());
}

TEST_CASE("comms: an image other than the one the press named is refused, not installed") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    dfu.version = ports::ImageVersion{0, 3, 0, 1};
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, &dfu);
    upload_opened(cs, dfu).tick(100);
    CHECK_FALSE(cs.install_requested());
    CHECK_FALSE(cs.receiving_firmware());
    CHECK(link.last().bytes.find("not_approved") != std::string::npos);
}

TEST_CASE("comms: a landed upload whose slot cannot be read is refused, not rebooted into") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    dfu.staged = false;
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, &dfu);
    upload_opened(cs, dfu).tick(100);
    CHECK_FALSE(cs.install_requested());
    CHECK(link.last().bytes.find("nothing_staged") != std::string::npos);
}

TEST_CASE("comms: opening an upload window forgets the upload an earlier one finished") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    go::SettingsStore store_cs(s, kTestAddr);
    dfu.finished = true;
    ConfigService cs(link, store_cs, &dfu);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame(kDfu));
    CHECK(dfu.forgotten == 0);
    cs.confirm();
    CHECK(dfu.forgotten == 1);

    cs.tick(100);
    CHECK(cs.receiving_firmware());
    CHECK_FALSE(cs.install_requested());
}

TEST_CASE("comms: apply is no longer a command") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, &dfu);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"apply\"}"));
    CHECK(cs.pending() == Pending::None);
    CHECK_FALSE(cs.install_requested());
    CHECK(link.last().bytes.find("unknown_cmd") != std::string::npos);
}

TEST_CASE("comms: dfu is refused at the door on a critical cell") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, &dfu);
    cs.set_flight_state(flight::FlightState::Ground);
    power::BatteryState low{};
    low.valid = true;
    low.millivolts = 3400;
    cs.set_battery_state(low, power::PowerLevel::Critical);
    REQUIRE_FALSE(cs.swap_powered());

    cs.on_rx(frame(kDfu));
    CHECK(cs.pending() == Pending::None);
    CHECK(link.last().bytes.find("low_power") != std::string::npos);
    CHECK_FALSE(cs.upload_allowed());
}

// The upload takes minutes, and a cell can cross the warning inside them.
TEST_CASE("comms: a cell that turns critical during the upload refuses the swap") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, &dfu);
    power::BatteryState healthy{};
    healthy.valid = true;
    healthy.millivolts = 4000;
    cs.set_battery_state(healthy, power::PowerLevel::Normal);
    upload_opened(cs, dfu);

    cs.set_supply_warned(true);
    cs.tick(100);
    CHECK_FALSE(cs.install_requested());
    CHECK_FALSE(cs.receiving_firmware());
    CHECK(link.last().bytes.find("low_power") != std::string::npos);

    // A power-off is not a swap: the same cell is allowed to switch the device off.
    cs.on_rx(frame("{\"cmd\":\"power_off\"}"));
    REQUIRE(cs.pending() == Pending::PowerOff);
    cs.confirm();
    CHECK(cs.power_off_requested());
}

TEST_CASE("comms: a landed upload on a device no longer proven on the ground is not installed") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, &dfu);
    upload_opened(cs, dfu);
    cs.set_flight_state(flight::FlightState::Unknown);
    cs.tick(100);
    CHECK_FALSE(cs.install_requested());
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);
}

TEST_CASE("comms: the update question names the image state and the versions of the attempt") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.on_rx(frame("{\"cmd\":\"update\"}"));
    CHECK(link.last().bytes.find("\"image\":\"confirmed\"") != std::string::npos);
    CHECK(link.last().bytes.find("\"from\"") == std::string::npos);

    dfu::UpdateRecord record;
    record.from = ports::ImageVersion{0, 1, 0, 12};
    record.to = ports::ImageVersion{0, 2, 0, 15};
    cs.set_image_state(dfu::ImageState::Reverted, record);
    cs.on_rx(frame("{\"cmd\":\"update\"}"));
    CHECK(link.last().bytes.find("\"image\":\"reverted\"") != std::string::npos);
    CHECK(link.last().bytes.find("\"from\":\"0.1.0+12\"") != std::string::npos);
    CHECK(link.last().bytes.find("\"to\":\"0.2.0+15\"") != std::string::npos);
    CHECK(link.last().bytes.find("\"swap_powered\":true") != std::string::npos);
}

// A phone that connects to a device whose last update did not take is told so
// before it asks, because it is the one fact the pilot would otherwise discover
// from a version number that did not change.
TEST_CASE(
    "comms: a link that comes up on an unconfirmed or reverted image is told without asking") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.on_link_up(events::LinkUp{1, 244});
    CHECK(link.sent.empty());

    cs.set_image_state(dfu::ImageState::Probation, dfu::UpdateRecord{});
    link.raise_link(2);
    cs.on_link_up(events::LinkUp{2, 244});
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last().bytes.find("\"cmd\":\"update\"") != std::string::npos);
    CHECK(link.last().bytes.find("\"image\":\"probation\"") != std::string::npos);
}

TEST_CASE("comms: a link that comes up on settings that fell back is told, on a confirmed image") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_settings_fallback(settings::Fallback::Defaults);
    cs.on_link_up(events::LinkUp{1, 244});
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last().bytes.find("\"image\":\"confirmed\"") != std::string::npos);
    CHECK(link.last().bytes.find("\"settings\":\"defaults\"") != std::string::npos);
    CHECK(link.last().bytes.find("\"from\"") == std::string::npos);
}

// The product paints the recovery page first, so a confirmed recovery is a latch the sequencer
// spends.
TEST_CASE("comms: recovery routed through confirmation, latched and never entered inline") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, &dfu);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"recovery\"}"));
    CHECK(cs.pending() == Pending::Recovery);
    CHECK_FALSE(cs.recovery_requested());
    cs.confirm();
    CHECK(cs.recovery_requested());
    CHECK(dfu.recovery == 0);
    CHECK_FALSE(cs.power_off_requested());
    CHECK(link.last().bytes.find("\"reason\":\"recovery\"") != std::string::npos);

    cs.clear_recovery_request();
    CHECK_FALSE(cs.recovery_requested());
}

TEST_CASE("comms: recovery refused in flight") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, &dfu);
    cs.set_flight_state(flight::FlightState::Airborne);
    cs.on_rx(frame("{\"cmd\":\"recovery\"}"));
    CHECK(cs.pending() == Pending::None);
    CHECK(dfu.recovery == 0);
}

// The third way a device is asked to go dark (core/power/shutdown.h:
// ShutdownReason::LinkRequest). The button and the cutoff already had callers;
// this is the companion link's, behind the same gate as dfu and recovery.
TEST_CASE("comms: power_off is confirmed on the device, then latched for the sequencer") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);

    cs.on_rx(frame("{\"cmd\":\"power_off\"}"));
    CHECK(cs.pending() == Pending::PowerOff);
    CHECK_FALSE(cs.power_off_requested());  // a remote request alone turns nothing off
    CHECK(link.last().bytes.find("confirm_power_off") != std::string::npos);

    cs.confirm();
    CHECK(cs.pending() == Pending::None);
    CHECK(cs.power_off_requested());
    CHECK(link.last().bytes.find("\"ack\":true") != std::string::npos);

    cs.clear_power_off_request();
    CHECK_FALSE(cs.power_off_requested());
}

TEST_CASE("comms: power_off refused in flight") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Airborne);
    cs.on_rx(frame("{\"cmd\":\"power_off\"}"));
    CHECK(cs.pending() == Pending::None);
    CHECK_FALSE(cs.power_off_requested());
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);

    // And a confirmation that arrives after takeoff does not turn it off either.
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"power_off\"}"));
    REQUIRE(cs.pending() == Pending::PowerOff);
    cs.set_flight_state(flight::FlightState::Airborne);
    cs.confirm();
    CHECK_FALSE(cs.power_off_requested());
}

// D5: the reset reason was read at boot and shown on the self-test page, and a
// device in a field with a phone next to it has no self-test page in view.
// G6's plug-in-and-read: the same on_rx dispatch that answers "status"
// answers "timing" from whatever core/timing::SlotTimingStats the device has
// been accumulating - no second channel, no panel real estate a bucket array
// would not fit on anyway.
// Fail closed: takeoff must revoke an authorisation granted on the ground, or a
// long upload could still be running when the aircraft leaves.
TEST_CASE("comms: takeoff closes an open upload window and it stays latched") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    cs.confirm();
    REQUIRE(cs.upload_allowed());

    cs.set_flight_state(flight::FlightState::Airborne);
    CHECK_FALSE(cs.upload_allowed());

    // An "Unknown" reading after takeoff must not read as permission.
    cs.set_flight_state(flight::FlightState::Unknown);
    CHECK_FALSE(cs.upload_allowed());
}

TEST_CASE("comms: upload window expires") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.tick(1000);
    cs.on_rx(frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    cs.confirm();
    REQUIRE(cs.upload_allowed());

    cs.tick(1000 + 9u * 60u * 1000u);
    CHECK(cs.upload_allowed());
    cs.tick(1000 + 11u * 60u * 1000u);
    CHECK_FALSE(cs.upload_allowed());
}

// M. The two windows this service holds, across the 49.7-day wrap of
// ports::Clock::millis(). A ten-minute upload window that never closes is a device
// that will take firmware from a phone for seven weeks; a thirty-second prompt
// that expires the instant it is raised cannot be answered at all. Both are
// unsigned differences from a stamp guarded by a flag, and this is what says so.
TEST_CASE("comms: the upload and confirmation windows span the 49.7-day wrap") {
    const uint32_t before = 0xFFFFFF00u;  // 256 ms short of the wrap

    platform::host::Link link;

    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.tick(before);
    cs.on_rx(frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    cs.confirm();
    REQUIRE(cs.upload_allowed());

    cs.tick(before + 9u * 60u * 1000u);  // nine minutes later, past the wrap
    CHECK(cs.upload_allowed());
    cs.tick(before + 11u * 60u * 1000u);
    CHECK_FALSE(cs.upload_allowed());

    // The prompt, on the same clock. Raised before the wrap, still standing after
    // it, and expired thirty seconds after it was raised.
    platform::host::Link second_link;
    second_link.raise_link(1);
    go::SettingsStore store_prompt(s, kTestAddr);
    ConfigService prompt(second_link, store_prompt);
    prompt.set_flight_state(flight::FlightState::Ground);
    prompt.tick(before);
    prompt.on_rx(frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    REQUIRE(prompt.pending() == Pending::Dfu);
    prompt.tick(before + kConfirmWindowMs - 1u);
    CHECK(prompt.pending() == Pending::Dfu);
    prompt.tick(before + kConfirmWindowMs);
    CHECK(prompt.pending() == Pending::None);
    CHECK(second_link.last().bytes.find("expired") != std::string::npos);
}

TEST_CASE("comms: disconnect closes the upload window") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    cs.confirm();
    REQUIRE(cs.upload_allowed());

    events::LinkDown down{1};
    cs.on_link_down(down);
    CHECK_FALSE(cs.upload_allowed());
}

TEST_CASE("comms: DFU refused in flight") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    SpyDfu dfu;
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, &dfu);
    cs.set_flight_state(flight::FlightState::Airborne);
    cs.on_rx(frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    CHECK(cs.pending() == Pending::None);
    CHECK(dfu.triggered == 0);
}
