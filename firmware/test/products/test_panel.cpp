// What reaches the glass, driven through the whole product rather than through a
// screen in isolation: the pages the pad walks, the self test that names the
// part which did not answer, the unit a pilot reads an instrument in, and the
// image an e-paper wears once its rails are down. A page that is right in a
// widget test and never presented is a page nobody sees.
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/boot.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"
#include "ui/widgets/wordmark.h"

using namespace skyblip;

namespace {

bool reads_from(const go::Glass& fb, int x, int y, const char* text) {
    go::Glass expected;
    expected.clear(true);
    expected.draw_text(x, y, text, true, 1);
    for (int dy = 0; dy < 7; dy++)
        for (int dx = 0; dx < int(std::string(text).size()) * go::kBootCellW; dx++)
            if (fb.get_pixel(x + dx, y + dy) != expected.get_pixel(x + dx, y + dy)) return false;
    return true;
}

}  // namespace

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

// One decision, in core/power: a cable is not a low cell, so the ring has nothing to say.
TEST_CASE("product: a cable takes the cell's word off the radar, and no charge climbs there") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.run(t, t + 2000);
    t += 2000;

    rig.platform.battery().millivolts = power::kLowWarnMv - 100;
    rig.run(t, t + 8000);
    t += 8000;
    REQUIRE(rig.state().power.level == power::PowerLevel::Low);
    CHECK(reads_in(rig.platform.chips().epd.framebuffer(), "BAT", 40, 120, 160, 160, 2));

    rig.platform.battery().external_power = true;
    rig.run(t, t + 8000);
    CHECK(rig.state().power.level == power::PowerLevel::Normal);
    CHECK_FALSE(reads_in(rig.platform.chips().epd.framebuffer(), "BAT", 0, 0, 200, 199, 2));
    CHECK_FALSE(reads_in(rig.platform.chips().epd.framebuffer(), "%", 0, 0, 200, 199, 2));
}

// Nobody pressed anything, so the frame is the only thing that can say why the device stopped.
TEST_CASE("product: a cell that takes the device down names itself on the way out") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 2000);

    rig.platform.battery().millivolts = 3100;
    rig.run(2000, 20000);
    REQUIRE(rig.product.shutdown().reason() == power::ShutdownReason::LowBattery);
    REQUIRE(rig.product.ready_to_power_off());

    const go::Glass& parked = rig.platform.chips().epd.framebuffer();
    CHECK(reads_in(parked, "FLAT BATTERY", 10, 130, 190, 199, 2));
    CHECK_FALSE(reads_in(parked, "%", 0, 0, 200, 199, 2));
    CHECK(rig.platform.system_power().flat_on_glass());

    // The press after it gets nothing: the frame is the answer and the wake pin is never armed.
    rig.platform.system_power().system_off(
        power::button_wake_after(rig.product.shutdown().reason(), rig.platform.external_power()));
    CHECK(rig.platform.system_power().order_of(power::PowerDownStep::WakePinArmed) == -1);
}

// The other road to the same empty cell: a winter on a shelf, which runs no shutdown at all.
TEST_CASE("product: a boot refused on a flat cell writes the reason under the mark") {
    Rig rig;
    rig.platform.battery().millivolts = power::kBootLockoutMv - 1;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.boot_path() == power::BootPath::SleepAgain);
    rig.sleep_again();

    const go::Glass& parked = rig.platform.chips().epd.framebuffer();
    CHECK(rig.platform.chips().epd.present_count == 1);
    CHECK(rig.platform.chips().epd.last_full);
    CHECK_FALSE(rig.platform.chips().epd.powered);
    CHECK(reads_in(parked, "FLAT BATTERY", 10, 130, 190, 199, 2));
    CHECK(rig.platform.system_power().flat_on_glass());

    // Never a percentage: it would be the reading the device died at, standing unchanged
    // through the whole charge that follows.
    CHECK_FALSE(reads_in(parked, "%", 0, 0, 200, 199, 2));
}

// A frame is seconds of panel rail, and the cell paying for it is the flat one.
TEST_CASE("product: a refused boot pushes no frame the glass is already wearing") {
    Rig rig;
    rig.platform.battery().millivolts = power::kBootLockoutMv - 1;
    rig.platform.system_power().flat_glass = true;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.refused_frame() == power::RefusedFrame::Leave);
    rig.sleep_again();
    CHECK(rig.platform.chips().epd.present_count == 0);
}

// The cable re-arms the button, so the mark is the whole instruction again.
TEST_CASE("product: the charger that wakes a flat device takes the word back off the glass") {
    Rig rig;
    rig.platform.battery().millivolts = power::kBootLockoutMv - 1;
    rig.platform.battery().external_power = true;
    rig.platform.system_power().flat_glass = true;
    rig.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    REQUIRE(rig.setup() == Status::Ok);
    REQUIRE(rig.product.boot_path() == power::BootPath::SleepAgain);
    rig.sleep_again();

    go::Glass expected;
    expected.clear(true);
    ui::draw_wordmark(expected, go::kGlassW / 2, go::kGlassH / 2);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() == expected.count_black());
    CHECK_FALSE(rig.platform.system_power().flat_on_glass());

    // And the cable wiggle after it costs nothing: the glass already says the right thing.
    Rig again;
    again.platform.battery().millivolts = power::kBootLockoutMv - 1;
    again.platform.battery().external_power = true;
    again.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    REQUIRE(again.setup() == Status::Ok);
    again.sleep_again();
    CHECK(again.platform.chips().epd.present_count == 0);
}

// The whole road, on the one bit that survives the rails: the cutoff writes it, the cable reads it.
TEST_CASE("product: the cable after a cutoff puts the mark back and arms the button") {
    Rig dying;
    REQUIRE(dying.setup() == Status::Ok);
    dying.run(0, 2000);
    dying.platform.battery().millivolts = 3100;
    dying.run(2000, 20000);
    REQUIRE(dying.product.ready_to_power_off());

    Rig plugged;
    plugged.platform.system_power().flat_glass = dying.platform.system_power().flat_on_glass();
    plugged.platform.battery().millivolts = power::kBootLockoutMv - 1;
    plugged.platform.battery().external_power = true;
    plugged.platform.system_power().causes =
        power::ResetCause::LowPowerWake | power::ResetCause::UsbVbus;
    REQUIRE(plugged.setup() == Status::Ok);
    REQUIRE(plugged.product.refused_frame() == power::RefusedFrame::Wordmark);
    plugged.sleep_again();

    const go::Glass& parked = plugged.platform.chips().epd.framebuffer();
    CHECK_FALSE(reads_in(parked, "FLAT BATTERY", 0, 0, 200, 199, 2));
    CHECK_FALSE(plugged.platform.system_power().flat_on_glass());
    CHECK(power::button_wake_after_refusal(plugged.product.boot_cell()) ==
          power::ButtonWake::Armed);
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

// D2: the panel is the self test. A device that returns from main and goes dark
// tells a pilot on a bench nothing at all; a device holding a page that names
// the part that did not answer tells them everything.

// D2: the panel is the self test. A device that returns from main and goes dark
// tells a pilot on a bench nothing at all; a device holding a page that names
// the part that did not answer tells them everything.

// A device that can fly spends no full refresh on a page nobody asked for.
TEST_CASE("product: a device that can fly keeps the self test off the glass at boot") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(rig.platform.chips().epd.present_count == 0);
    CHECK(rig.product.boot_page().count_black() > 200);

    rig.run(0, 2000);
    CHECK(rig.product.screen().page() == go::Page::Radar);
    CHECK(rig.platform.chips().epd.present_count == 2);  // the black, then the page
}

TEST_CASE("product: the self-test page reaches the panel before anything refuses") {
    constexpr ports::Capabilities kNoGnss = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Gnss));
    Rig rig{kNoGnss};
    CHECK(rig.setup() == Status::Down);
    CHECK_FALSE(rig.product.flyable());

    // Painted, full, and it is the self-test page rather than a blank glass.
    CHECK(rig.platform.chips().epd.present_count == 1);
    CHECK(rig.platform.chips().epd.last_full);
    CHECK(rig.platform.chips().epd.framebuffer().count_black() ==
          rig.product.boot_page().count_black());
    CHECK(rig.product.boot_page().count_black() > 200);

    // And it stays. The loop refuses to fly, so nothing overwrites the one page
    // that says why.
    rig.run(0, 20000);
    CHECK(rig.platform.chips().epd.present_count == 1);
    CHECK_FALSE(rig.state().started);
}

TEST_CASE("product: the self-test page names the part, not just the failure") {
    constexpr ports::Capabilities kNoGnss = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Gnss));
    Rig missing{kNoGnss};
    REQUIRE(missing.setup() == Status::Down);

    Rig whole;
    REQUIRE(whole.setup() == Status::Ok);

    // Row 1 is GNSS (products/skyblip_go/product.h::kBootParts). The two pages
    // differ there and nowhere else in that row's band.
    const go::Glass& bad = missing.product.boot_page();
    const go::Glass& good = whole.product.boot_page();
    int row_difference = 0;
    for (int y = go::boot_row_y(1); y < go::boot_row_y(1) + 8; y++)
        for (int x = 0; x < go::kGlassW; x++)
            row_difference += bad.get_pixel(x, y) != good.get_pixel(x, y) ? 1 : 0;
    CHECK(row_difference > 0);

    // The radio row is identical on both: only the part that failed changed.
    int radio_difference = 0;
    for (int y = go::boot_row_y(0); y < go::boot_row_y(0) + 8; y++)
        for (int x = 0; x < go::kGlassW; x++)
            radio_difference += bad.get_pixel(x, y) != good.get_pixel(x, y) ? 1 : 0;
    CHECK(radio_difference == 0);
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
    REQUIRE(rig.state().power.level != power::PowerLevel::Low);
    const int undecided = rig.product.screen().framebuffer().count_black();

    rig.run(2500, 6000);
    REQUIRE(rig.state().power.level == power::PowerLevel::Low);
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

// K: the page has to name which part answered, not only that one did. LilyGO
// ships two barometer addresses, five e-paper lots and two kinds of haptic
// against the same footprints, so "BARO PASS" on its own does not identify the
// device a bench is holding.
TEST_CASE("product: the self-test page carries what the probes found, not what was expected") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);

    const go::BootPart* baro = nullptr;
    const go::BootPart* haptic = nullptr;
    const go::BootPart* radio = nullptr;
    for (int i = 0; i < go::kBootPartCount; i++) {
        const go::BootPart& row = rig.product.boot_rows()[i];
        if (go::kBootParts[i].capability == ports::Capability::Baro) baro = &row;
        if (go::kBootParts[i].capability == ports::Capability::Haptic) haptic = &row;
        if (go::kBootParts[i].capability == ports::Capability::Rf) radio = &row;
    }
    REQUIRE(baro != nullptr);
    REQUIRE(haptic != nullptr);
    REQUIRE(radio != nullptr);

    // The host bus answers the BOM's address, and the page prints the address
    // that answered rather than the one in the devicetree.
    REQUIRE(baro->detail != nullptr);
    CHECK(std::string(baro->detail) == "BME280 76");
    // The haptic on this platform is the waveform driver, so the row says which.
    REQUIRE(haptic->detail != nullptr);
    CHECK(std::string(haptic->detail) == "DRV2605");
    // A footprint with one part behind it names that part and nothing else.
    REQUIRE(radio->detail != nullptr);
    CHECK(std::string(radio->detail) == "SX1262");
}

// A row that only says PASS cannot tell two units apart, which is what the page is for.
TEST_CASE("product: every row on the self-test page names its part, inside the width of a row") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);

    constexpr size_t kRowCells = go::kBootRowCells;
    for (int i = 0; i < go::kBootPartCount; i++) {
        const go::BootPart& row = rig.product.boot_rows()[i];
        REQUIRE(row.detail != nullptr);
        CHECK(std::string(row.detail).size() > 0);
        // name, space, part, space, then the longest verdict there is.
        const size_t cells = std::string(row.name).size() + std::string(row.detail).size() +
                             std::string("NOT FITTED").size() + 2;
        CHECK(cells <= kRowCells);
    }
}

// The IMU and the RTC have no row because nothing drives them: the bus is their evidence.
TEST_CASE("product: the parts this firmware never drives are named on the bus row") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);

    CHECK(reads_from(rig.product.boot_page(), go::kBootLeftX, go::boot_row_y(go::kBootPartCount),
                     "I2C IMU RTC HAPTIC BARO"));
}

// The page is the inventory: a capability with no row is a part nobody can miss.
TEST_CASE("product: every capability the product flies on or without has a row") {
    const uint32_t declared =
        static_cast<uint32_t>(go::kRequired) | static_cast<uint32_t>(go::kOptional);
    for (int bit = 0; bit < 32; bit++) {
        const uint32_t one = 1u << bit;
        if ((declared & one) == 0) continue;
        bool on_the_page = false;
        for (int i = 0; i < go::kBootPartCount; i++)
            if (static_cast<uint32_t>(go::kBootParts[i].capability) == one) on_the_page = true;
        CHECK(on_the_page);
    }
}

// The row already says NOT FITTED, and "00" would read as a part at address zero.
TEST_CASE("product: a footprint nothing answered names the part and prints no address") {
    constexpr ports::Capabilities kNoBaro = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Baro));
    Rig rig{kNoBaro};
    REQUIRE(rig.setup() == Status::Ok);

    for (int i = 0; i < go::kBootPartCount; i++) {
        if (go::kBootParts[i].capability != ports::Capability::Baro) continue;
        const go::BootPart& row = rig.product.boot_rows()[i];
        CHECK(row.state == go::PartState::Absent);
        REQUIRE(row.detail != nullptr);
        CHECK(std::string(row.detail) == "BME280");
    }
}
