// SSD1681 e-paper driver tests against models/ssd1681.h. Verifies the init
// sequence, the framebuffer to RAM polarity (canvas 1=black becomes panel 0=black), the
// glass turned or not, and the signature that names the panel, all on the host, no
// panel required.
#include <string>

#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "test/support/ssd1681_rig.h"

using namespace skyblip;

namespace {

parts::Ssd1681 turned_driver_for(models::Ssd1681& chip) {
    return parts::Ssd1681(chip, chip, chip, chip.dc_pin, chip.reset_pin, chip.busy_pin, -1,
                          parts::GlassRotation::Degrees270);
}

// One pixel of panel RAM, addressed as the controller does: a source on a gate line, black at 0.
bool ram_black(const models::Ssd1681& chip, int source, int gate) {
    const size_t byte = size_t(gate) * parts::Ssd1681Glass::kStride + size_t(source >> 3);
    return byte < chip.ram.size() && (chip.ram[byte] & (0x80 >> (source & 7))) == 0;
}

}  // namespace

TEST_CASE("ssd1681: begin() runs the SSD1681 init sequence and resets the panel") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = driver_for(chip);
    driver.begin();
    CHECK(chip.reset_pulses >= 1);
    CHECK(chip.saw_command(0x12));  // SW reset
    CHECK(chip.saw_command(0x01));  // driver output control
    CHECK(chip.saw_command(0x11));  // data entry mode
    CHECK(chip.saw_command(0x3C));  // border waveform
    CHECK(chip.saw_command(0x18));  // temperature sensor
}

// 10 ms as a literal: the test this replaced compared epd::kResetHoldSpins with itself.
TEST_CASE("ssd1681: RES# is held low for the vendor's 10 ms, at begin and at every wake") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = driver_for(chip);
    driver.begin();
    CHECK(chip.reset_low_us >= 10000);
    CHECK(chip.short_resets == 0);

    parts::Ssd1681Glass canvas;
    canvas.clear(true);
    driver.present(canvas, ports::Refresh::Full, 0);
    refresh_to_ready(driver, 0);
    driver.power_off();
    REQUIRE_FALSE(chip.powered);

    chip.reset_low_us = 0;
    driver.present(canvas, ports::Refresh::Partial, 5000);
    CHECK(chip.reset_low_us >= 10000);
    CHECK(chip.powered);
    CHECK(chip.short_resets == 0);
}

// A glitched RES# leaves a deep-sleeping SSD1681 asleep, BUSY high, first image forever.
TEST_CASE("ssd1681: a RES# pulse one microsecond under 10 ms leaves a sleeping panel asleep") {
    models::Ssd1681 chip;
    const uint8_t deep_sleep = 0x10;
    chip.set(chip.dc_pin, false);
    chip.transfer(&deep_sleep, nullptr, 1);
    REQUIRE_FALSE(chip.powered);

    chip.set(chip.reset_pin, false);
    chip.wait_at_least_us(9999);
    chip.set(chip.reset_pin, true);
    CHECK_FALSE(chip.powered);
    CHECK(chip.short_resets == 1);

    chip.set(chip.reset_pin, false);
    chip.wait_at_least_us(10000);
    chip.set(chip.reset_pin, true);
    CHECK(chip.powered);
    CHECK(chip.reset_pulses == 1);
}

TEST_CASE("ssd1681: present() writes a full framebuffer with correct black/white polarity") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = driver_for(chip);
    driver.begin();

    parts::Ssd1681Glass canvas;
    canvas.clear(/*white=*/true);
    driver.present(canvas, ports::Refresh::Full, 0);

    CHECK(chip.ram.size() == parts::Ssd1681Glass::kBytes);
    // An all-white framebuffer becomes all 0xFF in panel RAM (inverted).
    bool all_ff = true;
    for (uint8_t b : chip.ram)
        if (b != 0xFF) all_ff = false;
    CHECK(all_ff);
    CHECK(chip.saw_command(0x24));  // WriteRAM
    CHECK(chip.saw_command(0x20));  // Master activation
}

TEST_CASE("ssd1681: a black pixel flips the corresponding RAM bit to 0") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = driver_for(chip);
    driver.begin();

    parts::Ssd1681Glass canvas;
    canvas.clear(true);
    canvas.set_pixel(0, 0, /*black=*/true);
    driver.present(canvas, ports::Refresh::Full, 0);

    // First RAM byte now has at least one cleared bit (was 0xFF all-white).
    CHECK(chip.ram[0] != 0xFF);
}

TEST_CASE("ssd1681: an unturned glass takes framebuffer rows as gate lines") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = driver_for(chip);
    driver.begin();

    parts::Ssd1681Glass canvas;
    canvas.clear(true);
    canvas.set_pixel(0, 0, true);
    canvas.set_pixel(199, 0, true);
    canvas.set_pixel(3, 8, true);
    driver.present(canvas, ports::Refresh::Full, 0);

    REQUIRE(chip.ram.size() == parts::Ssd1681Glass::kBytes);
    CHECK(ram_black(chip, 0, 0));
    CHECK(ram_black(chip, 199, 0));
    CHECK(ram_black(chip, 3, 8));
    CHECK_FALSE(ram_black(chip, 0, 199));
}

// The Plus mounts the glass a quarter turn off the scan: unturned here, the device reads sideways.
TEST_CASE("ssd1681: a glass turned 270 degrees takes framebuffer columns as gate lines") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = turned_driver_for(chip);
    driver.begin();

    parts::Ssd1681Glass canvas;
    canvas.clear(true);
    canvas.set_pixel(0, 0, true);
    canvas.set_pixel(199, 0, true);
    canvas.set_pixel(3, 8, true);
    driver.present(canvas, ports::Refresh::Full, 0);

    REQUIRE(chip.ram.size() == parts::Ssd1681Glass::kBytes);
    // (x, y) lands on source y of gate line 199 - x.
    CHECK(ram_black(chip, 0, 199));
    CHECK(ram_black(chip, 0, 0));
    CHECK(ram_black(chip, 8, 196));
    CHECK_FALSE(ram_black(chip, 199, 0));
}

TEST_CASE("ssd1681: a turned glass writes the same count of black pixels it was handed") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = turned_driver_for(chip);
    driver.begin();

    parts::Ssd1681Glass canvas;
    canvas.clear(true);
    for (int i = 0; i < 200; i++) canvas.set_pixel(i, i / 2, true);
    driver.present(canvas, ports::Refresh::Full, 0);

    int black = 0;
    for (int gate = 0; gate < parts::Ssd1681::kGlassHeight; gate++)
        for (int source = 0; source < parts::Ssd1681::kGlassWidth; source++)
            if (ram_black(chip, source, gate)) black++;
    CHECK(black == canvas.count_black());
}

TEST_CASE("ssd1681: the five signatures shipped in T-Echos are told apart") {
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

TEST_CASE("ssd1681: 2118 and 2129 are the same in 0x2D and only 0x2E separates them") {
    // The reason the ident reads both registers. A table keyed on 0x2D alone
    // would answer this question with whichever row it happened to try first.
    for (int i = 0; i < parts::kPanelIdBytes2d; i++)
        CHECK(parts::panels::kGdeh0154D67Syx2118.register_2d[i] ==
              parts::panels::kGdeh0154D67Syx2129.register_2d[i]);

    bool register_2e_differs = false;
    for (int i = 0; i < parts::kPanelIdBytes2e; i++)
        if (parts::panels::kGdeh0154D67Syx2118.register_2e[i] !=
            parts::panels::kGdeh0154D67Syx2129.register_2e[i])
            register_2e_differs = true;
    CHECK(register_2e_differs);
}

TEST_CASE("ssd1681: a signature nobody has recorded is unlisted, and an unread one is neither") {
    // The seventh row of SoftRF's table is the Plus - our own board - and it
    // carries no bytes at all, only the string "20.05.21". So this is the answer
    // this board is expected to give until somebody reads one on a bench.
    parts::PanelSignature unrecorded{};
    unrecorded.valid = true;
    unrecorded.register_2d[0] = 0x12;
    unrecorded.register_2e[3] = 0x34;
    CHECK(parts::identify_panel(unrecorded) == parts::Panel::Unlisted);

    // A fingerprint that was never taken is a different miss: nobody read it.
    parts::PanelSignature never_taken{};
    CHECK(parts::identify_panel(never_taken) == parts::Panel::Unknown);
    // Even when the bytes would otherwise match a shipped panel: an unread
    // register file is all zeroes, and all zeroes is DEPG0150BN.
    parts::PanelSignature zeroes = parts::panels::kDepg0150Bn;
    zeroes.valid = false;
    CHECK(parts::identify_panel(zeroes) == parts::Panel::Unknown);
}

TEST_CASE("ssd1681: the identity is a name the self-test page can print") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = driver_for(chip);
    driver.begin();
    CHECK(std::string(driver.panel_name()) == "NO ID");

    driver.adopt(parts::panels::kDepg0150Bn);
    CHECK(std::string(driver.panel_name()) == "DEPG0150");

    // Short enough to share a row with a name and a verdict inside 200 pixels.
    for (int i = 0; i <= int(parts::Panel::ElecrowM1); i++) {
        const char* name = parts::panel_name(static_cast<parts::Panel>(i));
        CHECK(std::string(name).size() <= 8);
    }
}

// A canvas of another size would be written as if it were this glass: same bank,
// same window, rows sheared by the stride it does not have.
TEST_CASE("ssd1681: a canvas that is not this glass never reaches the panel") {
    models::Ssd1681 chip;
    parts::Ssd1681 driver = driver_for(chip);
    driver.begin();

    ui::Panel<128, 64> other;
    other.clear(/*white=*/false);
    const int presents = chip.present_count;
    driver.present(other, ports::Refresh::Full, 0);

    CHECK_FALSE(parts::Ssd1681::drives(other));
    CHECK(chip.present_count == presents);
}
