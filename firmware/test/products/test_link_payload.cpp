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
// A link model and scripted frames, no device: the product's own two ends of the
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

// Every timing field at 65536 or more, which CBOR writes in five bytes as it writes UINT32_MAX.
void run_widest_bench(timing::SlotTimingStatistics& statistics) {
    using Statistics = timing::SlotTimingStatistics;
    const int64_t past_the_guard = Statistics::kJitterGuardUs + 70000;
    const int64_t one_per_bucket[Statistics::kBuckets] = {
        -past_the_guard,       -Statistics::kHopGuardUs, -Statistics::kRetuneUs, 0,
        Statistics::kRetuneUs, Statistics::kHopGuardUs,  past_the_guard};
    const uint32_t samples = Statistics::kBuckets * 65536u;
    uint64_t edge_us = Statistics::kNominalSecondUs;
    statistics.record_edge(edge_us, true);
    for (uint32_t i = 0; i < samples; i++) {
        const int64_t error_us = one_per_bucket[i % Statistics::kBuckets];
        edge_us += static_cast<uint64_t>(Statistics::kNominalSecondUs + error_us);
        statistics.record_edge(edge_us, true);
        statistics.record_dwell_phase(error_us);
    }
    for (uint32_t i = 0; i < 65536u; i++) {
        statistics.record_edge(edge_us, false);
        edge_us += static_cast<uint64_t>(Statistics::kNominalSecondUs);
        statistics.record_edge(edge_us, true);
        statistics.record_missed();
        statistics.record_refused();
    }
    statistics.record_switching(UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX);
}

std::string joined(const platform::host::Link& link) {
    std::string all;
    for (const platform::host::Link::Frame& frame : link.sent) all += link_text(frame.bytes);
    return all;
}

const char* const kSmallMtuRefusal =
    "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"small_mtu\"}";

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
    const std::string frame = link.last().bytes;
    CHECK(frame.size() <= static_cast<size_t>(kSmallestSupportedPayloadBytes));
    CHECK(frame.size() == 20);
    const std::string body = link_text(frame);
    CHECK(body.find("\"charging\":false") != std::string::npos);
    CHECK(body.find("\"power_level\":\"critical\"") != std::string::npos);
    CHECK(link.refused_oversize == 0);
    CHECK(config.link_drops() == 0);

    // Every field whole: the last key is there.
    CHECK(body.find("\"die_temperature_c\":-3277}") != std::string::npos);
}

TEST_CASE("comms: the config reply fits it too, as one flat map, byte for byte") {
    platform::host::Link link;
    link.raise_link(1);
    link.raise_link(1);
    link.declare_payload_bytes(kSmallestSupportedPayloadBytes);
    go::Settings settings = widest_settings();
    go::SettingsStore store(settings, kWidestAddress);
    ConfigService config(link, store);

    config.on_rx(config_frame("{\"command\":\"get\"}"));
    REQUIRE(link.sent.size() == 1);
    const std::string frame = link.last().bytes;
    CHECK(frame.size() <= static_cast<size_t>(kSmallestSupportedPayloadBytes));
    // A nine-entry map: address 65535 behind a 2-byte head, 58 and 17 behind one, nine letters.
    const uint8_t golden[] = {0xA9, 0x00, 0x02, 0x01, 0x01, 0x02, 0x19, 0xFF, 0xFF, 0x03, 0x18,
                              0x3A, 0x04, 0x11, 0x05, 0xF4, 0x06, 0x02, 0x07, 0x00, 0x08, 0x69,
                              'A',  'B',  'C',  'D',  'E',  'F',  'G',  'H',  'I'};
    CHECK(frame == std::string(reinterpret_cast<const char*>(golden), sizeof(golden)));
    CHECK(frame.size() == 31);
    CHECK(config.link_drops() == 0);

    const std::string body = link_text(frame);
    CHECK(body.find("\"alarm_enabled\":false") != std::string::npos);
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

    explicit UpdateBench(bool allows_downgrade, ports::ImageVersion version = dfu::kWidestVersion,
                         uint16_t payload_bytes = kSmallestSupportedPayloadBytes) {
        link.raise_link(1);
        link.raise_link(1);
        link.declare_payload_bytes(payload_bytes);
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

}  // namespace

TEST_CASE(
    "comms: the update report fits it too, widest versions, a settings fallback, a hub image "
    "and a key") {
    UpdateBench bench(false);
    REQUIRE(bench.link.sent.size() == 1);
    // The widest release build's report, every key written and the swap refused for power.
    CHECK(bench.link.last().bytes.size() == 57);
    const std::string body = link_text(bench.link.last().bytes);
    CHECK(body.find("\"swap_powered\":false") != std::string::npos);
    CHECK(body.find("\"from\":\"9.99.99+99999\"") != std::string::npos);
    CHECK(body.find("\"to\":\"9.99.99+99999\"") != std::string::npos);
    CHECK(body.find("\"settings\":\"defaults\"") != std::string::npos);
    CHECK(body.find("\"imu\":\"held\",\"imu_digest\":\"abababababababab\"") != std::string::npos);
    CHECK(body.find("\"key\":\"ffffffff\"") != std::string::npos);
    CHECK(bench.config.link_drops() == 0);
}

TEST_CASE(
    "comms: an update report too wide for the payload is refused small_mtu and counted, never "
    "sent without its key") {
    // The widest development report is 59 bytes, two more than the release one for its downgrade.
    UpdateBench fits(true, dfu::kWidestVersion, 59);
    CHECK(fits.link.last().bytes.size() == 59);
    UpdateBench bench(true, dfu::kWidestVersion, 58);
    REQUIRE(bench.link.sent.size() == 1);
    CHECK(link_text(bench.link.last().bytes) == kSmallMtuRefusal);
    CHECK(bench.config.link_drops() == 1);
}

// The push races the MTU exchange at every connect, so a refusal there would be noise.
TEST_CASE("comms: an update pushed at link-up that does not fit is counted, never refused") {
    platform::host::Link link;
    link.declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    link.raise_link(1);
    go::Settings settings = widest_settings();
    go::SettingsStore store(settings, kWidestAddress);
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
    go::Settings settings = widest_settings();
    go::SettingsStore store(settings, kWidestAddress);
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
    CHECK(link_text(wide.last().bytes).find("\"part\":0") != std::string::npos);
    CHECK(link_text(wide.last().bytes).find("\"more\":false") != std::string::npos);

    // A link a third that wide gets several whole frames and the thirteen fields between them.
    const uint16_t kNarrowBytes = 64;
    on_wide.on_rx(config_frame("{\"command\":\"timing\"}"));
    CHECK(wide.last().bytes.size() <= static_cast<size_t>(kSmallestSupportedPayloadBytes));
    platform::host::Link narrow;
    narrow.raise_link(1);
    narrow.declare_payload_bytes(kNarrowBytes);
    go::SettingsStore store_on_narrow(settings, kWidestAddress);
    ConfigService on_narrow(narrow, store_on_narrow, nullptr, &statistics);
    on_narrow.on_rx(config_frame("{\"command\":\"timing\"}"));
    REQUIRE(narrow.sent.size() > 1);
    CHECK(link_text(narrow.sent.back().bytes).find("\"more\":false") != std::string::npos);
    for (size_t i = 0; i < narrow.sent.size(); i++) {
        CHECK(narrow.sent[i].bytes.size() <= kNarrowBytes);
        const std::string body = link_text(narrow.sent[i].bytes);
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
    hopeless.declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    go::SettingsStore store_on_hopeless(settings, kWidestAddress);
    ConfigService on_hopeless(hopeless, store_on_hopeless, nullptr, &statistics);
    on_hopeless.on_rx(config_frame("{\"command\":\"timing\"}"));
    REQUIRE(hopeless.sent.size() == 1);
    CHECK(link_text(hopeless.last().bytes) == kSmallMtuRefusal);
    CHECK(on_hopeless.link_drops() == 1);
}

// The widest the bench can make it: every histogram bucket and counter written in five bytes.
TEST_CASE("comms: the widest timing report is 147 bytes, one frame on an iPhone's link") {
    timing::SlotTimingStatistics statistics;
    run_widest_bench(statistics);
    for (int i = 0; i < timing::SlotTimingStatistics::kBuckets; i++) {
        CHECK(statistics.pps_bucket(i) >= 65536u);
        CHECK(statistics.dwell_bucket(i) >= 65536u);
    }
    CHECK(statistics.holdover_events() == 65536u);
    CHECK(statistics.pps_worst_us() <= -65536);
    CHECK(statistics.dwell_worst_us() <= -65536);
    go::Settings settings = go::defaults();

    platform::host::Link iphone;
    iphone.raise_link(1);
    iphone.declare_payload_bytes(kSmallestSupportedPayloadBytes);
    go::SettingsStore store_on_iphone(settings, kWidestAddress);
    ConfigService on_iphone(iphone, store_on_iphone, nullptr, &statistics);
    on_iphone.on_rx(config_frame("{\"command\":\"timing\"}"));
    REQUIRE(iphone.sent.size() == 1);
    // 1 + command 2 + part 2 + more 2 + two histograms of 1 + 1 + 7 x 5 + eleven fields of 1 + 5.
    CHECK(iphone.last().bytes.size() == 147);

    platform::host::Link one_short;
    one_short.raise_link(1);
    one_short.declare_payload_bytes(146);
    go::SettingsStore store_on_one_short(settings, kWidestAddress);
    ConfigService on_one_short(one_short, store_on_one_short, nullptr, &statistics);
    on_one_short.on_rx(config_frame("{\"command\":\"timing\"}"));
    CHECK(one_short.sent.size() == 2);
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
    CHECK(link_text(link.last().bytes).find("\"command\":\"status\"") != std::string::npos);
    CHECK(config.link_drops() == 1);

    // And a second pass does not send it again: a retry is not a stream.
    config.tick(20);
    CHECK(link.sent.size() == 1);
}

// The widest status is 20 bytes, so even a central that never exchanged its MTU hears every push.
TEST_CASE("comms: the widest status push fits the twenty bytes every link guarantees") {
    platform::host::Link link;
    link.raise_link(1);
    link.raise_link(1);
    link.declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    go::Settings settings = go::defaults();
    go::SettingsStore store(settings, kWidestAddress);
    ConfigService config(link, store);
    config.on_link_up(events::LinkUp{1, link.payload_bytes()});
    config.set_die_temperature(INT16_MIN, true);
    make_worst_case(config);

    REQUIRE_FALSE(link.sent.empty());
    CHECK(link.last().bytes.size() == ports::kMinimumLinkPayloadBytes);
    CHECK(link_text(link.last().bytes).find("\"die_temperature_c\":-3277}") != std::string::npos);
    CHECK(config.link_drops() == 0);
}

// The notify share cut a report at its first refusal, and the phone waited on "more".
TEST_CASE("comms: a report the link takes one frame at a time arrives whole and in order") {
    timing::SlotTimingStatistics statistics;
    run_bench(statistics);
    go::Settings settings = go::defaults();

    platform::host::Link free_link;
    free_link.raise_link(1);
    free_link.declare_payload_bytes(64);
    go::SettingsStore store_free(settings, kWidestAddress);
    ConfigService on_free(free_link, store_free, nullptr, &statistics);
    on_free.on_rx(config_frame("{\"command\":\"timing\"}"));
    REQUIRE(free_link.sent.size() > 1);

    platform::host::Link busy;
    busy.raise_link(1);
    busy.declare_payload_bytes(64);
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
    CHECK(link_text(link.last().bytes).find("\"command\":\"status\"") != std::string::npos);
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
