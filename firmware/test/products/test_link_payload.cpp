// What one frame may carry, and what each sender does when the answer is small.
//
// The finding this file exists for: the companion app worked on Android and not
// on an iPhone. Nothing read the MTU the central negotiated - every reply was
// sized against a local buffer, prj.conf asked for CONFIG_BT_L2CAP_TX_MTU=498
// and an iOS central commonly settles at 185, which is 182 bytes of notification
// payload. A notification longer than the payload is not truncated by the
// controller, it fails, so a reply that does not fit is either sized to fit or
// refused out loud - never handed down and hoped for.
//
// A link model and scripted JSON, no device: the product's own two ends of the
// range are walked in test/products/test_flight_log.cpp.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "core/comms/log_link.h"
#include "core/comms/timing_report.h"
#include "core/events/link.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "hardware/platform/host/platform.h"
#include "ports/null.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/config_frame.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {

// INFO: fc 18oct26 serial 0xFFFF under prefix 0x00 is 65535, the widest address an unlock can hand
const identity::Activation kWidestAddress{0xFFFF, 0xFFFF};

// The widest a unit can make its own answers: every field at the longest value
// it is allowed to hold, so these cases measure the worst case rather than a
// convenient one.
go::Settings widest_settings() {
    go::Settings settings = go::defaults();
    settings.aircraft_type = 17;
    settings.alarm_enabled = false;
    settings.alarm_volume = go::AlarmVolume::Loud;
    settings.units = go::Units::Nautical;
    std::memcpy(settings.callsign, "ABCDEFGHI", 10);  // the nine characters the schema allows
    return settings;
}

power::BatteryState full_battery() {
    power::BatteryState battery{};
    battery.cell_mv = 4200;
    battery.percent = 100;  // three digits
    battery.external_power = true;
    battery.charging = true;
    battery.valid = true;
    return battery;
}

// A device at its most talkative, field by field rather than as one state a cell
// could be in: the longest flight word, the longest power level, a full cell off
// the cable, and the widest temperature an int16 of tenths can print.
void make_worst_case(ConfigService& config) {
    power::BatteryState unplugged = full_battery();
    unplugged.external_power = false;
    unplugged.charging = false;
    config.set_flight_state(flight::FlightState::Airborne);
    config.set_battery_state(unplugged, power::PowerLevel::Critical);
    config.set_die_temperature(INT16_MIN, true);
}

// Two weeks of bench: one PPS edge a second puts a bucket into seven figures,
// and a histogram that loses its last bucket is not evidence.
void run_bench(timing::SlotTimingStatistics& statistics) {
    for (uint32_t i = 0; i < 1200000u; i++) {
        statistics.record_edge(static_cast<uint64_t>(i) * 1000000u, true);
        statistics.record_dwell_phase(static_cast<int64_t>(i % 7) * 3000 - 9000);
    }
    statistics.record_missed();
    statistics.record_refused();
}

std::string joined(const platform::host::Link& link) {
    std::string all;
    for (const platform::host::Link::Frame& frame : link.sent) all += frame.bytes;
    return all;
}

int occurrences(const std::string& haystack, const char* needle) {
    int count = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + 1))
        count++;
    return count;
}

}  // namespace

TEST_CASE("link: a link that has not been told what it carries promises what BLE guarantees") {
    // 23 bytes of default ATT_MTU less the three of notification header. Both
    // upstream SoftRF forks stop here and never send more; we only do better
    // when a central has agreed to.
    CHECK(ports::kMinimumLinkPayloadBytes == 20);
    ports::NullLink absent;
    CHECK(absent.payload_bytes() == ports::kMinimumLinkPayloadBytes);

    platform::host::Link link;

    link.raise_link(1);

    link.raise_link(1);
    link.declare_payload_bytes(4);
    CHECK(link.payload_bytes() == ports::kMinimumLinkPayloadBytes);

    // And a frame longer than that is refused rather than shortened, which is
    // what a controller does with an oversized notification.
    const char* twenty_one = "123456789012345678901";
    const ConstByteSpan too_long(reinterpret_cast<const uint8_t*>(twenty_one), 21);
    CHECK(link.send(events::Endpoint::Config, too_long) == Status::OutOfRange);
    CHECK(link.sent.empty());
    CHECK(link.refused_oversize == 1);
}

TEST_CASE("comms: the status a phone is pushed fits the narrowest payload we support, worst case") {
    platform::host::Link link;
    link.raise_link(1);
    link.raise_link(1);
    link.declare_payload_bytes(kSmallestSupportedPayloadBytes);
    go::Settings settings = widest_settings();
    go::SettingsStore store(settings, kWidestAddress);
    ConfigService config(link, store);
    make_worst_case(config);

    config.on_rx(config_frame("{\"command\":\"status\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string body = link.last().bytes;
    CHECK(body.size() <= static_cast<size_t>(kSmallestSupportedPayloadBytes));
    CHECK(body.size() == 168);
    CHECK(body.find("\"charging\":false") != std::string::npos);
    CHECK(body.find("\"power_level\":\"CRITICAL\"") != std::string::npos);
    CHECK(link.refused_oversize == 0);
    CHECK(config.link_drops() == 0);

    // Every field whole. json::Writer leaves a field out rather than cutting it,
    // so "fits" has to mean the last key is there and not merely valid JSON.
    CHECK(body.find("\"die_temperature_c\":-3277}") != std::string::npos);
}

TEST_CASE("comms: the config reply fits it too, as one flat object instead of an escaped one") {
    platform::host::Link link;
    link.raise_link(1);
    link.raise_link(1);
    link.declare_payload_bytes(kSmallestSupportedPayloadBytes);
    go::Settings settings = widest_settings();
    go::SettingsStore store(settings, kWidestAddress);
    ConfigService config(link, store);

    config.on_rx(config_frame("{\"command\":\"get\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string body = link.last().bytes;
    CHECK(body.size() <= static_cast<size_t>(kSmallestSupportedPayloadBytes));
    // Counted by hand: every key, address 65535, aircraft_type 17, the alarm off, nine letters.
    CHECK(body.size() == 166);
    CHECK(body.find("\"alarm_enabled\":false") != std::string::npos);
    CHECK(config.link_drops() == 0);

    // The settings used to ride as a JSON string inside the reply, and the
    // backslashes alone were 53 bytes of a 158-byte body. There is not one left.
    CHECK(body.find('\\') == std::string::npos);
    CHECK(body.find("\"command\":\"config\"") != std::string::npos);
    CHECK(body.find("\"version\":1") != std::string::npos);
    CHECK(body.find("\"address\":65535") != std::string::npos);
    CHECK(body.find("\"address_mapping_table\":58") != std::string::npos);
    CHECK(body.find("\"callsign\":\"ABCDEFGHI\"") != std::string::npos);
    // The last field written, so its presence is the proof nothing was dropped.
    CHECK(body.find("\"callsign\":\"ABCDEFGHI\"") != std::string::npos);
}

namespace {

struct UpdateBench {
    platform::host::Link link;
    go::Settings settings = widest_settings();
    go::SettingsStore store{settings, kWidestAddress};
    platform::host::Watchdog watchdog;
    platform::host::Dfu dfu{watchdog};
    ConfigService config{link, store, &dfu};

    explicit UpdateBench(bool allows_downgrade, ports::ImageVersion version = dfu::kWidestVersion) {
        link.raise_link(1);
        link.raise_link(1);
        link.declare_payload_bytes(kSmallestSupportedPayloadBytes);
        dfu.running_key_valid = true;
        dfu.running_signer.fill(0xff);
        dfu.allows_downgrade = allows_downgrade;
        config.set_image_state(dfu::ImageState::Probation, dfu::UpdateRecord{version, version});
        config.set_settings_fallback(settings::Fallback::Defaults);
        config.set_battery_state(full_battery(), power::PowerLevel::Critical);
        dfu::HubImageReport held{};
        held.holding = dfu::HubImage::Held;
        for (uint8_t& byte : held.digest) byte = 0xAB;
        config.set_hub_image(held);
        config.on_rx(config_frame("{\"command\":\"update\"}"));
    }
};

long version_field(const std::string& text, const char* key) {
    const size_t at = text.find(key);
    REQUIRE_MESSAGE(at != std::string::npos, "no ", key);
    return std::strtol(text.c_str() + text.find('=', at) + 1, nullptr, 10);
}

ports::ImageVersion widest_build_of_this_version() {
    const char* path = "products/skyblip_go/VERSION";
    std::FILE* file = std::fopen(path, "rb");
    REQUIRE_MESSAGE(file != nullptr, "cannot open ", path);
    std::string text;
    char chunk[256];
    size_t count = 0;
    while ((count = std::fread(chunk, 1, sizeof(chunk), file)) > 0) text.append(chunk, count);
    std::fclose(file);
    return ports::ImageVersion{static_cast<uint8_t>(version_field(text, "\nVERSION_MAJOR")),
                               static_cast<uint8_t>(version_field(text, "\nVERSION_MINOR")),
                               static_cast<uint16_t>(version_field(text, "\nPATCHLEVEL")),
                               dfu::kWidestVersion.build};
}

}  // namespace

TEST_CASE(
    "comms: the update report fits it too, widest versions, a settings fallback, a hub image "
    "and a key") {
    UpdateBench bench(false);
    REQUIRE(bench.link.sent.size() == 1);
    const std::string body = bench.link.last().bytes;
    // The widest release build's report, every key written and the swap refused for power.
    CHECK(body.size() == 169);
    CHECK(body.find("\"swap_powered\":false") != std::string::npos);
    CHECK(body.find("\"from\":\"9.99.99+99999\"") != std::string::npos);
    CHECK(body.find("\"to\":\"9.99.99+99999\"") != std::string::npos);
    CHECK(body.find("\"settings\":\"defaults\"") != std::string::npos);
    CHECK(body.find("\"imu\":\"abababababababab\"") != std::string::npos);
    CHECK(body.find("\"key\":\"FFFFFFFF\"") != std::string::npos);
    CHECK(bench.config.link_drops() == 0);
}

TEST_CASE(
    "comms: an update report too wide for the payload is refused and counted, never sent without "
    "its key") {
    UpdateBench bench(true);
    CHECK(bench.link.sent.empty());
    CHECK(bench.config.link_drops() == 1);
}

// scripts/build_image.sh stamps the VERSION file's triple and a commit count of at most 99999.
TEST_CASE(
    "comms: a development build's update report fits, at the widest version this product's "
    "VERSION file can stamp") {
    const ports::ImageVersion widest = widest_build_of_this_version();
    UpdateBench bench(true, widest);
    REQUIRE(bench.link.sent.size() == 1);
    const std::string body = bench.link.last().bytes;
    CHECK(body.size() <= static_cast<size_t>(kSmallestSupportedPayloadBytes));
    CHECK(body.find("\"downgrade\":true") != std::string::npos);
    CHECK(body.find("\"key\":\"FFFFFFFF\"") != std::string::npos);
    CHECK(bench.config.link_drops() == 0);
}

TEST_CASE("comms: a link that came up at the BLE minimum is answered with a count, not a frame") {
    platform::host::Link link;
    link.raise_link(1);
    link.raise_link(1);
    link.declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    go::Settings settings = widest_settings();
    go::SettingsStore store(settings, kWidestAddress);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);

    // Nothing in this dialect fits twenty bytes, not even a refusal, so the
    // honest answer is silence and a number - never a notification the
    // controller would fail, and never a reply with fields quietly missing.
    config.on_rx(config_frame("{\"command\":\"status\"}"));
    config.on_rx(config_frame("{\"command\":\"get\"}"));
    config.on_rx(config_frame("{\"command\":\"nonsense\"}"));
    CHECK(link.sent.empty());
    CHECK(config.link_drops() == 3);
    // The refusal happened above the port: nothing was ever handed down.
    CHECK(link.refused_oversize == 0);
}

TEST_CASE("comms: the bench's timing report is one frame on a wide link and several on a narrow") {
    timing::SlotTimingStatistics statistics;
    run_bench(statistics);
    go::Settings settings = go::defaults();

    // A phone that negotiated the whole L2CAP MTU: one frame, and it says so.
    platform::host::Link wide;
    wide.raise_link(1);
    wide.declare_payload_bytes(495);
    go::SettingsStore store_on_wide(settings, kWidestAddress);
    ConfigService on_wide(wide, store_on_wide, nullptr, &statistics);
    on_wide.on_rx(config_frame("{\"command\":\"timing\"}"));
    REQUIRE(wide.sent.size() == 1);
    CHECK(wide.last().bytes.find("\"part\":0") != std::string::npos);
    CHECK(wide.last().bytes.find("\"more\":false") != std::string::npos);

    // The same numbers to an iPhone: more than one frame, each one a complete
    // object no longer than the link said it would carry, and the same twelve
    // fields between them - a laboratory reads this once, so a second frame is
    // free, and trimming a bucket would destroy what it came for.
    platform::host::Link narrow;
    narrow.raise_link(1);
    narrow.declare_payload_bytes(kSmallestSupportedPayloadBytes);
    go::SettingsStore store_on_narrow(settings, kWidestAddress);
    ConfigService on_narrow(narrow, store_on_narrow, nullptr, &statistics);
    on_narrow.on_rx(config_frame("{\"command\":\"timing\"}"));
    REQUIRE(narrow.sent.size() > 1);
    CHECK(narrow.sent.back().bytes.find("\"more\":false") != std::string::npos);
    for (size_t i = 0; i < narrow.sent.size(); i++) {
        const std::string& body = narrow.sent[i].bytes;
        CHECK(body.size() <= static_cast<size_t>(kSmallestSupportedPayloadBytes));
        CHECK(body.front() == '{');
        CHECK(body.back() == '}');
        CHECK(body.find("\"command\":\"timing\"") != std::string::npos);
        if (i + 1 < narrow.sent.size()) CHECK(body.find("\"more\":true") != std::string::npos);
    }

    const std::string all = joined(narrow);
    CHECK(occurrences(all, "\"pps_us\":") == 1);
    CHECK(occurrences(all, "\"dwell_us\":") == 1);
    CHECK(occurrences(all, "\"holdover_events\":") == 1);
    CHECK(occurrences(all, "\"refused\":") == 1);
    CHECK(all.find("\"pps_samples\":1199999") != std::string::npos);
    CHECK(all.find("\"missed\":1") != std::string::npos);
    CHECK(all.find("\"refused\":1") != std::string::npos);
    CHECK(on_narrow.link_drops() == 0);
    CHECK(narrow.refused_oversize == 0);

    // A link so narrow that one histogram cannot be placed in any frame at all
    // is refused whole: two frames of a three-frame report, with no third one
    // coming, is worse than no report.
    platform::host::Link hopeless;
    hopeless.raise_link(1);
    hopeless.declare_payload_bytes(64);
    go::SettingsStore store_on_hopeless(settings, kWidestAddress);
    ConfigService on_hopeless(hopeless, store_on_hopeless, nullptr, &statistics);
    on_hopeless.on_rx(config_frame("{\"command\":\"timing\"}"));
    CHECK(hopeless.sent.empty());
    CHECK(on_hopeless.link_drops() == 1);
}

TEST_CASE("comms: a status push the controller could not take is retried, not lost") {
    platform::host::Link link;
    link.raise_link(1);
    link.raise_link(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kWidestAddress);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_link_up(events::LinkUp{1, link.payload_bytes()});

    // Out of buffers for one pass - an upload sharing the connection will do
    // that. The push is the one frame nobody asked for, so nobody will ask
    // again: it is the one that has to come back on the next pass.
    link.force_status(Status::WouldBlock);
    config.set_battery_state(full_battery(), power::PowerLevel::Normal);
    CHECK(link.sent.empty());
    CHECK(config.link_drops() == 1);

    config.tick(10);
    REQUIRE(link.sent.size() == 1);
    CHECK(link.last().bytes.find("\"command\":\"status\"") != std::string::npos);
    CHECK(config.link_drops() == 1);

    // And a second pass does not send it again: a retry is not a stream.
    config.tick(20);
    CHECK(link.sent.size() == 1);
}

TEST_CASE("comms: a push that will never fit is counted once and not retried forever") {
    platform::host::Link link;
    link.raise_link(1);
    link.raise_link(1);
    link.declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kWidestAddress);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_link_up(events::LinkUp{1, link.payload_bytes()});

    config.set_battery_state(full_battery(), power::PowerLevel::Normal);
    CHECK(config.link_drops() == 1);
    for (uint32_t now_ms = 10; now_ms <= 100; now_ms += 10) config.tick(now_ms);
    // Nothing about a frame too big changes by waiting, so waiting is not the
    // answer: one count, and the phone can ask for a status whenever it likes.
    CHECK(config.link_drops() == 1);
    CHECK(link.sent.empty());
}

// The notify share cut a report at its first refusal, and the phone waited on "more".
TEST_CASE("comms: a report the link takes one frame at a time arrives whole and in order") {
    timing::SlotTimingStatistics statistics;
    run_bench(statistics);
    go::Settings settings = go::defaults();

    platform::host::Link free_link;
    free_link.raise_link(1);
    free_link.declare_payload_bytes(kSmallestSupportedPayloadBytes);
    go::SettingsStore store_free(settings, kWidestAddress);
    ConfigService on_free(free_link, store_free, nullptr, &statistics);
    on_free.on_rx(config_frame("{\"command\":\"timing\"}"));
    REQUIRE(free_link.sent.size() > 1);

    platform::host::Link busy;
    busy.raise_link(1);
    busy.declare_payload_bytes(kSmallestSupportedPayloadBytes);
    busy.hold_after(1);
    go::SettingsStore store_busy(settings, kWidestAddress);
    ConfigService on_busy(busy, store_busy, nullptr, &statistics);
    on_busy.on_rx(config_frame("{\"command\":\"timing\"}"));
    CHECK(busy.sent.size() == 1);
    CHECK(on_busy.replying());
    for (uint32_t now_ms = 10; now_ms < 1000 && on_busy.replying(); now_ms += 10) {
        busy.serve();
        on_busy.resume_replies(now_ms);
    }
    CHECK_FALSE(on_busy.replying());
    CHECK(joined(busy) == joined(free_link));
    CHECK(on_busy.link_drops() == 0);
}

TEST_CASE("comms: a reply the link refused goes out once the central is served, not dropped") {
    platform::host::Link link;
    link.raise_link(1);
    link.hold_after(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kWidestAddress);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame("{\"command\":\"get\"}"));
    config.on_rx(config_frame("{\"command\":\"status\"}"));
    REQUIRE(link.sent.size() == 1);
    CHECK(config.replying());

    config.resume_replies(10);
    CHECK(link.sent.size() == 1);
    link.serve();
    config.resume_replies(20);
    REQUIRE(link.sent.size() == 2);
    CHECK(link.last().bytes.find("\"command\":\"status\"") != std::string::npos);
    CHECK_FALSE(config.replying());
    CHECK(config.link_drops() == 0);
}

TEST_CASE("comms: a central that never takes its reply loses it, counted, and frees the service") {
    platform::host::Link link;
    link.raise_link(1);
    link.hold_after(1);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kWidestAddress);
    ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame("{\"command\":\"get\"}"));
    config.on_rx(config_frame("{\"command\":\"status\"}"));
    config.resume_replies(ports::kReplyWaitCeilingMs - 1);
    CHECK(config.replying());
    config.resume_replies(ports::kReplyWaitCeilingMs);
    CHECK_FALSE(config.replying());
    CHECK(config.link_drops() == 1);
    CHECK(link.sent.size() == 1);
}
