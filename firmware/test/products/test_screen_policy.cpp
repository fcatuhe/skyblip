// The refresh policy over the real SSD1681 driver: partials only, every screen through black.
#include "core/events/input.h"
#include "test/support/screen_rig.h"

TEST_CASE("screen policy: a static frame is never re-presented") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    CHECK(rig.chip.present_count == 2);  // the boot black, then the page
    CHECK_FALSE(rig.chip.last_full);

    rig.run_seconds(t, 30);  // nothing on screen changes
    CHECK(rig.chip.present_count == 2);
}

TEST_CASE("screen policy: a minute of changing frames costs partials and no full") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);  // boot full

    rig.churn(t, 60);
    CHECK(rig.chip.present_count > 1);
    CHECK_FALSE(rig.chip.last_full);
}

// SoftRF runs this glass on partials alone: the full waveform is power on and power off, no more.
TEST_CASE("screen policy: hours of changing frames never cost a full refresh") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);  // boot full
    const int boot = rig.chip.present_count;

    rig.churn_at(t, 60000, 180);
    CHECK(rig.chip.present_count > boot);
    CHECK_FALSE(rig.chip.last_full);
}

TEST_CASE("screen policy: a page change goes through black, not through the full waveform") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.screen.next_page();
    rig.tick(t += 1000);
    CHECK(rig.chip.present_count == before + 1);
    CHECK_FALSE(rig.chip.last_full);
    CHECK(rig.glass_all_black());

    // The page behind it does not wait out the one-a-second floor.
    rig.tick(t += 600);
    CHECK(rig.chip.present_count == before + 2);
    CHECK_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.last_full);
}

// The rails carry the ~140 ms power-down: skipping it is the whole saving of the shorter wipe.
TEST_CASE("screen policy: a wipe leaves the rails up, and the page behind it puts them down") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.screen.next_page();
    rig.tick(t += 1000);
    REQUIRE(rig.glass_all_black());
    CHECK(rig.chip.rails_on);

    // 500 ms of partial waveform less the 140 ms tail the wipe does not spend
    rig.tick(t += 400);
    REQUIRE_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: a second page change on a black glass costs no second wipe") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.screen.next_page();
    rig.tick(t += 1000);
    REQUIRE(rig.glass_all_black());

    rig.screen.next_page();
    rig.tick(t += 600);
    CHECK(rig.chip.present_count == before + 2);
    CHECK_FALSE(rig.glass_all_black());
    CHECK(rig.screen.page() == go::Page::SixPack);
}

TEST_CASE("screen policy: a fix arriving is a data change, presented as a partial") {
    Rig rig;
    rig.state.own.fix_valid = false;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int before = rig.chip.present_count;

    rig.state.own.fix_valid = true;
    rig.tick(t += 1000);
    CHECK(rig.chip.present_count == before + 1);
    CHECK_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.last_full);
}

// A radar ghosted over the page under it is worse than 360 ms of black the pilot asked for.
TEST_CASE("screen policy: the loudest alarm standing still gets the page its black") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    rig.alarm(traffic::Level::Advisory);
    rig.screen.next_page();
    rig.tick(t += 1000);
    CHECK(rig.glass_all_black());
    CHECK_FALSE(rig.chip.last_full);

    rig.tick(t += 400);
    CHECK_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.last_full);
}

// A pilot reading the sixpack is a pilot who cannot see the bearing the alarm is about.
TEST_CASE("screen policy: converging traffic takes a flying page back to the radar") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.screen.next_page();
    rig.run_seconds(t, 2);
    REQUIRE(rig.screen.page() != go::Page::Radar);

    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.run_seconds(t, 2);
    CHECK(rig.screen.page() == go::Page::Radar);

    // Taken back once, not held: the pages are still a pilot's to walk under a standing alarm.
    rig.screen.next_page();
    rig.run_seconds(t, 2);
    CHECK(rig.screen.page() != go::Page::Radar);
}

// A diagnostics page is read on a bench, where there is no sky to be taken back to.
TEST_CASE("screen policy: converging traffic leaves the diagnostics pages standing") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.show(t, go::Page::Status);
    rig.run_seconds(t, 2);
    REQUIRE(rig.screen.page() == go::Page::Status);

    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.run_seconds(t, 2);
    CHECK(rig.screen.page() == go::Page::Status);
}

// The diagnostics menu is the only way to those pages, so an alarm that closed it would seal them.
TEST_CASE("screen policy: converging traffic leaves the diagnostics menu standing") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.tap(t);
    REQUIRE(rig.screen.page() == go::Page::Nearby);
    rig.press(t);
    rig.run_seconds(t, 2);
    REQUIRE(rig.screen.editor().page() == go::Page::Nearby);

    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.run_seconds(t, 2);
    CHECK(rig.screen.mode() == go::Mode::Menu);
    CHECK(rig.screen.editor().active());
}

// The settings mode keeps the button to itself, so it has to give it back unasked.
TEST_CASE("screen policy: converging traffic takes the settings mode back off the glass") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.bus.input.push(events::ContactEvent{events::Contact::Button, true, t});
    rig.bus.input.push(events::ContactEvent{events::Contact::Button, false, t + 100});
    rig.run_seconds(t, 2);
    REQUIRE(rig.screen.mode() == go::Mode::Menu);
    REQUIRE(rig.screen.editor().active());

    // A bearing worth turning the head for: the menu goes and the traffic picture comes back.
    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.run_seconds(t, 2);
    CHECK(rig.screen.mode() == go::Mode::Page);
    CHECK(rig.screen.page() == go::Page::Radar);
    CHECK_FALSE(rig.screen.editor().active());
    CHECK_FALSE(rig.chip.last_full);
}

TEST_CASE("screen policy: presents wait for the panel, none is issued mid-refresh") {
    Rig rig;
    uint32_t t = 0;
    rig.tick(t += 1000);  // boot full: the glass is busy 2.5 s
    const int count = rig.chip.present_count;

    rig.state.own.sats = 8;  // a visible change, right away
    rig.screen.mark_dirty();
    rig.tick(t += 100);  // 1.1 s: full not settled yet
    CHECK(rig.chip.present_count == count);

    rig.tick(t += 2700);  // settled: the pending change lands
    CHECK(rig.chip.present_count == count + 1);
}

// A wipe is 360 ms of black over the bearing the pilot was just told to look at.
TEST_CASE("screen policy: the long touch that silences an alarm costs no wipe and no page") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.screen.next_page();
    rig.run_seconds(t, 2);
    REQUIRE(rig.screen.page() != go::Page::Radar);

    rig.threat(traffic::Level::Advisory, 0, 1500, t);
    rig.run_seconds(t, 2);
    const go::Page chosen = rig.screen.page();

    rig.long_touch(t);
    CHECK(rig.screen.page() == chosen);
    CHECK_FALSE(rig.glass_all_black());

    // Silenced, the same hold is the way home again.
    rig.dismiss();
    rig.run_seconds(t, 2);
    rig.long_touch(t);
    CHECK(rig.screen.page() == go::Page::Radar);
}

// Nothing may hide a page a board can draw: the radar is home and the nearby menu opens the rest.
TEST_CASE("screen policy: the walk is every picture the board can draw, and every tap changes it") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    const go::Page walk[3] = {go::Page::Nearby, go::Page::SixPack, go::Page::Radar};
    for (go::Page expected : walk) {
        rig.screen.next_page();
        rig.run_seconds(t, 2);
        CHECK(rig.screen.page() == expected);
    }
}

// A plain T-Echo has no inertial sensor, so the g-meter is not one of its pictures.
TEST_CASE("screen policy: the g-meter is on the walk only where the sensor is fitted") {
    Rig rig;
    rig.roles.capabilities = rig.roles.capabilities | ports::Capability::Inclinometer;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    const go::Page walk[4] = {go::Page::Nearby, go::Page::SixPack, go::Page::GMeter,
                              go::Page::Radar};
    for (go::Page expected : walk) {
        rig.screen.next_page();
        rig.run_seconds(t, 2);
        CHECK(rig.screen.page() == expected);
    }
}

// The wipe the pilot's hold asks for, made unasked over the bearing they were just told to look at.
TEST_CASE("screen policy: an alarm arriving on a radar already read leaves the glass standing") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    REQUIRE(rig.screen.page() == go::Page::Radar);

    rig.threat(go::ScreenService::kAlarmTakesGlass, 0, 1500, t);
    rig.tick(t += 1000);
    CHECK_FALSE(rig.glass_all_black());
    CHECK(rig.screen.page() == go::Page::Radar);
}

// Months of the same rings burn into the glass, and this hold is the pilot's way to scrub them.
TEST_CASE("screen policy: the long touch on the radar takes the picture through black") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    REQUIRE(rig.screen.page() == go::Page::Radar);
    const int settled = rig.chip.present_count;

    rig.long_touch(t);
    CHECK(rig.glass_all_black());

    rig.run_seconds(t, 2);
    CHECK(rig.screen.page() == go::Page::Radar);
    CHECK_FALSE(rig.glass_all_black());
    CHECK(rig.chip.present_count == settled + 2);
    CHECK_FALSE(rig.chip.last_full);
}

// The wedge flips once per frame presented, so the present floor is the blink rate.
TEST_CASE("screen policy: an alarm flashes the wedge at a flip a second") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    const int settled = rig.chip.present_count;

    rig.threat(traffic::Level::Advisory, 0, 1500, t);
    for (int i = 0; i < 100; i++) rig.tick(t += 100);  // ten seconds, sampled ten times a second
    const int flips = rig.chip.present_count - settled;
    CHECK(flips >= 9);
    CHECK(flips <= 11);

    // Dismissed, the picture stands still: one last frame, then nothing.
    rig.dismiss();
    for (int i = 0; i < 20; i++) rig.tick(t += 100);
    const int held = rig.chip.present_count;
    for (int i = 0; i < 100; i++) rig.tick(t += 100);
    CHECK(rig.chip.present_count == held);
}

namespace {

constexpr uint32_t kPassMs = 11;
constexpr uint32_t kSecondMs = 1000;

void locked_tick(Rig& rig, uint32_t t) {
    rig.state.clock.utc_valid = true;
    rig.state.clock.pps_locked = true;
    rig.state.clock.ms_since_pps = t % kSecondMs;
    rig.tick(t);
}

bool on_the_redraw_phase(uint32_t t) {
    return t % kSecondMs >= go::ScreenService::kLockedRedrawMs &&
           t % kSecondMs < go::ScreenService::kLockedRedrawMs + kPassMs;
}

int locked_presents(Rig& rig, uint32_t& t, uint32_t ms) {
    int presents = 0;
    for (const uint32_t end = t + ms; t < end; t += kPassMs) {
        if (t % kSecondMs < kPassMs) rig.state.flight.seconds += 60;
        const int before = rig.chip.present_count;
        locked_tick(rig, t);
        if (rig.chip.present_count == before) continue;
        presents++;
        CAPTURE(t);
        CHECK(on_the_redraw_phase(t));
    }
    return presents;
}

}  // namespace

// Bench 2026-09-29: a redraw a pass after the last walked ~7 ms/s into the fix and the uplink
// dwell.
TEST_CASE("screen policy: under a PPS lock a moving page is redrawn once a second, on one phase") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);

    const int presents = locked_presents(rig, t, 100 * kSecondMs);
    CHECK(presents >= 99);
    CHECK(presents <= 101);
}

// Every present used to ask for the next, so a flashing wedge set its own drifting cadence.
TEST_CASE("screen policy: under a PPS lock an alarm flashes the wedge on the redraw phase") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    rig.threat(traffic::Level::Advisory, 0, 1500, t);
    for (const uint32_t end = t + 2 * kSecondMs; t < end; t += kPassMs) locked_tick(rig, t);

    int flips = 0;
    for (const uint32_t end = t + 60 * kSecondMs; t < end; t += kPassMs) {
        const int before = rig.chip.present_count;
        locked_tick(rig, t);
        if (rig.chip.present_count == before) continue;
        flips++;
        CAPTURE(t);
        CHECK(on_the_redraw_phase(t));
    }
    CHECK(flips >= 59);
    CHECK(flips <= 61);
}

// A present just ahead of the phase holds the glass busy through it: that second is lost, no more.
TEST_CASE("screen policy: under a PPS lock a redraw the busy glass refused lands the next second") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    for (const uint32_t end = t + 3 * kSecondMs; t < end; t += kPassMs) locked_tick(rig, t);

    t = (t / kSecondMs + 1) * kSecondMs + go::ScreenService::kLockedRedrawMs - 50;
    const int before = rig.chip.present_count;
    rig.state.flight.seconds += 60;
    rig.screen.mark_dirty();
    locked_tick(rig, t);
    REQUIRE(rig.chip.present_count == before + 1);
    t += kPassMs;

    // the second the glass was busy through, then four on the phase
    CHECK(locked_presents(rig, t, 5 * kSecondMs) == 4);
}

// The screen record rode the same drifting rule, so a corpus showed it walking through the second.
TEST_CASE("screen policy: under a PPS lock the screen is recorded once a second, on one phase") {
    Rig rig;
    uint32_t t = 0;
    rig.recorder.arm();
    rig.run_seconds(t, 3);
    diag::Record record{};
    while (rig.recorder.peek(record)) rig.recorder.commit();

    int recorded = 0;
    for (const uint32_t end = t + 30 * kSecondMs; t < end; t += kPassMs) {
        locked_tick(rig, t);
        while (rig.recorder.peek(record)) {
            rig.recorder.commit();
            if (record.type != diag::Type::Screen) continue;
            recorded++;
            CAPTURE(t);
            CHECK(record.phase_valid());
            CHECK(record.into_ms >= go::ScreenService::kLockedRedrawMs);
            CHECK(record.into_ms < go::ScreenService::kLockedRedrawMs + kPassMs);
        }
    }
    CHECK(recorded >= 29);
    CHECK(recorded <= 31);
}

// A pass held up past the phase would push the glass into the next second's fix.
TEST_CASE(
    "screen policy: under a PPS lock a pass that reaches the phase too late leaves it alone") {
    Rig rig;
    uint32_t t = 0;
    rig.run_seconds(t, 3);
    locked_presents(rig, t, 3 * kSecondMs);

    t = (t / kSecondMs + 1) * kSecondMs;
    locked_tick(rig, t + go::ScreenService::kLockedRedrawMs - 10);
    const int before = rig.chip.present_count;
    rig.state.flight.seconds += 60;
    locked_tick(rig, t + go::ScreenService::kLockedRedrawEndMs);
    CHECK(rig.chip.present_count == before);

    t += kSecondMs;
    CHECK(locked_presents(rig, t, kSecondMs) == 1);
}
