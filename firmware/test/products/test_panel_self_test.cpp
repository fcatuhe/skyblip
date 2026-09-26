// The self test on the glass: the page that names the part which did not answer, row by row.
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
