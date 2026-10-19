// core/comms config state machine tested over a link model with scripted JSON
// messages, NO device. Covers get, set-with-confirmation,
// the in-flight lockout (fail closed) and backpressure.
//
// Two of these guard the wire the shell now provides: the gate is fed from the
// ADS-L flight code core/flight publishes, and a prompt standing on the panel
// is an open authorisation, so it has a life of its own that ends in a refusal.
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

TEST_CASE("comms: get returns current config on the Config endpoint") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame("{\"cmd\":\"get\"}"));
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last_on(events::Endpoint::Config));
    CHECK(link.last().bytes.find("config") != std::string::npos);
}

TEST_CASE("comms: defaults answers what a new unit ships on, and sent back as a set restores it") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    settings.aircraft_type = 7;
    settings.alarm_volume = go::AlarmVolume::Soft;
    std::memcpy(settings.callsign, "D-KXYZ", 7);
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);

    config.on_rx(config_frame("{\"cmd\":\"defaults\"}"));
    const std::string body = link.last().bytes;
    CHECK(body ==
          "{\"cmd\":\"defaults\",\"aircraft_type\":1,\"alarm\":true,\"alarm_volume\":2,"
          "\"units\":0,\"callsign\":\"\"}");

    const std::string set = "{\"cmd\":\"set\"" + body.substr(body.find(','));
    config.on_rx(config_frame(set.c_str()));
    config.confirm();
    const go::Settings shipped = go::defaults();
    CHECK(settings.aircraft_type == shipped.aircraft_type);
    CHECK(settings.alarm_volume == shipped.alarm_volume);
    CHECK(std::string(settings.callsign).empty());
}

TEST_CASE("comms: set on the ground stages, needs confirmation, then applies") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);

    config.on_rx(config_frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    CHECK(config.pending() == Pending::Set);
    CHECK(link.last().bytes.find("confirm") != std::string::npos);
    CHECK(int(settings.aircraft_type) == 1);  // not yet applied

    config.confirm();
    CHECK(config.pending() == Pending::None);
    CHECK(int(settings.aircraft_type) == 4);  // applied
    CHECK(config.settings_dirty());
    CHECK(link.last().bytes.find("\"ack\":true") != std::string::npos);
}

// The press authorises what the glass says, so the glass says what the phone sent.
TEST_CASE("comms: the settings prompt lists what the set changes, and only that") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);

    config.on_rx(
        config_frame("{\"cmd\":\"set\",\"callsign\":\"F-JXYZ\",\"units\":0,\"alarm_volume\":1}"));
    REQUIRE(config.pending() == Pending::Set);
    CHECK(std::string(config.prompt_detail()) == "CALLSIGN F-JXYZ\nVOLUME SOFT");

    config.on_rx(config_frame("{\"cmd\":\"set\",\"units\":0}"));
    CHECK(std::string(config.prompt_detail()) == pending_detail(Pending::Set));
}

TEST_CASE("comms: a set that fills the whole frame is staged to its last byte") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);

    const std::string head = "{\"cmd\":\"set\",";
    const std::string tail = "\"aircraft_type\":4}";
    const std::string full =
        head + std::string(events::RxFrame{}.data.size() - head.size() - tail.size(), ' ') + tail;
    config.on_rx(config_frame(full.c_str()));
    REQUIRE(config.pending() == Pending::Set);
    CHECK(std::string(config.pending_set_json()) == full);
}

TEST_CASE("comms: set is REFUSED in flight (fail closed), no staging") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Airborne);
    config.on_rx(config_frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    CHECK(config.pending() == Pending::None);
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);
    CHECK(int(settings.aircraft_type) == 1);
}

// The latch itself is core/flight/ground.h's; this gate refuses whatever is not a confirmed ground.
TEST_CASE("comms: unknown flight-state refuses") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Unknown);  // never confirmed on ground
    CHECK(config.flight_state() == flight::FlightState::Unknown);
    config.on_rx(config_frame("{\"cmd\":\"set\",\"alarm\":false}"));
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);

    config.set_flight_state(flight::FlightState::Ground);
    CHECK(config.flight_state() == flight::FlightState::Ground);
}

TEST_CASE("comms: confirm re-checks the gate, becoming airborne cancels apply") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    config.set_flight_state(flight::FlightState::Airborne);  // took off before confirming
    config.confirm();
    CHECK(int(settings.aircraft_type) == 1);  // NOT applied
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);
}

// Nothing used to set the gate, so it stayed Unknown and refused forever: green tests, dead device.
TEST_CASE("comms: only the ADS-L on-ground code is permission, every other value refuses") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::state_from(0));
    config.on_rx(config_frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    CHECK(config.pending() == Pending::None);
    config.set_flight_state(flight::state_from(1));
    config.on_rx(config_frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    CHECK(config.pending() == Pending::Dfu);
}

TEST_CASE("comms: a prompt nobody answers expires, and a later confirm grants nothing") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.tick(1000);

    config.on_rx(config_frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    REQUIRE(config.pending() == Pending::Dfu);
    config.tick(1000 + kPromptForgetMs - 1);
    CHECK(config.pending() == Pending::Dfu);

    config.tick(1000 + kPromptForgetMs);
    CHECK(config.pending() == Pending::None);
    CHECK(link.last().bytes.find("expired") != std::string::npos);

    // The button pressed after the prompt came down authorises the operation
    // that was on it, or it authorises nothing. It is nothing.
    config.confirm();
    CHECK_FALSE(config.upload_allowed());
}

TEST_CASE("comms: taking off takes a standing prompt away with it") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame("{\"cmd\":\"recovery\"}"));
    REQUIRE(config.pending() == Pending::Recovery);

    config.set_flight_state(flight::FlightState::Airborne);
    CHECK(config.pending() == Pending::None);
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);
}

// The prompt is the whole security boundary, so it has to say what it is: a
// panel that shows an unlabelled question is a panel a pilot answers blind.
TEST_CASE("comms: every operation that needs authorising names itself and what it will do") {
    const Pending all[] = {Pending::Set, Pending::Dfu, Pending::Recovery, Pending::PowerOff};
    for (Pending p : all) {
        CHECK(std::strlen(pending_title(p)) > 0);
        CHECK(std::strlen(pending_detail(p)) > 8);
    }
    // No two operations wear the same title, or confirming one would look like
    // confirming another.
    for (Pending a : all)
        for (Pending b : all)
            if (a != b) CHECK(std::strcmp(pending_title(a), pending_title(b)) != 0);

    CHECK(std::strlen(pending_title(Pending::None)) == 0);
}

TEST_CASE("comms: link down cancels a pending change") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame("{\"cmd\":\"set\",\"alarm\":false}"));
    CHECK(config.pending() == Pending::Set);
    config.on_link_down(events::LinkDown{1});
    CHECK(config.pending() == Pending::None);
}

// G1: the wire, not the gauge or the cutoff rule - core/power decided percent,
// charging and the level, comms only carries them to the tablet.

// E1. A dying cell must not corrupt the settings. NVS survives an interrupted
// write by design, but the sector it garbage-collects is the internal flash the
// running image executes from, and the moment a write lands is the moment a
// 14 dBm burst sags a 3.3 V cell. So below the low-battery warning the sector is
// not touched - and a companion app that patches a value per keystroke is told
// so, rather than being acknowledged for a write that will not happen.

TEST_CASE("comms: a set is refused on a critical cell, with the reason") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);

    power::BatteryState low{};
    low.valid = true;
    low.cell_mv = 3400;
    config.set_battery_state(low, power::PowerLevel::Critical);
    CHECK_FALSE(config.settings_writable());

    config.on_rx(config_frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    // Refused at the door: no prompt to walk over and confirm for a write that
    // was never going to happen.
    CHECK(config.pending() == Pending::None);
    CHECK(link.last().bytes.find("low_power") != std::string::npos);
    CHECK(link.last().bytes.find("\"ack\":false") != std::string::npos);
    CHECK(int(settings.aircraft_type) == 1);
    CHECK_FALSE(config.settings_dirty());

    // A charger on the cable holds the terminal above the cell, so core/power
    // reports Normal again and the door opens.
    power::BatteryState charging{};
    charging.valid = true;
    charging.cell_mv = 3400;
    charging.external_power = true;
    charging.charging = true;
    config.set_battery_state(charging, power::PowerLevel::Normal);
    CHECK(config.settings_writable());
    config.on_rx(config_frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    REQUIRE(config.pending() == Pending::Set);
    config.confirm();
    CHECK(int(settings.aircraft_type) == 4);
}

// The confirmation window is 30 s wide and a cell can cross the warning inside
// it. The gate is therefore asked twice, exactly as the flight-state gate is.
TEST_CASE("comms: a cell that falls while the prompt stands cancels the change") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    REQUIRE(config.pending() == Pending::Set);

    power::BatteryState low{};
    low.valid = true;
    low.cell_mv = 3100;
    config.set_battery_state(low, power::PowerLevel::Flat);
    config.confirm();
    CHECK(config.pending() == Pending::None);
    CHECK(int(settings.aircraft_type) == 1);
    CHECK_FALSE(config.settings_dirty());
    CHECK(link.last().bytes.find("low_power") != std::string::npos);
}

// The power-failure comparator watches the SoC's own rail, which is at or below
// the cell: once it has fired, a healthy-looking divider reading is not the
// question any more.
TEST_CASE("comms: a fired power-failure comparator closes the door on its own") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    power::BatteryState healthy{};
    healthy.valid = true;
    healthy.cell_mv = 4000;
    config.set_battery_state(healthy, power::PowerLevel::Normal);
    REQUIRE(config.settings_writable());

    config.set_supply_warned(true);
    CHECK_FALSE(config.settings_writable());
    config.on_rx(config_frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    CHECK(config.pending() == Pending::None);
    CHECK(link.last().bytes.find("low_power") != std::string::npos);
}

// J. Die temperature in the status reply. The question it answers is one no other
// number on the device can: was this unit cooking. A canopy rail in August is
// 60 C of air over a black case, and the two failures that follow - a pack that
// will not charge, an e-paper panel that ghosts - both look like a fault in the
// part that gave up rather than in the afternoon that did it.

TEST_CASE("comms: the status reply carries the die temperature, in whole degrees") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);

    // Nothing has read the sensor yet: the key is absent, not zero.
    config.on_rx(config_frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("die_temp_c") == std::string::npos);

    config.set_die_temperature(412, /*valid=*/true);
    config.on_rx(config_frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":41") != std::string::npos);
}

// A board with no sensor, a driver that refused a measurement and a device at
// freezing are three different things. 0.0 C is a plausible hangar morning, so a
// zero must never stand in for the first two.
TEST_CASE("comms: no reading is no key, never a zero") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);

    config.set_die_temperature(0, /*valid=*/true);
    config.on_rx(config_frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":0") != std::string::npos);

    config.set_die_temperature(0, /*valid=*/false);
    config.on_rx(config_frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("die_temp_c") == std::string::npos);
}

TEST_CASE("comms: tenths are rounded away from zero on both sides of freezing") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);

    // A cold morning must not read one degree warmer than it is, and a hot
    // afternoon must not read one cooler: -20.6 C is -21, +20.6 C is +21.
    config.set_die_temperature(-206, true);
    config.on_rx(config_frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":-21") != std::string::npos);

    config.set_die_temperature(206, true);
    config.on_rx(config_frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":21") != std::string::npos);

    config.set_die_temperature(-4, true);
    config.on_rx(config_frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":0") != std::string::npos);
}

// J and L. A counter a bench reads is not state a pilot's screen reacts to, so it
// reads out with the rest of the radio's counters, not on the pushed status.
TEST_CASE("comms: the range gate's refusals read out with the radio's own counters") {
    platform::host::Link link;
    link.raise_link(1);
    link.declare_payload_bytes(kSmallestSupportedPayloadBytes);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kTestActivation);
    ConfigService config(link, store);

    Diagnostics& dump = config.diagnostics();
    dump.refreshes = 1;
    dump.noise_dbm = -101;
    dump.rx_ok = 7;
    config.set_range_refused(0);
    link.sent.clear();
    config.on_rx(config_frame("{\"cmd\":\"radio\"}"));
    REQUIRE_FALSE(link.sent.empty());
    std::string idle;
    for (const platform::host::Link::Frame& f : link.sent) {
        CHECK(f.bytes.size() <= static_cast<size_t>(kSmallestSupportedPayloadBytes));
        CHECK(f.bytes.find("\"cmd\":\"radio\"") != std::string::npos);
        CHECK(f.bytes.find("\"group\":\"radio\"") != std::string::npos);
        idle += f.bytes;
    }
    CHECK(idle.find("\"range_refused\":0") != std::string::npos);
    CHECK(idle.find("\"noise_dbm\":-101") != std::string::npos);
    CHECK(idle.find("\"rx_ok\":7") != std::string::npos);

    // A counter never resets, so the widest it can be is the widest the dump can
    // carry: ten digits (test/core/test_diagnostics.cpp holds the ceiling and why).
    // Nine of those do not fit one notification an iPhone will accept, so the
    // answer is two whole frames rather than one short one.
    dump.rx_ok = dump.rx_bad = dump.tx_ok = 2147483647u;
    dump.rx_wait = dump.rx_type = dump.tx_lost = 2147483647u;
    config.set_range_refused(2147483647u);
    link.sent.clear();
    config.on_rx(config_frame("{\"cmd\":\"radio\"}"));
    std::string body;
    for (const platform::host::Link::Frame& f : link.sent) {
        CHECK(f.bytes.size() <= static_cast<size_t>(kSmallestSupportedPayloadBytes));
        CHECK(f.bytes.back() == '}');
        body += f.bytes;
    }
    CHECK(body.find("\"range_refused\":2147483647") != std::string::npos);
    CHECK(body.find("\"more\":false") != std::string::npos);
    CHECK(config.link_drops() == 0);
}
