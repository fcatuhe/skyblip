// What reaches the glass, driven through the whole product rather than through a
// screen in isolation: the pages the pad walks, the unit a pilot reads an
// instrument in, and the image an e-paper wears once its rails are down. A page
// that is right in a widget test and never presented is a page nobody sees.
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/boot.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"
#include "ui/widgets/wordmark.h"

using namespace skyblip;

TEST_CASE("product: a pad tap switches page, and no swap costs the full waveform") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.product.screen().page() == go::Page::Radar);

    uint32_t t = 100;
    rig.tap(t);
    CHECK(rig.product.screen().page() == go::Page::Nearby);
    CHECK_FALSE(rig.platform.chips().epd.last_full);  // power on to power off, partials alone
    rig.run(t, t + 4000);
    t += 4000;

    rig.tap(t);
    CHECK(rig.product.screen().page() == go::Page::SixPack);

    rig.tap(t);
    CHECK(rig.product.screen().page() == go::Page::GMeter);

    // Four pictures on the walk, and it wraps. The rest are opened by name.
    rig.tap(t);
    CHECK(rig.product.screen().page() == go::Page::Radar);
    CHECK(rig.product.screen().mode() == go::Mode::Page);
}

// Every page is drawn and compared, and only a frame that differs reaches the glass.
TEST_CASE("product: a still world leaves every picture on the walk alone") {
    for (int p = 0; p < go::kWalkedPages; p++) {
        const go::Page page = static_cast<go::Page>(p);
        CAPTURE(p);
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        rig.platform.chips().imu.set_acceleration(1000, 0, 0);
        uint32_t t = 0;
        rig.seconds(t, 5, 0, 300);
        rig.show(t, page);
        rig.seconds(t, 5, 0, 300);
        REQUIRE(rig.product.screen().page() == page);

        const uint32_t presented = rig.state().duty.panel_partial_refreshes;
        rig.seconds(t, 60, 0, 300);
        CHECK(rig.state().duty.panel_partial_refreshes == presented);
    }
}

// A ball swinging every 200 ms would be five refreshes a second if the glass followed it.
TEST_CASE("product: a ball that never stops is presented once a second, never faster") {
    constexpr uint32_t kStepMs = 50;
    constexpr uint32_t kSwingMs = 200;
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.chips().imu.set_acceleration(1000, 0, 0);
    uint32_t t = 0;
    rig.run(t, t + 4000);
    t += 4000;
    rig.show(t, go::Page::SixPack);
    rig.run(t, t + 3000);
    t += 3000;

    uint32_t presents = 0;
    uint32_t last_present_ms = 0;
    uint32_t seen = rig.state().duty.panel_partial_refreshes;
    for (const uint32_t end = t + 10000; t <= end; t += kStepMs) {
        const bool left = (t / kSwingMs) % 2 == 0;
        rig.platform.chips().imu.set_acceleration(1000, left ? -150 : 150, 0);
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        if (rig.state().duty.panel_partial_refreshes == seen) continue;
        seen = rig.state().duty.panel_partial_refreshes;
        if (presents > 0) CHECK(t - last_present_ms >= go::ScreenService::kPresentFloorMs);
        last_present_ms = t;
        presents++;
    }
    CHECK(presents >= 5);
    CHECK(presents <= 11);
}

// A page opened from a menu is a detour, not a fifth stop on the walk.
TEST_CASE("product: the pad leaves a page it was sent to for the page that sent it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;

    rig.show(t, go::Page::RadioLog);
    REQUIRE(rig.product.screen().page() == go::Page::RadioLog);

    rig.tap(t);
    CHECK(rig.product.screen().page() == go::Page::Nearby);
    rig.tap(t);
    CHECK(rig.product.screen().page() == go::Page::SixPack);
}

// Counting taps back to the traffic picture is what nobody does with traffic converging.
TEST_CASE("product: a long touch comes back to the radar from wherever the pilot is") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    rig.show(t, go::Page::Status);
    REQUIRE(rig.product.screen().page() == go::Page::Status);

    rig.long_touch(t);
    CHECK(rig.product.screen().page() == go::Page::Radar);
    CHECK(rig.product.screen().mode() == go::Mode::Page);

    // And out of the menu, which the pad did not take the pilot into.
    rig.press(t);
    REQUIRE(rig.product.screen().mode() == go::Mode::Menu);
    rig.long_touch(t);
    CHECK(rig.product.screen().mode() == go::Mode::Page);
    CHECK(rig.product.screen().page() == go::Page::Radar);
    CHECK_FALSE(rig.product.screen().editor().active());
}

TEST_CASE("product: powering the panel down leaves the wordmark on it") {
    // An e-paper holds its last image with the rails down, so what is written
    // immediately before power_off is what the device wears while it is off.
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 1000);

    go::Glass expected;
    expected.clear(true);
    ui::draw_wordmark(expected, go::kGlassW / 2, go::kGlassH / 2);

    rig.product.screen().set_power(false);
    rig.run(1000, 7000);
    CHECK_FALSE(rig.platform.chips().epd.powered);
    CHECK(rig.platform.chips().epd.last_full);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());
    CHECK(expected.count_black() > 200);

    // ... and it stays there: a service that keeps rendering must not reach a
    // panel whose rails are down, or the mark would be wiped a second later.
    rig.run(1000, 4000);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());

    // The mark is above the word: the dot and its arcs put ink in the top half
    // of the glyph block, which the letters alone would leave blank.
    int arc_ink = 0;
    for (int y = 66; y < 78; y++)
        for (int x = 100; x < 160; x++) arc_ink += expected.get_pixel(x, y) ? 1 : 0;
    CHECK(arc_ink > 20);
}

// An e-paper stored for months under one image keeps a shadow of it for good.
TEST_CASE("product: the pad held through the press leaves the glass blank") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.run(0, 1000);
    t = 1000;

    rig.platform.board_gpio().pad_down = true;
    rig.platform.board_gpio().button_down = true;
    rig.run(t, t + power::kLongPressMs + 200);
    t += power::kLongPressMs + 200;

    rig.run(t, t + power::kParkMs);
    CHECK(rig.product.shutdown().reason() == power::ShutdownReason::Stow);
    CHECK_FALSE(rig.platform.chips().epd.powered);
    CHECK(rig.platform.chips().epd.last_full);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == 0);
}

// P1.11 is read as the panel's enable as often as its front light, so it goes out last.
TEST_CASE("product: the front light goes out after the park frame, not before it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 1000);
    rig.product.screen().set_backlight(true);

    rig.product.screen().set_power(false);
    rig.run(1000, 2000);
    CHECK(rig.platform.chips().epd.backlight);
    CHECK(rig.platform.chips().epd.powered);

    rig.run(2000, 8000);
    CHECK_FALSE(rig.platform.chips().epd.powered);
    CHECK_FALSE(rig.platform.chips().epd.backlight);
}

TEST_CASE("product: the backlight starts off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 1000);
    CHECK_FALSE(rig.product.screen().backlight());
    CHECK_FALSE(rig.platform.chips().epd.backlight);
}

// B4. The setting is handed to every page that prints a distance or a speed.
TEST_CASE("product: the unit setting reaches the pages that print one") {
    auto page_ink = [](go::Units units, go::Page page) {
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        rig.settings().units = units;
        uint32_t t = 100;
        rig.show(t, page);
        rig.run(t, t + 3000);
        REQUIRE(rig.product.screen().page() == page);
        return rig.product.screen().framebuffer().count_black();
    };

    CHECK(page_ink(go::Units::Metric, go::Page::SixPack) !=
          page_ink(go::Units::Nautical, go::Page::SixPack));
    CHECK(page_ink(go::Units::Metric, go::Page::Radar) !=
          page_ink(go::Units::Nautical, go::Page::Radar));
}

// The pilot the word is for is looking out of the window, not at the footer.
TEST_CASE("product: an aircraft rolling on the ground says TAXI in the ring, parked says GROUND") {
    auto radar_says = [](const char* word, int32_t speed_mm_s) {
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        uint32_t t = 100;
        rig.seconds(t, 20, speed_mm_s, 300);
        REQUIRE(rig.product.screen().page() == go::Page::Radar);
        return reads_in(rig.product.screen().framebuffer(), word, 40, 120, 160, 160, 2);
    };

    CHECK(radar_says("GROUND", 0));
    CHECK(radar_says("TAXI", 3000));  // a tug on the perimeter track
    CHECK_FALSE(radar_says("GROUND", 3000));

    // 1.5 m/s is where the word picks up: a parked receiver's own noise is not a taxi.
    CHECK(radar_says("TAXI", 1500));
    CHECK(radar_says("GROUND", 1250));
}

// The word after a landing: a pilot rolling in has landed, and used to read FLIGHT until parked.
TEST_CASE("product: a flight that rolls in without stopping reads TAXI on the taxiway") {
    auto reads = [](Rig& rig, const char* word) {
        return reads_in(rig.product.screen().framebuffer(), word, 40, 120, 160, 160, 2);
    };

    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    rig.seconds(t, 20, 0, 300);
    rig.seconds(t, 60, 30000, 800);
    REQUIRE_FALSE(reads(rig, "TAXI"));

    rig.seconds(t, 10, 4000, 300);  // off the runway at 4 m/s, and the hold is not out yet
    CHECK_FALSE(reads(rig, "TAXI"));

    rig.seconds(t, 5, 4000, 300);
    CHECK(reads(rig, "TAXI"));
    CHECK_FALSE(reads(rig, "GROUND"));
}

// A metre a second of multipath on a parked receiver used to cost a word and a refresh a second.
TEST_CASE("product: the word holds through the band between a standstill and a taxi") {
    auto reads = [](Rig& rig, const char* word) {
        return reads_in(rig.product.screen().framebuffer(), word, 40, 120, 160, 160, 2);
    };

    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    rig.seconds(t, 20, 0, 300);
    REQUIRE(reads(rig, "GROUND"));

    rig.seconds(t, 5, 1250, 300);  // 1.25 m/s of noise on a device that has not moved
    CHECK(reads(rig, "GROUND"));

    rig.seconds(t, 5, 2000, 300);  // 2 m/s, a glider pushed to the grid
    REQUIRE(reads(rig, "TAXI"));

    rig.seconds(t, 5, 1250, 300);  // the same 1.25 m/s, now a taxi slowing for the turn
    CHECK(reads(rig, "TAXI"));

    rig.seconds(t, 5, 750, 300);  // 0.75 m/s: stopped, and the word goes back
    CHECK(reads(rig, "GROUND"));
}

// An antenna under a wing in the circuit is a glitch, not a landing.
TEST_CASE("product: a fix lost in the air says NO FIX over a clock that keeps running") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.seconds(t, 130, 25000, 300);  // 25 m/s: airborne, and two minutes of it
    REQUIRE(rig.state().flight.running);
    const uint32_t flown = rig.state().flight.seconds;
    REQUIRE(flown >= 2 * 60);

    rig.blind_seconds(t, 90);
    CHECK_FALSE(rig.state().own.fix_valid);
    CHECK(rig.state().flight.running);
    CHECK(rig.state().flight.seconds >= flown + 85);

    const ui::Canvas& glass = rig.product.screen().framebuffer();
    CHECK(reads_in(glass, "NO FIX", 40, 120, 160, 160, 2));
    CHECK(reads_in(glass, "0:03", 0, 176, 60, 198, 2));
}

// F5. The device said nothing when the receiver finally solved, and it
// transmitted the first solution it got. Both are wrong on the bench and in the
// air: a cold receiver's first fixes walk, and the pilot is left guessing.

// B4 + the low-battery warning: two things the status page is the only reader
// of. Both are drawn from the state the services publish, so this is the wiring
// test - ui/screens/status.cpp owns what they look like.
TEST_CASE("product: the status page carries the device's name and a cell that is low") {
    auto status_ink = [](const char* callsign, uint16_t millivolts, bool on_cable = false) {
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        rig.platform.battery().millivolts = millivolts;
        rig.platform.battery().external_power = on_cable;
        for (int i = 0; callsign[i] != 0 && i < 9; i++) rig.settings().callsign[i] = callsign[i];
        uint32_t t = 100;
        rig.show(t, go::Page::Status);
        // Long enough for the cutoff monitor to have made its mind up: it wants
        // three consecutive samples before it calls a cell low, and the page
        // draws what it decided rather than deciding again.
        rig.run(t, t + 8000);
        REQUIRE(rig.product.screen().page() == go::Page::Status);
        return rig.product.screen().framebuffer().count_black();
    };

    const int plain = status_ink("", 4050);
    CHECK(status_ink("D-KXYZ", 4050) > plain);
    // 3.45 V is under core/power's warning threshold: the row says LOW rather
    // than leaving a pilot to read the number and know what it means. The same
    // cell on the cable is charging, and nothing on a charger is low.
    const int low = status_ink("", 3450);
    CHECK(low != plain);
    CHECK(status_ink("", 3450, /*on_cable=*/true) != low);
}

// D3, the reporting half. The marker on the page is the cutoff monitor's own
// verdict, published on the bus: the page does not compare millivolts a second
// time, so it cannot disagree with the thing that can switch the device off.

// D3, the reporting half. The marker on the page is the cutoff monitor's own
// verdict, published on the bus: the page does not compare millivolts a second
// time, so it cannot disagree with the thing that can switch the device off.
TEST_CASE("product: the status page marks a low cell when the monitor says so, not before") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().millivolts = 3450;
    uint32_t t = 100;
    rig.show(t, go::Page::Status);
    REQUIRE(rig.product.screen().page() == go::Page::Status);

    // Two samples under the warning is not yet a low cell: a transmit burst
    // sags the rail for as long as it lasts, and the third sample is what
    // decides. The page says nothing while the monitor has not.
    rig.run(t, 2500);
    REQUIRE(rig.state().power.level != power::PowerLevel::Critical);
    const int undecided = rig.product.screen().framebuffer().count_black();

    rig.run(2500, 6000);
    REQUIRE(rig.state().power.level == power::PowerLevel::Critical);
    // The same voltage and the same state of charge, so the only thing that can
    // have changed on the glass is the marker.
    CHECK(rig.product.screen().framebuffer().count_black() > undecided);
}

// The charger cannot be gated here, so the row is the only warning there is.
TEST_CASE("product: the status page marks a cell charging too hot to be charged") {
    constexpr ports::Capabilities kWithDie = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) |
        static_cast<uint32_t>(ports::Capability::DieTemperature));

    auto charging_at = [](int16_t decicelsius) {
        Rig rig{kWithDie};
        REQUIRE(rig.setup() == Status::Ok);
        rig.platform.die_temperature().hold(decicelsius);
        rig.platform.battery().millivolts = 4000;
        rig.platform.battery().external_power = true;
        uint32_t t = 100;
        rig.show(t, go::Page::Status);
        rig.run(t, t + 8000);
        REQUIRE(rig.product.screen().page() == go::Page::Status);
        REQUIRE(rig.state().power.battery.charging);
        return rig.product.screen().framebuffer().count_black();
    };

    const int warm = charging_at(250);
    const int hot = charging_at(power::kChargeHotDeciCelsius + 100);
    const int cold = charging_at(power::kChargeColdDeciCelsius - 100);
    CHECK(hot != warm);
    CHECK(cold != warm);
    CHECK(hot != cold);
}
