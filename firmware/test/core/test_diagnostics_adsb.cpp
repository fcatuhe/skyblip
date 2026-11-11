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
    diagnostics.adsb_invalid = 120;
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

TEST_CASE("diagnostics adsb: a unit with no 1090 MHz receiver prints and sends no adsb group") {
    const Diagnostics go{};
    DiagnosticsReport report(go, link::Command::Diagnostics);
    CHECK(report.line_count() == DiagnosticsReport::kGroupCount - 1);
    char line[DiagnosticsReport::kLineBytes];
    for (int i = 0; i < report.line_count(); i++) {
        REQUIRE(report.line(i, line, static_cast<int>(sizeof(line))) > 0);
        CHECK_FALSE(has(line, "adsb"));
    }
    DiagnosticsReport alone(go, link::Command::Diagnostics, DiagnosticsReport::Group::Adsb);
    CHECK(alone.exhausted());
}

TEST_CASE("diagnostics adsb: the receiver's counters and its RSSI histogram reach both surfaces") {
    const Diagnostics diagnostics = receiving();
    DiagnosticsReport console(diagnostics, link::Command::Diagnostics,
                              DiagnosticsReport::Group::Adsb);
    char line[DiagnosticsReport::kLineBytes];
    REQUIRE(console.line(0, line, static_cast<int>(sizeof(line))) > 0);
    CHECK(std::string(line) ==
          "adsb frames=900 frame_rate_hz=31 invalid=120 dropped=2 rearms=3 resets=1 "
          "command_errors=5 errors=128 rssi_dbm=0,1,2,3,4,5,6,7");

    DiagnosticsReport link(diagnostics, link::Command::Diagnostics, DiagnosticsReport::Group::Adsb);
    const std::string frame = only_adsb_frame(link);
    CHECK(has(frame, "\"group\":\"adsb\""));
    CHECK(has(frame, "\"frames\":900"));
    CHECK(has(frame, "\"invalid\":120"));
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

// 1 + four 2-byte heads + seven counters of 7 + errors 5 + the array's 2 + 1 + 8 x 5, under the
// radio's 112.
TEST_CASE("diagnostics adsb: the widest adsb frame is 106 bytes, narrower than the radio's") {
    Diagnostics diagnostics = receiving();
    diagnostics.adsb.frames = diagnostics.adsb.frame_rate_hz = diagnostics.adsb_invalid =
        0xFFFFFFFFu;
    diagnostics.adsb.dropped = diagnostics.adsb.rearms = diagnostics.adsb.resets = 0xFFFFFFFFu;
    diagnostics.adsb.command_errors = 0xFFFFFFFFu;
    diagnostics.adsb.device_errors = 0xFFFF;
    std::fill(std::begin(diagnostics.adsb.rssi), std::end(diagnostics.adsb.rssi), 0xFFFFFFFFu);
    DiagnosticsReport report(diagnostics, link::Command::Diagnostics,
                             DiagnosticsReport::Group::Adsb);
    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    const int written = report.next_frame(512, buffer, static_cast<int>(sizeof(buffer)));
    CHECK(written == 106);
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
