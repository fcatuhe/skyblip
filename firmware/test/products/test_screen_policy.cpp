// The refresh policy over the real SSD1681 driver: partials only, every screen through black.
#include "core/events/input.h"
#include "test/support/screen_rig.h"

TEST_CASE("screen policy: a static frame is never re-presented") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    CHECK(rig.chip.present_count == 2);  // the boot black, then the page
    CHECK_FALSE(rig.chip.last_full);

    rig.run_seconds(now_ms, 30);  // nothing on screen changes
    CHECK(rig.chip.present_count == 2);
}

TEST_CASE("screen policy: a minute of changing frames costs partials and no full") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);  // boot full

    rig.churn(now_ms, 60);
    CHECK(rig.chip.present_count > 1);
    CHECK_FALSE(rig.chip.last_full);
}

// SoftRF runs this glass on partials alone: the full waveform is power on and power off, no more.
TEST_CASE("screen policy: hours of changing frames never cost a full refresh") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);  // boot full
    const int boot = rig.chip.present_count;

    rig.churn_at(now_ms, 60000, 180);
    CHECK(rig.chip.present_count > boot);
    CHECK_FALSE(rig.chip.last_full);
}

TEST_CASE("screen policy: a page change goes through black, not through the full waveform") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    const int before = rig.chip.present_count;

    rig.screen.next_page();
    rig.tick(now_ms += 1000);
    CHECK(rig.chip.present_count == before + 1);
    CHECK_FALSE(rig.chip.last_full);
    CHECK(rig.glass_all_black());

    // The page behind it does not wait out the one-a-second floor.
    rig.tick(now_ms += 600);
    CHECK(rig.chip.present_count == before + 2);
    CHECK_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.last_full);
}

// The rails carry the ~140 ms power-down: skipping it is the whole saving of the shorter wipe.
TEST_CASE("screen policy: a wipe leaves the rails up, and the page behind it puts them down") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);

    rig.screen.next_page();
    rig.tick(now_ms += 1000);
    REQUIRE(rig.glass_all_black());
    CHECK(rig.chip.rails_on);

    // 500 ms of partial waveform less the 140 ms tail the wipe does not spend
    rig.tick(now_ms += 400);
    REQUIRE_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: a second page change on a black glass costs no second wipe") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    const int before = rig.chip.present_count;

    rig.screen.next_page();
    rig.tick(now_ms += 1000);
    REQUIRE(rig.glass_all_black());

    rig.screen.next_page();
    rig.tick(now_ms += 600);
    CHECK(rig.chip.present_count == before + 2);
    CHECK_FALSE(rig.glass_all_black());
    CHECK(rig.screen.page() == go::Page::SixPack);
}

TEST_CASE("screen policy: a fix arriving is a data change, presented as a partial") {
    ScreenRig rig;
    rig.state.own.fix_valid = false;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    const int before = rig.chip.present_count;

    rig.state.own.fix_valid = true;
    rig.tick(now_ms += 1000);
    CHECK(rig.chip.present_count == before + 1);
    CHECK_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.last_full);
}

// A radar ghosted over the page under it is worse than 360 ms of black the pilot asked for.
TEST_CASE("screen policy: the loudest alarm standing still gets the page its black") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);

    rig.alarm(traffic::Level::Advisory);
    rig.screen.next_page();
    rig.tick(now_ms += 1000);
    CHECK(rig.glass_all_black());
    CHECK_FALSE(rig.chip.last_full);

    rig.tick(now_ms += 400);
    CHECK_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.last_full);
}

// A pilot reading the sixpack is a pilot who cannot see the bearing the alarm is about.
TEST_CASE("screen policy: converging traffic takes a flying page back to the radar") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    rig.screen.next_page();
    rig.run_seconds(now_ms, 2);
    REQUIRE(rig.screen.page() != go::Page::Radar);

    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.run_seconds(now_ms, 2);
    CHECK(rig.screen.page() == go::Page::Radar);

    // Taken back once, not held: the pages are still a pilot's to walk under a standing alarm.
    rig.screen.next_page();
    rig.run_seconds(now_ms, 2);
    CHECK(rig.screen.page() != go::Page::Radar);
}

// A diagnostics page is read on a bench, where there is no sky to be taken back to.
TEST_CASE("screen policy: converging traffic leaves the diagnostics pages standing") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    rig.show(now_ms, go::Page::Status);
    rig.run_seconds(now_ms, 2);
    REQUIRE(rig.screen.page() == go::Page::Status);

    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.run_seconds(now_ms, 2);
    CHECK(rig.screen.page() == go::Page::Status);
}

// The diagnostics menu is the only way to those pages, so an alarm that closed it would seal them.
TEST_CASE("screen policy: converging traffic leaves the diagnostics menu standing") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    rig.tap(now_ms);
    REQUIRE(rig.screen.page() == go::Page::Nearby);
    rig.press(now_ms);
    rig.run_seconds(now_ms, 2);
    REQUIRE(rig.screen.editor().page() == go::Page::Nearby);

    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.run_seconds(now_ms, 2);
    CHECK(rig.screen.mode() == go::Mode::Menu);
    CHECK(rig.screen.editor().active());
}

// The settings mode keeps the button to itself, so it has to give it back unasked.
TEST_CASE("screen policy: converging traffic takes the settings mode back off the glass") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    rig.bus.input.push(events::ContactEvent{events::Contact::Button, true, now_ms});
    rig.bus.input.push(events::ContactEvent{events::Contact::Button, false, now_ms + 100});
    rig.run_seconds(now_ms, 2);
    REQUIRE(rig.screen.mode() == go::Mode::Menu);
    REQUIRE(rig.screen.editor().active());

    // A bearing worth turning the head for: the menu goes and the traffic picture comes back.
    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.run_seconds(now_ms, 2);
    CHECK(rig.screen.mode() == go::Mode::Page);
    CHECK(rig.screen.page() == go::Page::Radar);
    CHECK_FALSE(rig.screen.editor().active());
    CHECK_FALSE(rig.chip.last_full);
}

TEST_CASE("screen policy: presents wait for the panel, none is issued mid-refresh") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.tick(now_ms += 1000);  // boot full: the glass is busy 2.5 s
    const int count = rig.chip.present_count;

    rig.state.own.satellites = 8;  // a visible change, right away
    rig.screen.mark_dirty();
    rig.tick(now_ms += 100);  // 1.1 s: full not settled yet
    CHECK(rig.chip.present_count == count);

    rig.tick(now_ms += 2700);  // settled: the pending change lands
    CHECK(rig.chip.present_count == count + 1);
}

// A wipe is 360 ms of black over the bearing the pilot was just told to look at.
TEST_CASE("screen policy: the long touch that silences an alarm costs no wipe and no page") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    rig.screen.next_page();
    rig.run_seconds(now_ms, 2);
    REQUIRE(rig.screen.page() != go::Page::Radar);

    rig.threat(traffic::Level::Advisory, 0, 1500, now_ms);
    rig.run_seconds(now_ms, 2);
    const go::Page chosen = rig.screen.page();

    rig.long_touch(now_ms);
    CHECK(rig.screen.page() == chosen);
    CHECK_FALSE(rig.glass_all_black());

    // Silenced, the same hold is the way home again.
    rig.dismiss();
    rig.run_seconds(now_ms, 2);
    rig.long_touch(now_ms);
    CHECK(rig.screen.page() == go::Page::Radar);
}

// Nothing may hide a page a board can draw: the radar is home and the nearby menu opens the rest.
TEST_CASE("screen policy: the walk is every picture the board can draw, and every tap changes it") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);

    const go::Page walk[3] = {go::Page::Nearby, go::Page::SixPack, go::Page::Radar};
    for (go::Page expected : walk) {
        rig.screen.next_page();
        rig.run_seconds(now_ms, 2);
        CHECK(rig.screen.page() == expected);
    }
}

// A plain T-Echo has no inertial sensor, so the g-meter is not one of its pictures.
TEST_CASE("screen policy: the g-meter is on the walk only where the sensor is fitted") {
    ScreenRig rig;
    rig.roles.capabilities = rig.roles.capabilities | ports::Capability::Inclinometer;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);

    const go::Page walk[4] = {go::Page::Nearby, go::Page::SixPack, go::Page::GMeter,
                              go::Page::Radar};
    for (go::Page expected : walk) {
        rig.screen.next_page();
        rig.run_seconds(now_ms, 2);
        CHECK(rig.screen.page() == expected);
    }
}

// The wipe the pilot's hold asks for, made unasked over the bearing they were just told to look at.
TEST_CASE("screen policy: an alarm arriving on a radar already read leaves the glass standing") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    REQUIRE(rig.screen.page() == go::Page::Radar);

    rig.threat(go::ScreenService::kAlarmTakesGlass, 0, 1500, now_ms);
    rig.tick(now_ms += 1000);
    CHECK_FALSE(rig.glass_all_black());
    CHECK(rig.screen.page() == go::Page::Radar);
}

// Months of the same rings burn into the glass, and this hold is the pilot's way to scrub them.
TEST_CASE("screen policy: the long touch on the radar takes the picture through black") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    REQUIRE(rig.screen.page() == go::Page::Radar);
    const int settled = rig.chip.present_count;

    rig.long_touch(now_ms);
    CHECK(rig.glass_all_black());

    rig.run_seconds(now_ms, 2);
    CHECK(rig.screen.page() == go::Page::Radar);
    CHECK_FALSE(rig.glass_all_black());
    CHECK(rig.chip.present_count == settled + 2);
    CHECK_FALSE(rig.chip.last_full);
}

// The wedge flips once per frame presented, so the present floor is the blink rate.
TEST_CASE("screen policy: an alarm flashes the wedge at a flip a second") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    const int settled = rig.chip.present_count;

    rig.threat(traffic::Level::Advisory, 0, 1500, now_ms);
    for (int i = 0; i < 100; i++)
        rig.tick(now_ms += 100);  // ten seconds, sampled ten times a second
    const int flips = rig.chip.present_count - settled;
    CHECK(flips >= 9);
    CHECK(flips <= 11);

    // Dismissed, the picture stands still: one last frame, then nothing.
    rig.dismiss();
    for (int i = 0; i < 20; i++) rig.tick(now_ms += 100);
    const int held = rig.chip.present_count;
    for (int i = 0; i < 100; i++) rig.tick(now_ms += 100);
    CHECK(rig.chip.present_count == held);
}

namespace {

constexpr uint32_t kPassMs = 11;
constexpr uint32_t kSecondMs = 1000;

void locked_tick(ScreenRig& rig, uint32_t now_ms) {
    rig.state.clock.utc_valid = true;
    rig.state.clock.pps_locked = true;
    rig.state.clock.since_edge_ms = now_ms % kSecondMs;
    rig.tick(now_ms);
}

bool on_the_redraw_phase(uint32_t now_ms) {
    return now_ms % kSecondMs >= go::ScreenService::kLockedRedrawWindowStartMs &&
           now_ms % kSecondMs < go::ScreenService::kLockedRedrawWindowStartMs + kPassMs;
}

int locked_presents(ScreenRig& rig, uint32_t& now_ms, uint32_t for_ms) {
    int presents = 0;
    for (const uint32_t end = now_ms + for_ms; now_ms < end; now_ms += kPassMs) {
        if (now_ms % kSecondMs < kPassMs) rig.state.flight.seconds += 60;
        const int before = rig.chip.present_count;
        locked_tick(rig, now_ms);
        if (rig.chip.present_count == before) continue;
        presents++;
        CAPTURE(now_ms);
        CHECK(on_the_redraw_phase(now_ms));
    }
    return presents;
}

}  // namespace

// Bench 2026-09-29: a redraw a pass after the last walked ~7 ms/s into the fix and the uplink
// dwell.
TEST_CASE("screen policy: under a PPS lock a moving page is redrawn once a second, on one phase") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);

    const int presents = locked_presents(rig, now_ms, 100 * kSecondMs);
    CHECK(presents >= 99);
    CHECK(presents <= 101);
}

// Every present used to ask for the next, so a flashing wedge set its own drifting cadence.
TEST_CASE("screen policy: under a PPS lock an alarm flashes the wedge on the redraw phase") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    rig.threat(traffic::Level::Advisory, 0, 1500, now_ms);
    for (const uint32_t end = now_ms + 2 * kSecondMs; now_ms < end; now_ms += kPassMs)
        locked_tick(rig, now_ms);

    int flips = 0;
    for (const uint32_t end = now_ms + 60 * kSecondMs; now_ms < end; now_ms += kPassMs) {
        const int before = rig.chip.present_count;
        locked_tick(rig, now_ms);
        if (rig.chip.present_count == before) continue;
        flips++;
        CAPTURE(now_ms);
        CHECK(on_the_redraw_phase(now_ms));
    }
    CHECK(flips >= 59);
    CHECK(flips <= 61);
}

// A present just ahead of the phase holds the glass busy through it: that second is lost, no more.
TEST_CASE("screen policy: under a PPS lock a redraw the busy glass refused lands the next second") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    for (const uint32_t end = now_ms + 3 * kSecondMs; now_ms < end; now_ms += kPassMs)
        locked_tick(rig, now_ms);

    now_ms =
        (now_ms / kSecondMs + 1) * kSecondMs + go::ScreenService::kLockedRedrawWindowStartMs - 50;
    const int before = rig.chip.present_count;
    rig.state.flight.seconds += 60;
    rig.screen.mark_dirty();
    locked_tick(rig, now_ms);
    REQUIRE(rig.chip.present_count == before + 1);
    now_ms += kPassMs;

    // the second the glass was busy through, then four on the phase
    CHECK(locked_presents(rig, now_ms, 5 * kSecondMs) == 4);
}

// The screen record rode the same drifting rule, so a corpus showed it walking through the second.
TEST_CASE("screen policy: under a PPS lock the screen is recorded once a second, on one phase") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.recorder.arm();
    rig.run_seconds(now_ms, 3);
    diagnostics::Record record{};
    while (rig.recorder.peek(record)) rig.recorder.commit();

    int recorded = 0;
    for (const uint32_t end = now_ms + 30 * kSecondMs; now_ms < end; now_ms += kPassMs) {
        locked_tick(rig, now_ms);
        while (rig.recorder.peek(record)) {
            rig.recorder.commit();
            if (record.type != diagnostics::Type::Screen) continue;
            recorded++;
            CAPTURE(now_ms);
            CHECK(record.phase_valid());
            CHECK(record.into_ms >= go::ScreenService::kLockedRedrawWindowStartMs);
            CHECK(record.into_ms < go::ScreenService::kLockedRedrawWindowStartMs + kPassMs);
        }
    }
    CHECK(recorded >= 29);
    CHECK(recorded <= 31);
}

// A pass held up past the phase would push the glass into the next second's fix.
TEST_CASE(
    "screen policy: under a PPS lock a pass that reaches the phase too late leaves it alone") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.run_seconds(now_ms, 3);
    locked_presents(rig, now_ms, 3 * kSecondMs);

    now_ms = (now_ms / kSecondMs + 1) * kSecondMs;
    locked_tick(rig, now_ms + go::ScreenService::kLockedRedrawWindowStartMs - 10);
    const int before = rig.chip.present_count;
    rig.state.flight.seconds += 60;
    locked_tick(rig, now_ms + go::ScreenService::kLockedRedrawWindowEndMs);
    CHECK(rig.chip.present_count == before);

    now_ms += kSecondMs;
    CHECK(locked_presents(rig, now_ms, kSecondMs) == 1);
}
