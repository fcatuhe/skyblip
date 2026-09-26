// What the companion link REPORTS, as opposed to what it authorises: why the
// device came up, the state of the cell a pilot's tablet draws, and the bench
// numbers a laboratory copies into a compliance report. A link model and
// scripted JSON, no device, exactly as in test_comms.cpp - which keeps the
// authorisation state machine and nothing else.
#include <cstdint>
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "core/events/link.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {
constexpr uint32_t kTestAddr = 0x123456;
events::RxFrame frame(const char* json) {
    events::RxFrame f{};
    f.session_id = 1;
    f.endpoint = events::Endpoint::Config;
    f.len = static_cast<uint16_t>(std::strlen(json));
    std::memcpy(f.data.data(), json, f.len);
    return f;
}

// The bench's timing answer as a laboratory reads it: every frame the service
// put on the link, joined. It is one frame when the negotiated payload carries
// it whole and several when it does not, and the frames arrive back to back -
// nothing has to be asked for twice.
std::string joined(const platform::host::Link& link) {
    std::string all;
    for (const platform::host::Link::Frame& f : link.sent) all += f.bytes;
    return all;
}

power::BatteryState battery_of(uint8_t percent, bool charging, bool valid = true) {
    power::BatteryState b{};
    b.millivolts = 3700;
    b.percent = percent;
    b.external_power = charging;
    b.charging = charging;
    b.valid = valid;
    return b;
}
}  // namespace

TEST_CASE("comms: status carries state, and why the device came up is the dump's") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    std::memcpy(s.callsign, "D-KXYZ", 7);
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::OnGround);
    cs.set_reset_reason(power::ResetReason::Watchdog);
    cs.diagnostics().refreshes = 1;

    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string body = link.last().bytes;
    CHECK(body.find("\"cmd\":\"status\"") != std::string::npos);
    CHECK(body.find("\"flight\":\"ground\"") != std::string::npos);
    // State, and only state. The callsign and the device address are what the
    // "config" reply answers, and the reset reason never changes inside a session:
    // carrying any of them here is what used to crowd the one unsolicited frame.
    CHECK(body.find("D-KXYZ") == std::string::npos);
    CHECK(body.find("\"addr\"") == std::string::npos);
    CHECK(body.find("\"reset\"") == std::string::npos);
    cs.on_rx(frame("{\"cmd\":\"get\"}"));
    CHECK(link.last().bytes.find("D-KXYZ") != std::string::npos);

    link.clear();
    cs.on_rx(frame("{\"cmd\":\"diag\"}"));
    CHECK(joined(link).find("\"reset\":\"WATCHDOG\"") != std::string::npos);
}

TEST_CASE("comms: timing reports the accumulator's buckets and counters") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    timing::SlotTimingStats stats;
    stats.record_edge(0, true);
    stats.record_edge(1000000, true);  // one clean second: centre bucket
    stats.record_dwell_phase(200);     // inside the hop guard
    stats.record_missed();
    stats.record_refused();
    stats.record_refused();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, nullptr, &stats);

    cs.on_rx(frame("{\"cmd\":\"timing\"}"));
    REQUIRE(!link.sent.empty());
    // The last frame is the one that says there is nothing after it, whether
    // that is the first frame or the third.
    CHECK(link.sent.back().bytes.find("\"more\":false") != std::string::npos);
    const std::string body = joined(link);
    CHECK(body.find("\"cmd\":\"timing\"") != std::string::npos);
    CHECK(body.find("\"pps_us\":\"0,0,0,1,0,0,0\"") != std::string::npos);
    CHECK(body.find("\"dwell_us\":\"0,0,0,0,1,0,0\"") != std::string::npos);
    CHECK(body.find("\"pps_samples\":1") != std::string::npos);
    CHECK(body.find("\"dwell_worst_us\":200") != std::string::npos);
    CHECK(body.find("\"holdover\":0") != std::string::npos);
    CHECK(body.find("\"missed\":1") != std::string::npos);
    CHECK(body.find("\"refused\":2") != std::string::npos);
}

TEST_CASE("comms: timing without an accumulator wired up says so, not zeros") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.on_rx(frame("{\"cmd\":\"timing\"}"));
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last().bytes.find("no_stats") != std::string::npos);
}

TEST_CASE("comms: status carries state of charge, the charging flag and the level") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::OnGround);
    cs.set_battery_state(battery_of(61, false), power::PowerLevel::Normal);

    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string body = link.last().bytes;
    CHECK(body.find("\"battery_percent\":61") != std::string::npos);
    CHECK(body.find("\"charging\":false") != std::string::npos);
    CHECK(body.find("\"power_level\":\"OK\"") != std::string::npos);
}

TEST_CASE("comms: a battery nobody has read has no percentage, never a false zero") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::OnGround);
    // No set_battery_state call at all: no sample has ever arrived.

    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    const std::string body = link.last().bytes;
    CHECK(body.find("battery_percent") == std::string::npos);
    CHECK(body.find("\"power_level\":\"--\"") != std::string::npos);
}

TEST_CASE(
    "comms: no push without a link, one push per real change, none for a repeat or for noise "
    "inside a step") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::OnGround);

    // A baseline, and then a real change, both before the link comes up.
    cs.set_battery_state(battery_of(50, false), power::PowerLevel::Normal);
    cs.set_battery_state(battery_of(50, true), power::PowerLevel::Normal);
    CHECK(link.sent.empty());

    cs.on_link_up(events::LinkUp{1, 200});

    cs.set_battery_state(battery_of(50, true), power::PowerLevel::Normal);  // repeat: no push
    CHECK(link.sent.empty());

    cs.set_battery_state(battery_of(50, false), power::PowerLevel::Normal);  // charging flips back
    REQUIRE(link.sent.size() == 1);

    cs.set_battery_state(battery_of(50, false), power::PowerLevel::Normal);  // repeat: no storm
    CHECK(link.sent.size() == 1);

    cs.set_battery_state(battery_of(50, false), power::PowerLevel::Low);  // level changes
    CHECK(link.sent.size() == 2);

    cs.set_battery_state(battery_of(56, false), power::PowerLevel::Low);  // crosses a step (50->56)
    CHECK(link.sent.size() == 3);

    cs.set_battery_state(battery_of(57, false),
                         power::PowerLevel::Low);  // same step as 56: no push
    CHECK(link.sent.size() == 3);

    cs.on_link_down(events::LinkDown{1});
    cs.set_battery_state(battery_of(90, false), power::PowerLevel::Normal);  // link is down again
    CHECK(link.sent.size() == 3);
}

TEST_CASE("comms: status at its widest is this frame, field for field, inside 182 bytes") {
    platform::host::Link link;
    link.raise_link(1);
    link.declare_payload_bytes(kSmallestSupportedPayload);
    go::Settings s = go::defaults();
    std::memcpy(s.callsign, "ABCDEFGHI", 10);
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.set_flight_state(flight::FlightState::Airborne);
    cs.set_reset_reason(power::ResetReason::Lockup);
    power::BatteryState battery{};
    battery.millivolts = 4200;
    battery.percent = 100;
    battery.external_power = true;
    battery.charging = true;
    battery.valid = true;
    cs.set_battery_state(battery, power::PowerLevel::Cutoff);
    // The driver gates -50 to +125 C, the formatter does not: int16 prints widest.
    cs.set_die_temperature(INT16_MIN, true);

    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string body = link.last().bytes;
    CHECK(body ==
          "{\"cmd\":\"status\",\"flight\":\"airborne\",\"upload\":false,\"battery_percent\":100,"
          "\"charging\":true,\"power_level\":\"CUTOFF\",\"went_dark_flat\":false,"
          "\"die_temp_c\":-3277}");
    CHECK(body.size() <= static_cast<size_t>(kSmallestSupportedPayload));
    CHECK(cs.link_drops() == 0);
}

TEST_CASE("comms: status says whether the unit last went dark on a flat cell") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);

    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"went_dark_flat\":false") != std::string::npos);

    cs.set_went_dark_flat(true);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"went_dark_flat\":true") != std::string::npos);
}

TEST_CASE("comms: timing carries the dwell evidence, and no clear-channel figure") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    timing::SlotTimingStats stats;
    stats.record_refused();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, nullptr, &stats);

    cs.on_rx(frame("{\"cmd\":\"timing\"}"));
    REQUIRE(!link.sent.empty());
    const std::string body = joined(link);
    CHECK(body.find("\"refused\":1") != std::string::npos);
    // Nothing listens before talking, so there is no threshold to report.
    CHECK(body.find("carrier_sense") == std::string::npos);
    // Every frame is a complete object of its own, and no frame is longer than
    // the link said it would carry.
    for (const platform::host::Link::Frame& f : link.sent) {
        CHECK(f.bytes.front() == '{');
        CHECK(f.bytes.back() == '}');
        CHECK(f.bytes.size() <= link.payload_bytes());
    }
}

// L. The whole dump, over the link the phone already has open: the same five
// subsystems the USB console prints, so a laptop on the bench and a companion page
// on a wing cannot report a device differently. The formatter itself is proved in
// test/core/test_diagnostics.cpp; this is the dispatch and the ceiling.
TEST_CASE("comms: one question answers every subsystem, in frames the link can carry") {
    platform::host::Link link;
    link.raise_link(1);
    link.declare_payload_bytes(kSmallestSupportedPayload);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);

    // What the product tells this service, through the doors it already had.
    cs.set_reset_reason(power::ResetReason::Watchdog);
    cs.set_battery_state(battery_of(64, false), power::PowerLevel::Normal);
    cs.set_die_temperature(415, true);
    cs.set_range_refused(5);

    // And what the product's collector fills in, once a second.
    Diagnostics& dump = cs.diagnostics();
    dump.refreshes = 3;
    dump.supply_warnings = 1;
    dump.battery_implausible = 2;
    dump.uptime_s = 3725;
    dump.noise_dbm = -101;
    dump.rx_ok = 1204;
    dump.tracked = 4;
    dump.gnss_fixes = 5210;
    dump.gnss_baud = 9600;
    dump.gnss_identified = true;
    dump.gnss_firmware = "URANUS5,V5.1.0.0";
    dump.gnss_reject = gnss::FixReject::Stale;
    dump.gnss_rejected = 6;

    link.clear();
    cs.on_rx(frame("{\"cmd\":\"diag\"}"));
    REQUIRE(!link.sent.empty());
    const std::string body = joined(link);

    CHECK(body.find("\"group\":\"sys\"") != std::string::npos);
    CHECK(body.find("\"up_s\":3725") != std::string::npos);
    CHECK(body.find("\"reset\":\"WATCHDOG\"") != std::string::npos);
    CHECK(body.find("\"noise_dbm\":-101") != std::string::npos);
    CHECK(body.find("\"rx_ok\":1204") != std::string::npos);
    CHECK(body.find("\"range_refused\":5") != std::string::npos);
    CHECK(body.find("\"tracked\":4") != std::string::npos);
    CHECK(body.find("\"fixes\":5210") != std::string::npos);
    CHECK(body.find("\"baud\":9600") != std::string::npos);
    CHECK(body.find("\"firmware\":\"URANUS5,V5.1.0.0\"") != std::string::npos);
    CHECK(body.find("\"reject\":\"STALE\"") != std::string::npos);
    CHECK(body.find("\"rejected\":6") != std::string::npos);
    CHECK(body.find("\"supply_warnings\":1") != std::string::npos);
    CHECK(body.find("\"implausible\":2") != std::string::npos);
    // The gauge and the die sensor are the SAME numbers the status reply carries,
    // because there is one snapshot behind both replies and not two copies.
    CHECK(body.find("\"percent\":64") != std::string::npos);
    CHECK(body.find("\"die_temp_c\":42") != std::string::npos);
    cs.on_rx(frame("{\"cmd\":\"status\"}"));
    CHECK(link.last().bytes.find("\"battery_percent\":64") != std::string::npos);
    CHECK(link.last().bytes.find("\"die_temp_c\":42") != std::string::npos);

    // Every frame whole, none longer than the narrowest phone in the field, and
    // nothing refused on the way out.
    for (const platform::host::Link::Frame& f : link.sent) {
        CHECK(f.bytes.front() == '{');
        CHECK(f.bytes.back() == '}');
        CHECK(f.bytes.size() <= static_cast<size_t>(kSmallestSupportedPayload));
    }
    CHECK(cs.link_drops() == 0);
}

TEST_CASE("comms: a dump nobody has collected says so, not zeros") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.on_rx(frame("{\"cmd\":\"diag\"}"));
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last().bytes.find("no_stats") != std::string::npos);
    // A device with a dead receiver and a dead radio reports zeros for real, so
    // the difference has to be visible: the refusal names the wiring.
    CHECK(link.last().bytes.find("\"ack\":false") != std::string::npos);

    // The radio group is not gated, and that asymmetry is deliberate: its own key
    // has a writer that runs every pass, and a radio that has heard nothing
    // truthfully has zero receptions. A receiver's firmware string does not.
    link.clear();
    cs.on_rx(frame("{\"cmd\":\"radio\"}"));
    CHECK(link.last().bytes.find("\"cmd\":\"radio\"") != std::string::npos);
}

// Asking is not clearing. Every counter in the dump belongs to somebody else and
// keeps counting; two questions in a row must produce the same answer.
TEST_CASE("comms: asking for the dump twice answers the same numbers twice") {
    platform::host::Link link;
    link.raise_link(1);
    link.declare_payload_bytes(kSmallestSupportedPayload);
    go::Settings s = go::defaults();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs);
    cs.diagnostics().refreshes = 1;
    cs.diagnostics().rx_ok = 99;
    cs.set_range_refused(7);

    cs.on_rx(frame("{\"cmd\":\"diag\"}"));
    const std::string first = joined(link);
    link.clear();
    cs.on_rx(frame("{\"cmd\":\"diag\"}"));
    CHECK(joined(link) == first);
    CHECK(cs.diagnostics().rx_ok == 99);
    CHECK(cs.diagnostics().range_refused == 7);
}

// M. The other 32-bit boundary in this section, and it is not the clock: a count a
// laboratory reads. json::Writer and the console writer both carry a long, which is
// 64-bit on this host and 32-bit on the nRF52840, so a uint32_t counter cast
// straight to long prints correctly here and NEGATIVELY on the device - the one
// divergence a host suite cannot see, since the suite is the thing being trusted.
// The timing report's sample counts were naked casts until 2026-08-06; they go
// through the one saturating conversion both reports share now
// (core/comms/frame_budget.h, pinned in test/core/test_diagnostics.cpp).
TEST_CASE("comms: the timing report's counts are unsigned on every platform") {
    platform::host::Link link;
    link.raise_link(1);
    go::Settings s = go::defaults();
    timing::SlotTimingStats stats;
    stats.record_edge(0, true);
    stats.record_edge(1000000, true);
    stats.record_missed();
    go::SettingsStore store_cs(s, kTestAddr);
    ConfigService cs(link, store_cs, nullptr, &stats);
    cs.on_rx(frame("{\"cmd\":\"timing\"}"));
    const std::string body = joined(link);
    CHECK(body.find("\"pps_samples\":1") != std::string::npos);
    CHECK(body.find("\"dwell_samples\":0") != std::string::npos);
    CHECK(body.find("\"missed\":1") != std::string::npos);
    CHECK(body.find("_samples\":-") == std::string::npos);
    CHECK(body.find("\"holdover\":-") == std::string::npos);
    // The signed figures stay signed: an error in microseconds has a direction.
    CHECK(body.find("\"pps_worst_us\":0") != std::string::npos);
}
