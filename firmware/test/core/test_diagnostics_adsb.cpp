// The 1090 MHz receiver's group of the status dump: only on a unit that has one, and a snapshot.
#include <algorithm>
#include <iterator>
#include <optional>
#include <string>

#include "core/comms/diagnostics.h"
#include "core/comms/link_registry.h"
#include "doctest/doctest.h"
#include "test/support/config_frame.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {

Diagnostics receiving() {
    Diagnostics diagnostics{};
    diagnostics.adsb_fitted = true;
    diagnostics.adsb.frames = 900;
    diagnostics.adsb.frame_rate_hz = 31;
    diagnostics.adsb_crc = 120;
    diagnostics.adsb_unsupported = 14;
    diagnostics.adsb_folded = 766;
    diagnostics.adsb_aircraft = 9;
    diagnostics.adsb.dropped = 2;
    diagnostics.adsb.rearms = 3;
    diagnostics.adsb.resets = 1;
    diagnostics.adsb.command_errors = 5;
    diagnostics.adsb.device_errors = 0x0080;
    const uint32_t levels[ports::AdsbReceiverStatistics::kRssiBuckets] = {0, 1, 2, 3, 4, 5, 6, 7};
    std::copy(std::begin(levels), std::end(levels), std::begin(diagnostics.adsb.rssi));
    return diagnostics;
}

std::string only_adsb_frame(DiagnosticsReport& report) {
    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    const int written = report.next_frame(512, buffer, static_cast<int>(sizeof(buffer)));
    REQUIRE(written > 0);
    CHECK(report.exhausted());
    return link_text(std::string(reinterpret_cast<const char*>(buffer), written));
}

bool has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("diagnostics adsb: a unit with no 1090 MHz receiver sends no adsb group") {
    const Diagnostics go{};
    DiagnosticsReport report(go, link::Command::Diagnostics);
    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    while (!report.exhausted()) {
        const int written = report.next_frame(512, buffer, static_cast<int>(sizeof(buffer)));
        REQUIRE(written > 0);
        // INFO: fc 14nov26 REQUIRE does not stop this build, so a frame that is not cut ends it
        if (written <= 0) break;
        const std::string frame =
            link_text(std::string(reinterpret_cast<const char*>(buffer), written));
        CHECK_FALSE(has(frame, "\"group\":\"adsb\""));
        CHECK_FALSE(has(frame, "\"group\":\"capture\""));
    }
    DiagnosticsReport alone(go, link::Command::Diagnostics, DiagnosticsReport::Group::Adsb);
    CHECK(alone.exhausted());
}

TEST_CASE("diagnostics adsb: the receiver's counters and its RSSI histogram reach the link") {
    const Diagnostics diagnostics = receiving();
    DiagnosticsReport link(diagnostics, link::Command::Diagnostics, DiagnosticsReport::Group::Adsb);
    const std::string frame = only_adsb_frame(link);
    CHECK(has(frame, "\"group\":\"adsb\""));
    CHECK(has(frame, "\"frames\":900"));
    CHECK(has(frame, "\"errors\":128"));
    CHECK(has(frame, "\"crc\":120"));
    CHECK(has(frame, "\"unsupported\":14"));
    CHECK(has(frame, "\"folded\":766"));
    CHECK(has(frame, "\"aircraft\":9"));
    CHECK(has(frame, "\"rssi_dbm\":[0,1,2,3,4,5,6,7]"));
}

// A refresh lands every second while a phone may still be reading the last dump's frames.
TEST_CASE("diagnostics adsb: the histogram is copied when the dump is built") {
    Diagnostics diagnostics = receiving();
    DiagnosticsReport report(diagnostics, link::Command::Diagnostics,
                             DiagnosticsReport::Group::Adsb);
    std::fill(std::begin(diagnostics.adsb.rssi), std::end(diagnostics.adsb.rssi), 99u);
    CHECK(has(only_adsb_frame(report), "\"rssi_dbm\":[0,1,2,3,4,5,6,7]"));
}

// 127 for the receiver, then formats' 2 + 2 + 25 x 5 and four counters of 7, past 182 bytes.
TEST_CASE("diagnostics adsb: the widest adsb group is 284 bytes, two frames on an iPhone") {
    Diagnostics diagnostics = receiving();
    diagnostics.adsb.frames = diagnostics.adsb.frame_rate_hz = 0xFFFFFFFFu;
    diagnostics.adsb_crc = diagnostics.adsb_unsupported = 0xFFFFFFFFu;
    diagnostics.adsb_folded = diagnostics.adsb_aircraft = 0xFFFFFFFFu;
    diagnostics.adsb.dropped = diagnostics.adsb.rearms = diagnostics.adsb.resets = 0xFFFFFFFFu;
    diagnostics.adsb.command_errors = 0xFFFFFFFFu;
    diagnostics.adsb.device_errors = 0xFFFF;
    std::fill(std::begin(diagnostics.adsb.rssi), std::end(diagnostics.adsb.rssi), 0xFFFFFFFFu);
    std::fill(std::begin(diagnostics.adsb_formats), std::end(diagnostics.adsb_formats),
              0xFFFFFFFFu);
    diagnostics.adsb_transponders = diagnostics.adsb_mode_s = 0xFFFFFFFFu;
    diagnostics.adsb_mode_s_altitude = diagnostics.adsb_unmatched = 0xFFFFFFFFu;
    DiagnosticsReport report(diagnostics, link::Command::Diagnostics,
                             DiagnosticsReport::Group::Adsb);
    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    const int written = report.next_frame(512, buffer, static_cast<int>(sizeof(buffer)));
    CHECK(written == 284);
}

// The config service holds a report in a std::optional, so a copy must carry its own counts.
TEST_CASE("diagnostics adsb: a copied dump carries its own histogram") {
    std::optional<DiagnosticsReport> held;
    {
        const DiagnosticsReport built(receiving(), link::Command::Diagnostics,
                                      DiagnosticsReport::Group::Adsb);
        held = built;
    }
    CHECK(has(only_adsb_frame(*held), "\"rssi_dbm\":[0,1,2,3,4,5,6,7]"));
}
