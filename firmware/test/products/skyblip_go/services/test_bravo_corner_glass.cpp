// The Bravo corner follows NO TX: top right on every page that carries NO TX top left.
#include "core/comms/peer.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/bravo_corner.h"
#include "test/support/bravo_flight.h"
#include "test/support/glass_text.h"
#include "test/support/radar_rig.h"

using namespace skyblip;

namespace {

const ui::Canvas& on(Flight& flight, go::Page page) {
    flight.go().show(page);
    flight.go().run(flight.tango.now_ms, flight.tango.now_ms + 1100);
    flight.tango.now_ms += 1100;
    REQUIRE(flight.go().product.screen().page() == page);
    return flight.glass();
}

void lose_the_link(Flight& flight) {
    flight.fly(3);
    flight.tango.air.set_in_range(false);
    flight.tango.air.drop();
    flight.fly(6);
    REQUIRE(flight.state().bravo == comms::BravoState::LinkDown);
}

}  // namespace

TEST_CASE(
    "bravo corner glass: a lost Bravo says BRAVO and why, reversed, on every page NO TX "
    "marks" *
    doctest::test_suite("main")) {
    for (const go::Page page : {go::Page::SixPack, go::Page::GMeter, go::Page::Status,
                                go::Page::Satellites, go::Page::RadioLog, go::Page::SelfTest}) {
        CAPTURE(static_cast<int>(page));
        Flight flight;
        lose_the_link(flight);
        const ui::Canvas& glass = on(flight, page);
        CHECK(go::corner_reads(glass, "BRAVO", false));
        CHECK(go::corner_reads(glass, "NO LINK", false));
    }
}

// Nearby's unit rows fill its top, and BRAVO LOG's title already ends on the corner's word.
TEST_CASE("bravo corner glass: nearby and BRAVO LOG say the corner's word in their own row") {
    Flight flight;
    lose_the_link(flight);
    CHECK_FALSE(go::corner_reads(on(flight, go::Page::Nearby), "BRAVO", false));
    CHECK_FALSE(go::corner_reads(on(flight, go::Page::BravoLog), "BRAVO", false));
    CHECK(reads_in(flight.glass(), "NO LINK", 100, 0, go::kGlassWidth, 12));
}

TEST_CASE("bravo corner glass: a live Bravo leaves the corner empty, and a low cell says so") {
    Flight flight;
    flight.fly(3);
    CHECK_FALSE(go::corner_reads(on(flight, go::Page::Status), "BRAVO"));
    flight.tango.bravo.health.status_fields =
        "\"battery_percent\":9,\"charging\":false,\"power_level\":\"low\"";
    flight.fly(2);
    const ui::Canvas& glass = on(flight, go::Page::Status);
    CHECK(go::corner_reads(glass, "BRAVO"));
    CHECK(go::corner_reads(glass, "BAT 9%"));
}

// NO TX and the Bravo corner are one family: the same margin, pad, line pitch and type.
TEST_CASE("bravo corner glass: the corner stands at NO TX's margin, mirrored") {
    go::Glass canvas;
    go::draw_bravo_corner_over(canvas, {comms::BravoState::LinkDown, false, std::nullopt});
    CHECK(reads_at(canvas, go::kGlassWidth - go::kNoTxMargin - go::text_width("NO LINK", 1),
                   go::kNoTxReasonY, "NO LINK", 1, false));
    CHECK(canvas.get_pixel(go::kGlassWidth - go::kNoTxMargin + go::kNoTxPad - 1,
                           go::kNoTxMargin - go::kNoTxPad));
    CHECK_FALSE(canvas.get_pixel(go::kGlassWidth - go::kNoTxMargin + go::kNoTxPad,
                                 go::kNoTxMargin - go::kNoTxPad));
}
