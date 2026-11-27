// What the SSD1681 driver does when the SPI bus reports a transfer that did not happen.
#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "test/support/ssd1681_rig.h"

using namespace skyblip;

TEST_CASE(
    "ssd1681: a frame that did not reach the panel is not refreshed, and the next is a full") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = driver_for(chip);
    driver.begin();
    parts::Ssd1681Glass canvas;
    canvas.clear(true);
    driver.present(canvas, ports::Refresh::Full, 0);
    refresh_to_ready(driver, 0);
    const int resets = chip.reset_pulses;

    chip.transfers_before_failure = 0;
    SUBCASE("a page") { driver.present(canvas, ports::Refresh::Partial, 5000); }
    SUBCASE("a wipe") { driver.paint_black(5000); }
    CHECK_FALSE(driver.refreshing());
    CHECK(driver.ready(5000));

    chip.transfers_before_failure = -1;
    driver.present(canvas, ports::Refresh::Partial, 6000);
    CHECK(chip.reset_pulses == resets + 1);
    CHECK(chip.last_full);
    CHECK(driver.refreshing());
}

TEST_CASE("ssd1681: a bring-up that did not reach the panel is done again before the next frame") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = driver_for(chip);
    chip.transfers_before_failure = 0;
    driver.begin();
    const int resets = chip.reset_pulses;

    chip.transfers_before_failure = -1;
    parts::Ssd1681Glass canvas;
    canvas.clear(true);
    driver.present(canvas, ports::Refresh::Partial, 0);
    CHECK(chip.reset_pulses == resets + 1);
    CHECK(chip.saw_command(0x12));
    CHECK(chip.last_full);
}

TEST_CASE(
    "ssd1681: nothing follows a failed transfer, not even an activation the bus would carry") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = driver_for(chip);
    driver.begin();
    parts::Ssd1681Glass canvas;
    canvas.clear(true);
    driver.present(canvas, ports::Refresh::Full, 0);
    refresh_to_ready(driver, 0);
    const int before = chip.transfers;
    driver.present(canvas, ports::Refresh::Partial, 5000);
    REQUIRE(driver.ready(5000 + parts::Ssd1681::kReadyAfterFullMs));
    const int partial = chip.transfers - before;
    const int presents = chip.present_count;

    // A partial ends on the update command, its sequence and the activation: only the first fails.
    chip.transfers_before_failure = partial - 3;
    chip.failures_before_recovery = 1;
    driver.present(canvas, ports::Refresh::Partial, 10000);
    CHECK(chip.present_count == presents);
    CHECK_FALSE(driver.refreshing());
}
