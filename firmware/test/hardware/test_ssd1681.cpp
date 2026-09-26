// SSD1681 e-paper driver tests against models/ssd1681.h. Verifies the init
// sequence, the framebuffer to RAM polarity (fb 1=black becomes panel 0=black), the
// two-bank differential contract (previous image in 0x26, new in 0x24), the
// non-blocking present/ready cycle, the rails down after every refresh but the wipe,
// and the hung-BUSY recovery, all on the host, no panel required.
#include <string>

#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"

using namespace skyblip;

namespace {

parts::Ssd1681 make(models::Ssd1681& f) { return parts::Ssd1681(f, f, f, f.dc, f.rst, f.busy); }

parts::Ssd1681 make_turned(models::Ssd1681& f) {
    return parts::Ssd1681(f, f, f, f.dc, f.rst, f.busy, -1, parts::GlassRotation::Deg270);
}

// One pixel of panel RAM, addressed as the controller does: a source on a gate line, black at 0.
bool ram_black(const models::Ssd1681& f, int source, int gate) {
    const size_t byte = size_t(gate) * parts::Ssd1681Glass::kStride + size_t(source >> 3);
    return byte < f.ram.size() && (f.ram[byte] & (0x80 >> (source & 7))) == 0;
}

// Drives the present to ready cycle to completion, as the screen service would
// across ticks.
void settle(parts::Ssd1681& d, uint32_t issued_ms) {
    CHECK_FALSE(d.ready(issued_ms));
    CHECK(d.ready(issued_ms + parts::Ssd1681::kReadyAfterFullMs));
}

}  // namespace

TEST_CASE("epd: begin() runs the SSD1681 init sequence and resets the panel") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    CHECK(f.reset_pulses >= 1);
    CHECK(f.saw_cmd(0x12));  // SW reset
    CHECK(f.saw_cmd(0x01));  // driver output control
    CHECK(f.saw_cmd(0x11));  // data entry mode
    CHECK(f.saw_cmd(0x3C));  // border waveform
    CHECK(f.saw_cmd(0x18));  // temperature sensor
}

// 10 ms as a literal: the test this replaced compared epd::kResetHoldSpins with itself.
TEST_CASE("epd: RES# is held low for the vendor's 10 ms, at begin and at every wake") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    CHECK(f.reset_low_us >= 10000);
    CHECK(f.short_resets == 0);

    parts::Ssd1681Glass fb;
    fb.clear(true);
    d.present(fb, ports::Refresh::Full, 0);
    settle(d, 0);
    d.power_off();
    REQUIRE_FALSE(f.powered);

    f.reset_low_us = 0;
    d.present(fb, ports::Refresh::Partial, 5000);
    CHECK(f.reset_low_us >= 10000);
    CHECK(f.powered);
    CHECK(f.short_resets == 0);
}

// A glitched RES# leaves a deep-sleeping SSD1681 asleep, BUSY high, first image forever.
TEST_CASE("epd: a RES# pulse one microsecond under 10 ms leaves a sleeping panel asleep") {
    models::Ssd1681 f;
    const uint8_t deep_sleep = 0x10;
    f.set(f.dc, false);
    f.transfer(&deep_sleep, nullptr, 1);
    REQUIRE_FALSE(f.powered);

    f.set(f.rst, false);
    f.wait_at_least_us(9999);
    f.set(f.rst, true);
    CHECK_FALSE(f.powered);
    CHECK(f.short_resets == 1);

    f.set(f.rst, false);
    f.wait_at_least_us(10000);
    f.set(f.rst, true);
    CHECK(f.powered);
    CHECK(f.reset_pulses == 1);
}

TEST_CASE("epd: present() writes a full framebuffer with correct black/white polarity") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    parts::Ssd1681Glass fb;
    fb.clear(/*white=*/true);
    d.present(fb, ports::Refresh::Full, 0);

    CHECK(f.ram.size() == parts::Ssd1681Glass::kBytes);
    // An all-white framebuffer becomes all 0xFF in panel RAM (inverted).
    bool all_ff = true;
    for (uint8_t b : f.ram)
        if (b != 0xFF) all_ff = false;
    CHECK(all_ff);
    CHECK(f.saw_cmd(0x24));  // WriteRAM
    CHECK(f.saw_cmd(0x20));  // Master activation
}

TEST_CASE("epd: a black pixel flips the corresponding RAM bit to 0") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    parts::Ssd1681Glass fb;
    fb.clear(true);
    fb.set_pixel(0, 0, /*black=*/true);
    d.present(fb, ports::Refresh::Full, 0);

    // First RAM byte now has at least one cleared bit (was 0xFF all-white).
    CHECK(f.ram[0] != 0xFF);
}

TEST_CASE("epd: an unturned glass takes framebuffer rows as gate lines") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    parts::Ssd1681Glass fb;
    fb.clear(true);
    fb.set_pixel(0, 0, true);
    fb.set_pixel(199, 0, true);
    fb.set_pixel(3, 8, true);
    d.present(fb, ports::Refresh::Full, 0);

    REQUIRE(f.ram.size() == parts::Ssd1681Glass::kBytes);
    CHECK(ram_black(f, 0, 0));
    CHECK(ram_black(f, 199, 0));
    CHECK(ram_black(f, 3, 8));
    CHECK_FALSE(ram_black(f, 0, 199));
}

// The Plus mounts the glass a quarter turn off the scan: unturned here, the device reads sideways.
TEST_CASE("epd: a glass turned 270 degrees takes framebuffer columns as gate lines") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make_turned(f);
    d.begin();

    parts::Ssd1681Glass fb;
    fb.clear(true);
    fb.set_pixel(0, 0, true);
    fb.set_pixel(199, 0, true);
    fb.set_pixel(3, 8, true);
    d.present(fb, ports::Refresh::Full, 0);

    REQUIRE(f.ram.size() == parts::Ssd1681Glass::kBytes);
    // (x, y) lands on source y of gate line 199 - x.
    CHECK(ram_black(f, 0, 199));
    CHECK(ram_black(f, 0, 0));
    CHECK(ram_black(f, 8, 196));
    CHECK_FALSE(ram_black(f, 199, 0));
}

TEST_CASE("epd: a turned glass writes the same count of black pixels it was handed") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make_turned(f);
    d.begin();

    parts::Ssd1681Glass fb;
    fb.clear(true);
    for (int i = 0; i < 200; i++) fb.set_pixel(i, i / 2, true);
    d.present(fb, ports::Refresh::Full, 0);

    int black = 0;
    for (int gate = 0; gate < parts::Ssd1681::kGlassH; gate++)
        for (int source = 0; source < parts::Ssd1681::kGlassW; source++)
            if (ram_black(f, source, gate)) black++;
    CHECK(black == fb.count_black());
}

// A partial against an unknown glass diffs against garbage, so a session's first frame is full.
TEST_CASE("epd: the first present after begin() is a full refresh, whatever was asked") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    d.present(fb, ports::Refresh::Partial, 0);
    CHECK(f.last_full);

    settle(d, 0);
    d.present(fb, ports::Refresh::Partial, 5000);
    CHECK_FALSE(f.last_full);
}

// The rail is cut at power off, so both banks come up garbage: this frame needs neither.
TEST_CASE("epd: paint_black drives every pixel from white, on the partial waveform") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    d.paint_black(0);
    CHECK_FALSE(f.last_full);
    REQUIRE(f.ram_previous.size() == parts::Ssd1681Glass::kBytes);
    REQUIRE(f.ram.size() == parts::Ssd1681Glass::kBytes);
    for (uint8_t b : f.ram_previous) REQUIRE(b == 0xFF);  // panel RAM 1 is white
    for (uint8_t b : f.ram) REQUIRE(b == 0x00);
    CHECK(f.framebuffer().count_black() == parts::Ssd1681::kGlassW * parts::Ssd1681::kGlassH);
}

// The wipe is never the last frame: the page behind it spends the rails it left up.
TEST_CASE("epd: paint_black leaves the rails up, and the frame behind it takes them down") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    d.paint_black(0);
    CHECK(f.rails_on);
    CHECK(d.ready(parts::Ssd1681::kReadyAfterPartialMs));

    parts::Ssd1681Glass fb;
    fb.clear(true);
    d.present(fb, ports::Refresh::Partial, 1000);
    CHECK_FALSE(f.rails_on);
}

// A panel switched off under a wipe must not wear the bias while it waits.
TEST_CASE("epd: power_off() takes down the rails a wipe left up") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    d.paint_black(0);
    REQUIRE(f.rails_on);
    d.power_off();
    CHECK_FALSE(f.rails_on);
}

// The black is the session's first frame, so what follows it is a partial and not a second full.
TEST_CASE("epd: a paint_black leaves the glass known, and the page after it is a partial") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    d.paint_black(0);
    CHECK(d.ready(parts::Ssd1681::kReadyAfterPartialMs));
    parts::Ssd1681Glass fb;
    fb.clear(true);
    d.present(fb, ports::Refresh::Partial, 1000);
    CHECK_FALSE(f.last_full);
    for (uint8_t b : f.ram_previous) REQUIRE(b == 0x00);  // the black it was left on
}

// The simulator draws this: a partial that changed no pixel is invisible on glass.
TEST_CASE("epd: the panel says which refresh is in flight, and for how long") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);
    CHECK_FALSE(d.refreshing());

    d.present(fb, ports::Refresh::Full, 0);
    CHECK(d.refreshing());
    CHECK(d.refresh_mode() == ports::Refresh::Full);
    settle(d, 0);
    CHECK_FALSE(d.refreshing());

    d.present(fb, ports::Refresh::Partial, 5000);
    CHECK(d.refreshing());
    CHECK(d.refresh_mode() == ports::Refresh::Partial);
    CHECK_FALSE(d.ready(5000 + parts::Ssd1681::kReadyAfterPartialMs - 1));
    CHECK(d.refreshing());
    CHECK(d.ready(5000 + parts::Ssd1681::kReadyAfterPartialMs));
    CHECK_FALSE(d.refreshing());
}

TEST_CASE("epd: present() rewrites the previous-image bank so the panel diffs the truth") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    parts::Ssd1681Glass first;
    first.clear(true);
    first.set_pixel(10, 10, true);
    d.present(first, ports::Refresh::Full, 0);
    settle(d, 0);

    parts::Ssd1681Glass second;
    second.clear(true);
    second.set_pixel(20, 20, true);
    d.present(second, ports::Refresh::Partial, 5000);

    // Bank 0x26 must hold what the glass shows (the first frame) and bank
    // 0x24 the new one, both in panel polarity. A stale or empty 0x26 is the
    // classic partial-update ghosting bug.
    REQUIRE(f.ram_previous.size() == parts::Ssd1681Glass::kBytes);
    REQUIRE(f.ram.size() == parts::Ssd1681Glass::kBytes);
    parts::Ssd1681Glass glass;
    for (size_t i = 0; i < parts::Ssd1681Glass::kBytes; i++)
        glass.data()[i] = static_cast<uint8_t>(~f.ram_previous[i]);
    CHECK(glass.get_pixel(10, 10));
    CHECK_FALSE(glass.get_pixel(20, 20));
}

// GxEPD2 writeImageForFullRefresh: the wash reads both banks, and old against new adds inversions.
TEST_CASE("epd: a full refresh puts the new frame in both banks, not the old one in 0x26") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    parts::Ssd1681Glass first;
    first.clear(true);
    first.set_pixel(10, 10, true);
    d.present(first, ports::Refresh::Full, 0);
    settle(d, 0);

    parts::Ssd1681Glass second;
    second.clear(true);
    second.set_pixel(20, 20, true);
    d.present(second, ports::Refresh::Full, 5000);
    REQUIRE(f.ram.size() == parts::Ssd1681Glass::kBytes);
    CHECK(f.ram_previous == f.ram);
}

// Waveshare and ESPHome move VBD off the transition LUT for partials; at 0x05 the border greys.
TEST_CASE("epd: the border follows the waveform on a wash and is held at VCOM on a partial") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    d.present(fb, ports::Refresh::Full, 0);
    CHECK(f.border == 0x05);
    settle(d, 0);

    fb.set_pixel(5, 5, true);
    d.present(fb, ports::Refresh::Partial, 5000);
    CHECK(f.border == 0x80);
}

// Ink migrates under the bias a powered panel holds, and in the sun it migrates fast.
TEST_CASE("epd: every refresh ends with the rails down, the partial as well as the wash") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    d.present(fb, ports::Refresh::Full, 0);
    settle(d, 0);
    CHECK_FALSE(f.rails_on);

    fb.set_pixel(5, 5, true);
    d.present(fb, ports::Refresh::Partial, 5000);
    CHECK(d.ready(5000 + parts::Ssd1681::kReadyAfterPartialMs));
    CHECK_FALSE(f.rails_on);

    fb.set_pixel(6, 6, true);
    d.present(fb, ports::Refresh::Partial, 10000);
    CHECK(d.ready(10000 + parts::Ssd1681::kReadyAfterPartialMs));
    CHECK_FALSE(f.rails_on);
}

// Rails down is not deep sleep: the panel keeps its registers, so the next partial needs no reset.
TEST_CASE("epd: a run of partials costs one reset, not one per refresh") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    d.present(fb, ports::Refresh::Full, 0);
    settle(d, 0);
    const int resets_before = f.reset_pulses;

    for (int i = 0; i < 5; i++) {
        fb.set_pixel(10 + i, 10, true);
        const uint32_t t = 5000 + uint32_t(i) * 1000;
        d.present(fb, ports::Refresh::Partial, t);
        CHECK(d.ready(t + parts::Ssd1681::kReadyAfterPartialMs));
    }
    CHECK(f.reset_pulses == resets_before);
    CHECK(f.powered);
    CHECK(f.present_count == 6);
}

TEST_CASE("epd: present() is non-blocking and ready() settles the panel without sleeping it") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    CHECK(d.ready(0));
    d.present(fb, ports::Refresh::Full, 1000);

    // Not ready before the panel can plausibly have finished, even though the
    // model's BUSY pin is already low.
    CHECK_FALSE(d.ready(1000));
    CHECK_FALSE(d.ready(1000 + parts::Ssd1681::kReadyAfterFullMs - 1));

    const int sleeps_before = f.deep_sleeps;
    CHECK(d.ready(1000 + parts::Ssd1681::kReadyAfterFullMs));
    CHECK(f.deep_sleeps == sleeps_before);
    CHECK_FALSE(f.rails_on);
}

TEST_CASE("epd: a present after deep sleep wakes the panel with a reset pulse") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    d.present(fb, ports::Refresh::Full, 0);
    settle(d, 0);
    d.power_off();
    CHECK_FALSE(f.powered);

    const int resets_before = f.reset_pulses;
    fb.set_pixel(50, 50, true);
    d.present(fb, ports::Refresh::Partial, 5000);
    CHECK(f.reset_pulses == resets_before + 1);
    CHECK(f.powered);
    CHECK(f.present_count == 2);
}

TEST_CASE("epd: a hung BUSY line times out, re-initialises, and forces the next full") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    d.present(fb, ports::Refresh::Full, 0);
    settle(d, 0);

    d.present(fb, ports::Refresh::Partial, 5000);
    f.busy_stuck = true;
    CHECK_FALSE(d.ready(5000 + parts::Ssd1681::kReadyAfterPartialMs));
    CHECK_FALSE(d.ready(5000 + parts::Ssd1681::kBusyTimeoutMs - 1));

    const int resets_before = f.reset_pulses;
    CHECK(d.ready(5000 + parts::Ssd1681::kBusyTimeoutMs));
    CHECK(f.reset_pulses > resets_before);

    // The glass is unknown after the recovery: the next present must be full.
    f.busy_stuck = false;
    d.present(fb, ports::Refresh::Partial, 20000);
    CHECK(f.last_full);
}

// The rails are the thing to get down on a panel that wedged mid-refresh, and RES# does it.
TEST_CASE("epd: the panel a hung BUSY left mid-refresh is re-initialised with its rails down") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    d.present(fb, ports::Refresh::Partial, 0);
    f.busy_stuck = true;
    const int sleeps_before = f.deep_sleeps;
    REQUIRE(d.ready(parts::Ssd1681::kBusyTimeoutMs));
    CHECK_FALSE(f.rails_on);
    CHECK(f.deep_sleeps == sleeps_before);
    CHECK(f.powered);
}

TEST_CASE("epd: a panel that never released BUSY loses its shadow, so the next refresh is full") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    d.present(fb, ports::Refresh::Full, 0);
    f.busy_stuck = true;
    const int resets_before = f.reset_pulses;
    d.power_off();

    f.busy_stuck = false;
    d.present(fb, ports::Refresh::Partial, 60000);
    CHECK(f.reset_pulses > resets_before);
    CHECK(f.last_full);
}

// Deep sleep is the switched-off state now, so nothing about the last waveform may refuse it.
TEST_CASE("epd: power_off() sleeps the panel whatever waveform ran last") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    d.present(fb, ports::Refresh::Full, 0);
    settle(d, 0);
    fb.set_pixel(9, 9, true);
    d.present(fb, ports::Refresh::Partial, 1000);
    d.power_off();
    CHECK_FALSE(f.powered);
    CHECK_FALSE(f.rails_on);
}

// The path the shutdown actually takes: a full park frame, and then it may sleep.
TEST_CASE("epd: power_off() after the full park frame leaves nothing powered") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    d.present(fb, ports::Refresh::Full, 0);
    settle(d, 0);
    fb.clear(true);
    d.present(fb, ports::Refresh::Full, 1000);
    d.power_off();
    CHECK_FALSE(f.powered);
    CHECK_FALSE(f.rails_on);
}

TEST_CASE("epd: power_off() parks a sleeping panel without touching it twice") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    d.present(fb, ports::Refresh::Full, 0);
    settle(d, 0);
    d.power_off();
    const int sleeps_before = f.deep_sleeps;
    d.power_off();
    CHECK(f.deep_sleeps == sleeps_before);
    CHECK_FALSE(f.powered);
}

TEST_CASE("epd: the five signatures shipped in T-Echos are told apart") {
    CHECK(parts::identify_panel(parts::panels::kGdeh0154D67Syx1942) ==
          parts::Panel::Gdeh0154D67Syx1942);
    CHECK(parts::identify_panel(parts::panels::kGdeh0154D67Syx2118) ==
          parts::Panel::Gdeh0154D67Syx2118);
    CHECK(parts::identify_panel(parts::panels::kGdeh0154D67Syx2129) ==
          parts::Panel::Gdeh0154D67Syx2129);
    CHECK(parts::identify_panel(parts::panels::kDepg0150Bn) == parts::Panel::Depg0150Bn);
    CHECK(parts::identify_panel(parts::panels::kGdep015Oc1) == parts::Panel::Gdep015Oc1);
    CHECK(parts::identify_panel(parts::panels::kElecrowM1) == parts::Panel::ElecrowM1);
}

TEST_CASE("epd: 2118 and 2129 are the same in 0x2D and only 0x2E separates them") {
    // The reason the ident reads both registers. A table keyed on 0x2D alone
    // would answer this question with whichever row it happened to try first.
    for (int i = 0; i < parts::kPanelIdBytesA; i++)
        CHECK(parts::panels::kGdeh0154D67Syx2118.a[i] == parts::panels::kGdeh0154D67Syx2129.a[i]);

    bool b_differs = false;
    for (int i = 0; i < parts::kPanelIdBytesB; i++)
        if (parts::panels::kGdeh0154D67Syx2118.b[i] != parts::panels::kGdeh0154D67Syx2129.b[i])
            b_differs = true;
    CHECK(b_differs);
}

TEST_CASE("epd: a signature nobody has recorded is unlisted, and an unread one is neither") {
    // The seventh row of SoftRF's table is the Plus - our own board - and it
    // carries no bytes at all, only the string "20.05.21". So this is the answer
    // this board is expected to give until somebody reads one on a bench.
    parts::PanelSignature unrecorded{};
    unrecorded.read = true;
    unrecorded.a[0] = 0x12;
    unrecorded.b[3] = 0x34;
    CHECK(parts::identify_panel(unrecorded) == parts::Panel::Unlisted);

    // A fingerprint that was never taken is a different miss: nobody read it.
    parts::PanelSignature never_taken{};
    CHECK(parts::identify_panel(never_taken) == parts::Panel::Unknown);
    // Even when the bytes would otherwise match a shipped panel: an unread
    // register file is all zeroes, and all zeroes is DEPG0150BN.
    parts::PanelSignature zeroes = parts::panels::kDepg0150Bn;
    zeroes.read = false;
    CHECK(parts::identify_panel(zeroes) == parts::Panel::Unknown);
}

TEST_CASE("epd: the identity is a name the self-test page can print") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();
    CHECK(std::string(d.panel_name()) == "NO ID");

    d.adopt(parts::panels::kDepg0150Bn);
    CHECK(std::string(d.panel_name()) == "DEPG0150");

    // Short enough to share a row with a name and a verdict inside 200 pixels.
    for (int i = 0; i <= int(parts::Panel::ElecrowM1); i++) {
        const char* name = parts::panel_name(static_cast<parts::Panel>(i));
        CHECK(std::string(name).size() <= 8);
    }
}

// A canvas of another size would be written as if it were this glass: same bank,
// same window, rows sheared by the stride it does not have.
TEST_CASE("epd: a canvas that is not this glass never reaches the panel") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    ui::Panel<128, 64> other;
    other.clear(/*white=*/false);
    const int presents = f.present_count;
    d.present(other, ports::Refresh::Full, 0);

    CHECK_FALSE(parts::Ssd1681::drives(other));
    CHECK(f.present_count == presents);
}
