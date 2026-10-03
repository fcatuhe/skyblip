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
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"get\"}"));
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last_on(events::Endpoint::Config));
    CHECK(link.last().bytes.find("config") != std::string::npos);
}

TEST_CASE("comms: defaults answers what a new unit ships on, and sent back as a set restores it") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    s.aircraft_type = 7;
    s.alarm_volume = 5;
    std::memcpy(s.callsign, "D-KXYZ", 7);
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);

    cs.on_rx(frame("{\"cmd\":\"defaults\"}"));
    const std::string body = link.last().bytes;
    CHECK(body ==
          "{\"cmd\":\"defaults\",\"aircraft_type\":1,\"alarm\":true,\"alarm_volume\":3,"
          "\"units\":0,\"callsign\":\"\"}");

    const std::string set = "{\"cmd\":\"set\"" + body.substr(body.find(','));
    cs.on_rx(frame(set.c_str()));
    cs.confirm();
    const go::Settings shipped = go::defaults();
    CHECK(s.aircraft_type == shipped.aircraft_type);
    CHECK(s.alarm_volume == shipped.alarm_volume);
    CHECK(std::string(s.callsign).empty());
}

TEST_CASE("comms: set on the ground stages, needs confirmation, then applies") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);

    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    CHECK(cs.pending() == Pending::Set);
    CHECK(link.last().bytes.find("confirm") != std::string::npos);
    CHECK(int(s.aircraft_type) == 1);  // not yet applied

    cs.confirm();
    CHECK(cs.pending() == Pending::None);
    CHECK(int(s.aircraft_type) == 4);  // applied
    CHECK(cs.settings_dirty());
    CHECK(link.last().bytes.find("\"ack\":true") != std::string::npos);
}

// The press authorises what the glass says, so the glass says what the phone sent.
TEST_CASE("comms: the settings prompt lists what the set changes, and only that") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);

    cs.on_rx(frame("{\"cmd\":\"set\",\"callsign\":\"F-JXYZ\",\"units\":0,\"alarm_volume\":5}"));
    REQUIRE(cs.pending() == Pending::Set);
    CHECK(std::string(cs.prompt_detail()) == "CALLSIGN F-JXYZ\nVOLUME 5 OF 5");

    cs.on_rx(frame("{\"cmd\":\"set\",\"units\":0}"));
    CHECK(std::string(cs.prompt_detail()) == pending_detail(Pending::Set));
}

TEST_CASE("comms: a set that fills the whole frame is staged to its last byte") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);

    const std::string head = "{\"cmd\":\"set\",";
    const std::string tail = "\"aircraft_type\":4}";
    const std::string full =
        head + std::string(events::RxFrame{}.data.size() - head.size() - tail.size(), ' ') + tail;
    cs.on_rx(frame(full.c_str()));
    REQUIRE(cs.pending() == Pending::Set);
    CHECK(std::string(cs.pending_json()) == full);
}

TEST_CASE("comms: set is REFUSED in flight (fail closed), no staging") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Airborne);
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    CHECK(cs.pending() == Pending::None);
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);
    CHECK(int(s.aircraft_type) == 1);
}

// The latch itself is core/flight/ground.h's; this gate refuses whatever is not a confirmed ground.
TEST_CASE("comms: unknown flight-state refuses") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Unknown);  // never confirmed on ground
    CHECK(cs.flight_state() == flight::FlightState::Unknown);
    cs.on_rx(frame("{\"cmd\":\"set\",\"alarm\":false}"));
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);

    cs.set_flight_state(flight::FlightState::Ground);
    CHECK(cs.flight_state() == flight::FlightState::Ground);
}

TEST_CASE("comms: confirm re-checks the gate, becoming airborne cancels apply") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    cs.set_flight_state(flight::FlightState::Airborne);  // took off before confirming
    cs.confirm();
    CHECK(int(s.aircraft_type) == 1);  // NOT applied
    CHECK(link.last().bytes.find("in_flight") != std::string::npos);
}

// Nothing used to set the gate, so it stayed Unknown and refused forever: green tests, dead device.
TEST_CASE("comms: only the ADS-L on-ground code is permission, every other value refuses") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::state_from(0));
    cs.on_rx(frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    CHECK(cs.pending() == Pending::None);
    cs.set_flight_state(flight::state_from(1));
    cs.on_rx(frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    CHECK(cs.pending() == Pending::Dfu);
}

TEST_CASE("comms: a prompt nobody answers expires, and a later confirm grants nothing") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.tick(1000);

    cs.on_rx(frame("{\"cmd\":\"dfu\",\"version\":\"0.2.0+15\"}"));
    REQUIRE(cs.pending() == Pending::Dfu);
    cs.tick(1000 + kConfirmWindowMs - 1);
    CHECK(cs.pending() == Pending::Dfu);

    cs.tick(1000 + kConfirmWindowMs);
    CHECK(cs.pending() == Pending::None);
    CHECK(link.last().bytes.find("expired") != std::string::npos);

    // The button pressed after the prompt came down authorises the operation
    // that was on it, or it authorises nothing. It is nothing.
    cs.confirm();
    CHECK_FALSE(cs.upload_allowed());
}

TEST_CASE("comms: taking off takes a standing prompt away with it") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"recovery\"}"));
    REQUIRE(cs.pending() == Pending::Recovery);

    cs.set_flight_state(flight::FlightState::Airborne);
    CHECK(cs.pending() == Pending::None);
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
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"set\",\"alarm\":false}"));
    CHECK(cs.pending() == Pending::Set);
    cs.on_link_down(events::LinkDown{1});
    CHECK(cs.pending() == Pending::None);
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
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);

    power::BatteryState low{};
    low.valid = true;
    low.millivolts = 3400;
    cs.set_battery_state(low, power::PowerLevel::Critical);
    CHECK_FALSE(cs.settings_writable());

    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    // Refused at the door: no prompt to walk over and confirm for a write that
    // was never going to happen.
    CHECK(cs.pending() == Pending::None);
    CHECK(link.last().bytes.find("low_power") != std::string::npos);
    CHECK(link.last().bytes.find("\"ack\":false") != std::string::npos);
    CHECK(int(s.aircraft_type) == 1);
    CHECK_FALSE(cs.settings_dirty());

    // A charger on the cable holds the terminal above the cell, so core/power
    // reports Normal again and the door opens.
    power::BatteryState charging{};
    charging.valid = true;
    charging.millivolts = 3400;
    charging.external_power = true;
    charging.charging = true;
    cs.set_battery_state(charging, power::PowerLevel::Normal);
    CHECK(cs.settings_writable());
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    REQUIRE(cs.pending() == Pending::Set);
    cs.confirm();
    CHECK(int(s.aircraft_type) == 4);
}

// The confirmation window is 30 s wide and a cell can cross the warning inside
// it. The gate is therefore asked twice, exactly as the flight-state gate is.
TEST_CASE("comms: a cell that falls while the prompt stands cancels the change") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    REQUIRE(cs.pending() == Pending::Set);

    power::BatteryState low{};
    low.valid = true;
    low.millivolts = 3100;
    cs.set_battery_state(low, power::PowerLevel::Flat);
    cs.confirm();
    CHECK(cs.pending() == Pending::None);
    CHECK(int(s.aircraft_type) == 1);
    CHECK_FALSE(cs.settings_dirty());
    CHECK(link.last().bytes.find("low_power") != std::string::npos);
}

// The power-failure comparator watches the SoC's own rail, which is at or below
// the cell: once it has fired, a healthy-looking divider reading is not the
// question any more.
TEST_CASE("comms: a fired power-failure comparator closes the door on its own") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Ground);
    power::BatteryState healthy{};
    healthy.valid = true;
    healthy.millivolts = 4000;
    cs.set_battery_state(healthy, power::PowerLevel::Normal);
    REQUIRE(cs.settings_writable());

    cs.set_supply_warned(true);
    CHECK_FALSE(cs.settings_writable());
    cs.on_rx(frame("{\"cmd\":\"set\",\"aircraft_type\":4}"));
    CHECK(cs.pending() == Pending::None);
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
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);

    // Nothing has read the sensor yet: the key is absent, not zero.
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("die_temp_c") == std::string::npos);

    cs.set_die_temperature(412, /*valid=*/true);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":41") != std::string::npos);
}

// A board with no sensor, a driver that refused a measurement and a device at
// freezing are three different things. 0.0 C is a plausible hangar morning, so a
// zero must never stand in for the first two.
TEST_CASE("comms: no reading is no key, never a zero") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);

    cs.set_die_temperature(0, /*valid=*/true);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":0") != std::string::npos);

    cs.set_die_temperature(0, /*valid=*/false);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("die_temp_c") == std::string::npos);
}

TEST_CASE("comms: tenths are rounded away from zero on both sides of freezing") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);

    // A cold morning must not read one degree warmer than it is, and a hot
    // afternoon must not read one cooler: -20.6 C is -21, +20.6 C is +21.
    cs.set_die_temperature(-206, true);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":-21") != std::string::npos);

    cs.set_die_temperature(206, true);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":21") != std::string::npos);

    cs.set_die_temperature(-4, true);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"die_temp_c\":0") != std::string::npos);
}

// J and L. A counter a bench reads is not state a pilot's screen reacts to, so it
// reads out with the rest of the radio's counters, not on the pushed status.
TEST_CASE("comms: the range gate's refusals read out with the radio's own counters") {
    platform::host::Link link;
    link.raise_link(1);
    link.declare_payload_bytes(kSmallestSupportedPayload);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);

    Diagnostics& dump = cs.diagnostics();
    dump.refreshes = 1;
    dump.noise_dbm = -101;
    dump.rx_ok = 7;
    cs.set_range_refused(0);
    link.sent.clear();
    cs.on_rx(frame("{\"cmd\":\"radio\"}"));
    REQUIRE_FALSE(link.sent.empty());
    std::string idle;
    for (const platform::host::Link::Frame& f : link.sent) {
        CHECK(f.bytes.size() <= static_cast<size_t>(kSmallestSupportedPayload));
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
    cs.set_range_refused(2147483647u);
    link.sent.clear();
    cs.on_rx(frame("{\"cmd\":\"radio\"}"));
    std::string body;
    for (const platform::host::Link::Frame& f : link.sent) {
        CHECK(f.bytes.size() <= static_cast<size_t>(kSmallestSupportedPayload));
        CHECK(f.bytes.back() == '}');
        body += f.bytes;
    }
    CHECK(body.find("\"range_refused\":2147483647") != std::string::npos);
    CHECK(body.find("\"more\":false") != std::string::npos);
    CHECK(cs.link_drops() == 0);
}
