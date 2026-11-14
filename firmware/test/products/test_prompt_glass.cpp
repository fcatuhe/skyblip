// What the briefing shows of a prompt is the Go's own page, drawn from what the phone sent.
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/frame.h"
#include "simulator/world/prompt_glass.h"
#include "test/support/frame_glass.h"

using namespace skyblip;

namespace {

bool same(const go::Glass& a, const go::Glass& b) {
    return std::memcmp(a.data(), b.data(), go::Glass::kBytes) == 0;
}

go::Glass confirm(comms::Pending pending, const char* detail, uint32_t seconds) {
    go::ConfirmSnapshot snapshot;
    snapshot.title = comms::pending_title(pending);
    snapshot.detail = detail;
    snapshot.seconds_left = seconds;
    go::Glass glass;
    go::draw_confirm(glass, snapshot);
    return glass;
}

}  // namespace

TEST_CASE("prompt glass: a set reads its changes against the unit's settings, as the Go does") {
    go::Glass glass;
    REQUIRE(simulator::draw_prompt(
        glass, "set", "{\"command\":\"set\",\"callsign\":\"F-JXYZ\",\"units\":\"metric\"}",
        "{\"command\":\"set\",\"callsign\":\"F-JABC\",\"units\":\"nautical\"}", 27));
    CHECK(same(glass, confirm(comms::Pending::Set, "CALLSIGN F-JXYZ\nUNITS METRIC", 27)));

    // A key the unit already holds is no change, and is not listed.
    REQUIRE(simulator::draw_prompt(
        glass, "set", "{\"command\":\"set\",\"callsign\":\"F-JABC\",\"units\":\"metric\"}",
        "{\"command\":\"set\",\"callsign\":\"F-JABC\",\"units\":\"nautical\"}", 27));
    CHECK(same(glass, confirm(comms::Pending::Set, "UNITS METRIC", 27)));
}

TEST_CASE("prompt glass: a dfu names the version the phone sends, as the Go does") {
    go::Glass glass;
    REQUIRE(simulator::draw_prompt(glass, "dfu", "0.2.0+15", "", 30));
    char detail[comms::kPromptDetailBytes];
    ports::ImageVersion version;
    version.minor = 2;
    version.build = 15;
    comms::format_dfu_detail(detail, sizeof(detail), version);
    CHECK(same(glass, confirm(comms::Pending::Dfu, detail, 30)));
    CHECK(reads_heading(glass, "FIRMWARE", go::Heading::Interrupt));
}

TEST_CASE("prompt glass: every other prompt is the Go's title and detail for it") {
    const struct {
        const char* name;
        comms::Pending pending;
    } prompts[] = {
        {"recovery", comms::Pending::Recovery},  {"power_off", comms::Pending::PowerOff},
        {"erase_log", comms::Pending::EraseLog}, {"gnss_cold_start", comms::Pending::GnssColdStart},
        {"register", comms::Pending::Register},  {"capture", comms::Pending::Capture}};
    for (const auto& prompt : prompts) {
        CAPTURE(prompt.name);
        go::Glass glass;
        REQUIRE(simulator::draw_prompt(glass, prompt.name, "", "", 12));
        CHECK(same(glass, confirm(prompt.pending, comms::pending_detail(prompt.pending), 12)));
    }
}

TEST_CASE("prompt glass: a prompt the Go never raises, or a frame it cannot read, draws nothing") {
    go::Glass glass;
    CHECK_FALSE(simulator::draw_prompt(glass, "launch", "", "", 30));
    CHECK_FALSE(simulator::draw_prompt(glass, "dfu", "not a version", "", 30));
    CHECK_FALSE(simulator::draw_prompt(glass, "set", "{\"command\":\"set\"", "{}", 30));
}

// A screen reader cannot read the canvas, so the page's words go with it, in reading order.
TEST_CASE("prompt glass: the words a screen reader hears are the page's, key last") {
    go::Glass glass;
    char words[simulator::kPromptWordsBytes];
    REQUIRE(simulator::draw_prompt(
        glass, "set", "{\"command\":\"set\",\"callsign\":\"F-JXYZ\",\"units\":\"metric\"}",
        "{\"command\":\"set\",\"callsign\":\"F-JABC\"}", 27, words, sizeof(words)));
    CHECK(std::string(words) ==
          "SETTINGS. CALLSIGN F-JXYZ, UNITS METRIC. TO CANCEL, PUSH ONCE OR WAIT 27 S. PUSH TWICE "
          "TO ACCEPT.");

    REQUIRE(simulator::draw_prompt(glass, "power_off", "", "", 0, words, sizeof(words)));
    CHECK(std::string(words) ==
          "POWER OFF. STOP THE DEVICE. TO CANCEL, PUSH ONCE. PUSH TWICE TO ACCEPT.");
}
