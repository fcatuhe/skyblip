// What the panel's limits do to the refresh policy: held hot, stopped on a dying rail, parked.
#include "test/support/screen_rig.h"

// go::Glass is rated 0 to 50 C, and the Go is designed against a 72.4 C parked-cockpit soak.
TEST_CASE("screen policy: a panel too hot to drive is left unrefreshed, with its rails down") {
    ScreenRig rig;
    rig.run_seconds(3);
    const int before = rig.chip.present_count;

    rig.die_temperature(go::ScreenService::kRefreshCeilingDeciCelsius + 1);
    rig.churn(20);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

// skyBlip stays on for the whole flight, so nothing but the pilot's switch may sleep the panel.
TEST_CASE("screen policy: a screen nobody changes is left awake, unrefreshed, rails down") {
    ScreenRig rig;
    rig.run_seconds(go::ScreenService::kRedriveAfterChangeMs / 1000 + 3);
    const int before = rig.chip.present_count;
    const int sleeps = rig.chip.deep_sleeps;

    rig.run_seconds(600);
    CHECK(rig.chip.present_count == before);
    CHECK(rig.chip.deep_sleeps == sleeps);
    CHECK(rig.chip.powered);
    CHECK_FALSE(rig.chip.rails_on);
}

// The ink migrates under bias, so a busy sky must not hold the rails up for a whole flight.
TEST_CASE("screen policy: a screen changing every second still rests between refreshes") {
    ScreenRig rig;
    rig.run_seconds(3);
    rig.churn(120);
    rig.run_seconds(2);

    CHECK(rig.chip.present_count > 100);
    CHECK_FALSE(rig.chip.rails_on);
    CHECK(rig.chip.deep_sleeps == 0);
}

TEST_CASE("screen policy: nothing refreshes a panel held hot, partial or full") {
    ScreenRig rig;
    rig.run_seconds(3);
    rig.churn(2);
    rig.run_seconds(1);
    const int before = rig.chip.present_count;

    rig.die_temperature(go::ScreenService::kRefreshCeilingDeciCelsius + 1);
    rig.run_seconds(600);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: no refresh starts on a dying rail, partial or full") {
    ScreenRig rig;
    rig.run_seconds(3);
    rig.churn(2);
    const int before = rig.chip.present_count;

    rig.state.power.supply_warned = true;
    rig.run_seconds(600);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: the hold threshold is the rated limit, not a margin somebody chose") {
    CHECK(go::ScreenService::kRefreshCeilingDeciCelsius == 500);
}

// The rated limit is a temperature the panel works at, not the first one it refuses.
TEST_CASE("screen policy: the panel still refreshes at exactly its rated limit") {
    ScreenRig rig;
    rig.run_seconds(3);

    rig.die_temperature(go::ScreenService::kRefreshCeilingDeciCelsius);
    const int before = rig.chip.present_count;
    rig.churn(3);
    CHECK(rig.chip.present_count > before);

    rig.die_temperature(go::ScreenService::kRefreshCeilingDeciCelsius + 1);
    const int held = rig.chip.present_count;
    rig.churn(3);
    CHECK(rig.chip.present_count == held);
}

// A refresh already in flight when the panel goes out of range is left to finish.
TEST_CASE("screen policy: going hot mid-refresh does not abandon the frame on the glass") {
    ScreenRig rig;
    rig.run_seconds(3);
    rig.state.flight.seconds += 60;
    rig.tick(rig.now_ms += 1000);
    const int in_flight = rig.chip.present_count;

    rig.die_temperature(go::ScreenService::kRefreshCeilingDeciCelsius + 1);
    rig.tick(rig.now_ms += 100);
    rig.tick(rig.now_ms += 2000);
    CHECK(rig.chip.present_count == in_flight);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE(
    "screen policy: a hold that trips behind a wipe still lands the page and drops the rails") {
    ScreenRig rig;
    rig.tick(rig.now_ms += 1000);
    REQUIRE(rig.glass_all_black());

    rig.die_temperature(go::ScreenService::kRefreshCeilingDeciCelsius + 1);
    rig.run_seconds(2);
    CHECK_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: a page change a held panel cannot finish never starts") {
    ScreenRig rig;
    rig.run_seconds(3);
    const int before = rig.chip.present_count;

    rig.die_temperature(go::ScreenService::kRefreshCeilingDeciCelsius + 1);
    rig.screen.next_page();
    rig.run_seconds(5);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.glass_all_black());

    // Cooled, the page change it was holding runs in full.
    rig.die_temperature(300);
    rig.tick(rig.now_ms += 1000);
    CHECK(rig.glass_all_black());
    rig.run_seconds(2);
    CHECK_FALSE(rig.glass_all_black());
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: the traffic picture comes back once the panel has cooled") {
    ScreenRig rig;
    rig.run_seconds(3);
    rig.die_temperature(go::ScreenService::kRefreshCeilingDeciCelsius + 1);
    rig.churn(5);
    const int held = rig.chip.present_count;

    rig.die_temperature(300);
    rig.churn(3);
    CHECK(rig.chip.present_count > held);
}

// The partial LUT may well ghost cold, but a full refresh a frame wears the glass out first (#62).
TEST_CASE("screen policy: below freezing every refresh is still a partial one") {
    ScreenRig rig;
    rig.run_seconds(3);

    rig.die_temperature(-50);
    for (int i = 0; i < 6; i++) {
        rig.churn(1);
        rig.run_seconds(2);
        CHECK_FALSE(rig.chip.last_full);
    }
}

TEST_CASE("screen policy: a cold panel with a static frame is still never re-presented") {
    ScreenRig rig;
    rig.run_seconds(go::ScreenService::kRedriveAfterChangeMs / 1000 + 3);
    const int before = rig.chip.present_count;

    rig.die_temperature(-100);
    rig.run_seconds(60);
    CHECK(rig.chip.present_count == before);
}

TEST_CASE("screen policy: a board whose die sensor never read refreshes as it always did") {
    ScreenRig rig;
    rig.run_seconds(3);
    REQUIRE_FALSE(rig.state.power.die_valid);

    rig.churn(3);
    CHECK_FALSE(rig.chip.last_full);
    CHECK(rig.chip.powered);
}

TEST_CASE("screen policy: nothing routine is refreshed once the cell is at its cutoff") {
    ScreenRig rig;
    rig.run_seconds(3);
    const int before = rig.chip.present_count;

    rig.state.power.level = power::PowerLevel::Flat;
    rig.churn(10);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

TEST_CASE("screen policy: a critical cell still gets its traffic picture") {
    ScreenRig rig;
    rig.run_seconds(3);
    const int before = rig.chip.present_count;

    rig.state.power.level = power::PowerLevel::Critical;
    rig.churn(3);
    CHECK(rig.chip.present_count > before);
}

TEST_CASE("screen policy: the white field the glass wears while off is drawn at the cutoff") {
    ScreenRig rig;
    rig.run_seconds(3);
    const int before = rig.chip.present_count;

    rig.state.power.level = power::PowerLevel::Flat;
    rig.screen.set_power(false);
    rig.run_seconds(6);
    CHECK(rig.chip.present_count == before + 1);
    CHECK(rig.chip.last_full);
    CHECK_FALSE(rig.chip.powered);
}

// A command sent over a live BUSY is lost, and the park frame runs for seconds after it is issued.
TEST_CASE("screen policy: the panel sleeps when the park frame has finished, not when it starts") {
    ScreenRig rig;
    rig.run_seconds(5);

    rig.screen.set_power(false);
    rig.tick(rig.now_ms += 100);
    CHECK(rig.chip.powered);
    CHECK(rig.epd.refreshing());

    rig.tick(rig.now_ms += 1000);
    CHECK(rig.chip.powered);

    rig.tick(rig.now_ms += 2000);
    CHECK_FALSE(rig.epd.refreshing());
    CHECK_FALSE(rig.chip.powered);
}

// The ink the sun develops: a park frame written over a waveform that was still driving the glass.
TEST_CASE("screen policy: a park mid-refresh waits for the glass, it does not talk over it") {
    ScreenRig rig;
    rig.churn(5);
    rig.state.flight.seconds += 60;
    rig.tick(rig.now_ms += 1000);
    const int before = rig.chip.present_count;
    REQUIRE(rig.epd.refreshing());

    rig.screen.set_power(false);
    rig.run_seconds(6);
    CHECK(rig.chip.commands_while_busy == 0);
    CHECK(rig.chip.present_count == before + 1);
    CHECK(rig.chip.last_full);
    CHECK_FALSE(rig.chip.powered);
}

// POFCON fires milliseconds before the brownout reset, so the park frame cannot finish.
TEST_CASE("screen policy: a supply warning parks the panel without a park frame") {
    ScreenRig rig;
    rig.run_seconds(5);
    const int before = rig.chip.present_count;

    rig.state.power.supply_warned = true;
    rig.screen.set_power(false);
    rig.tick(rig.now_ms += 100);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.powered);
}

TEST_CASE("screen policy: a supply warning stops the routine refreshes too") {
    ScreenRig rig;
    rig.run_seconds(3);
    const int before = rig.chip.present_count;

    rig.state.power.supply_warned = true;
    rig.churn(10);
    CHECK(rig.chip.present_count == before);
    CHECK_FALSE(rig.chip.rails_on);
}

// A held panel presents nothing, so nothing a pilot has not read reaches the glass.
TEST_CASE("screen policy: a held panel puts no new frame on the glass, dirty or not") {
    ScreenRig rig;
    rig.run_seconds(3);

    rig.die_temperature(go::ScreenService::kRefreshCeilingDeciCelsius + 1);
    rig.run_seconds(5);
    const int held = rig.chip.present_count;

    rig.screen.mark_dirty();
    rig.screen.next_page();
    rig.run_seconds(5);
    CHECK(rig.chip.present_count == held);
}
