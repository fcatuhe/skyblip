// SSD1681 e-paper driver tests against models/ssd1681.h. Verifies the init
// sequence, the framebuffer to RAM polarity (fb 1=black becomes panel 0=black), the
// glass turned or not, and the signature that names the panel, all on the host, no
// panel required.
#include <string>

#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "test/support/ssd1681_rig.h"

using namespace skyblip;

namespace {

parts::Ssd1681 make_turned(models::Ssd1681& f) {
    return parts::Ssd1681(f, f, f, f.dc, f.rst, f.busy, -1, parts::GlassRotation::Deg270);
}

// One pixel of panel RAM, addressed as the controller does: a source on a gate line, black at 0.
bool ram_black(const models::Ssd1681& f, int source, int gate) {
    const size_t byte = size_t(gate) * parts::Ssd1681Glass::kStride + size_t(source >> 3);
    return byte < f.ram.size() && (f.ram[byte] & (0x80 >> (source & 7))) == 0;
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
