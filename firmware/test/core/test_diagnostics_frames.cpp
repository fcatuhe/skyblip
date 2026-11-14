// The status dump as link frames: whole, inside the negotiated payload, one subsystem a frame.
#include <string>

#include "core/comms/config.h"  // kSmallestSupportedPayloadBytes: the ceiling both surfaces answer under
#include "core/comms/diagnostics.h"
#include "core/comms/link_registry.h"
#include "doctest/doctest.h"
#include "test/support/diagnostics_report.h"

using namespace skyblip;
using namespace skyblip::comms;

// The narrowest phone in the field carries 182 bytes of notification payload, and
// a frame one byte over that is not shortened by the controller - it fails. So the
// dump is cut into frames the payload can carry, and every frame is a complete
// object naming the subsystem it belongs to.
TEST_CASE("diagnostics: the link form is frames the negotiated payload can carry, whole") {
    const Diagnostics diagnostics = widest_device();
    const Frames iphone = frames(diagnostics, kSmallestSupportedPayloadBytes);
    CHECK(iphone.widest <= kSmallestSupportedPayloadBytes);
    CHECK(iphone.count == DiagnosticsReport::kGroupCount);
    const Frames narrow = frames(diagnostics, 64);
    CHECK(narrow.widest <= 64);
    CHECK(narrow.count > DiagnosticsReport::kGroupCount);  // the widest values need a second pass
    CHECK(has(narrow.last, "\"more\":false"));
    // Only the last frame says there is nothing after it.
    size_t at = 0;
    int finals = 0;
    while ((at = narrow.joined.find("\"more\":false", at)) != std::string::npos) {
        finals++;
        at += 1;
    }
    CHECK(finals == 1);

    // A link wide enough for everything answers in one frame per subsystem, and
    // never fewer: a reader that only wants the radio stops at its group.
    const Frames wide = frames(busy_device(), 512);
    CHECK(wide.count == DiagnosticsReport::kGroupCount);
    CHECK(has(wide.joined, "\"group\":\"system\""));
    CHECK(has(wide.joined, "\"group\":\"radio\""));
    CHECK(has(wide.joined, "\"group\":\"traffic\""));
    CHECK(has(wide.joined, "\"group\":\"gnss\""));
    CHECK(has(wide.joined, "\"group\":\"power\""));
}

TEST_CASE("diagnostics: a frame never mixes two subsystems") {
    DiagnosticsReport report(widest_device(), link::Command::Diagnostics);
    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    while (!report.exhausted()) {
        const int written =
            report.next_frame(kSmallestSupportedPayloadBytes, buffer, sizeof(buffer));
        REQUIRE(written > 0);
        const std::string body = text(buffer, written);
        // The radio's keys cannot appear in a frame that says it is the receiver's,
        // whichever way round the report packed them.
        if (has(body, "\"group\":\"gnss\"")) {
            CHECK_FALSE(has(body, "rx_ok"));
            CHECK_FALSE(has(body, "noise_dbm"));
        }
        if (has(body, "\"group\":\"radio\"")) {
            CHECK_FALSE(has(body, "firmware"));
            CHECK_FALSE(has(body, "\"baud\""));
        }
    }
}

// All or nothing, asked before the first frame goes out: a bench holding four
// frames of a five-frame dump, with no fifth one coming, has been told that a
// fault it cannot see is absent.
TEST_CASE("diagnostics: a link too narrow for one field is refused, not answered short") {
    const Diagnostics diagnostics = widest_device();
    DiagnosticsReport report(diagnostics, link::Command::Diagnostics);
    CHECK(report.fits(kSmallestSupportedPayloadBytes));
    // What BLE guarantees cannot carry the receiver's 23-character banner and the frame's keys.
    CHECK_FALSE(report.fits(20));

    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    DiagnosticsReport narrow(diagnostics, link::Command::Diagnostics);
    CHECK(narrow.next_frame(12, buffer, sizeof(buffer)) == 0);
    CHECK_FALSE(narrow.exhausted());
}

// The radio group, alone in the radio reply and in its frame of the whole dump.
TEST_CASE("diagnostics: the widest radio frame is 141 bytes, one frame on an iPhone's link") {
    for (link::Command command : {link::Command::Diagnostics, link::Command::Radio}) {
        DiagnosticsReport report(widest_device(), command, DiagnosticsReport::Group::Radio);
        uint8_t buffer[DiagnosticsReport::kFrameBytes];
        // A 2-byte map head past 23 entries + four 2-byte heads + noise 3 + duty 4 + twelve
        // counters of 6 + two of 4 + 2 + six of 7, four of them keyed past 23.
        CHECK(report.next_frame(kSmallestSupportedPayloadBytes, buffer, sizeof(buffer)) == 141);
        CHECK(report.exhausted());
    }
}

TEST_CASE("diagnostics: one subsystem can be asked for on its own, under its own name") {
    const Diagnostics diagnostics = busy_device();
    DiagnosticsReport report(diagnostics, link::Command::Radio, DiagnosticsReport::Group::Radio);
    CHECK(report.line_count() == 1);

    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    std::string body;
    while (!report.exhausted()) {
        const int written =
            report.next_frame(kSmallestSupportedPayloadBytes, buffer, sizeof(buffer));
        REQUIRE(written > 0);
        body += text(buffer, written);
    }
    CHECK(has(body, "\"command\":\"radio\""));
    CHECK(has(body, "\"group\":\"radio\""));
    CHECK(has(body, "\"range_refused\":5"));
    // And nothing from any other subsystem came with it.
    CHECK_FALSE(has(body, "uptime_s"));
    CHECK_FALSE(has(body, "firmware"));
}
