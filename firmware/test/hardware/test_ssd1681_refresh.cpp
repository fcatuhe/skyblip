// The two-bank differential contract (previous image in 0x26, new in 0x24), the
// non-blocking present/ready cycle, the rails down after every refresh but the wipe,
// and the hung-BUSY recovery.
#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "test/support/ssd1681_rig.h"

using namespace skyblip;

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
