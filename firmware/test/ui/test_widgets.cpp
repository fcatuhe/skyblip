// The ownship symbol's placement is a bearing-accuracy property, not decoration:
// bbox-centring it once dragged the wing 4 px forward, which put a target 20 px
// abeam 11 degrees off its true bearing. These pin the hot spot and the symmetry.
#include <initializer_list>

#include "doctest/doctest.h"
#include "ui/canvas.h"
#include "ui/widgets/blip.h"
#include "ui/widgets/skyship.h"

using namespace skyblip::ui;

namespace {
using TestPanel = Panel<200, 200>;
constexpr int kCx = 100;
constexpr int kCy = 100;
constexpr int kSpan = 24;
constexpr int kOriginRow = 5;

// Inked rows of the wing band at one column: the chord the planform draws there.
int chord_at(const TestPanel& canvas, int x) {
    int chord = 0;
    for (int y = kCy - 1; y <= kCy + 3; y++) chord += canvas.get_pixel(x, y) ? 1 : 0;
    return chord;
}
}  // namespace

TEST_CASE("skyship: the hot spot lands on the point it was given") {
    TestPanel canvas;
    canvas.clear(/*white=*/true);
    draw_skyship(canvas, kCx, kCy);

    // The wing's full-span row IS the hot spot, so it must be inked at cy.
    CHECK(canvas.get_pixel(kCx, kCy));
    CHECK(canvas.get_pixel(kCx - 1, kCy));
    // And nothing may sit above the nose or below the tail.
    for (int x = 0; x < TestPanel::kW; x++) {
        CHECK_FALSE(canvas.get_pixel(x, kCy - kOriginRow - 1));
        CHECK_FALSE(canvas.get_pixel(x, kCy - kOriginRow + 16));
    }
}

TEST_CASE("skyship: the sprite is symmetric about the centre pixel pair") {
    TestPanel canvas;
    canvas.clear(true);
    draw_skyship(canvas, kCx, kCy);

    for (int y = kCy - kOriginRow; y < kCy - kOriginRow + 16; y++)
        for (int d = 0; d < kSpan / 2; d++)
            CHECK(canvas.get_pixel(kCx + d, y) == canvas.get_pixel(kCx - 1 - d, y));
}

// The trailing taper was once deleted as ink aft of the hot spot, leaving a slab.
TEST_CASE("skyship: the wing is elliptic, deepest at the root") {
    TestPanel canvas;
    canvas.clear(true);
    draw_skyship(canvas, kCx, kCy);

    const int tip = chord_at(canvas, kCx - kSpan / 2);
    const int mid = chord_at(canvas, kCx - kSpan / 2 + 3);
    const int root = chord_at(canvas, kCx - kSpan / 2 + 6);
    CHECK(tip < mid);
    CHECK(mid < root);
}

TEST_CASE("skyship: it draws the same aircraft wherever it is asked to") {
    TestPanel a, b;
    a.clear(true);
    b.clear(true);
    draw_skyship(a, kCx, kCy);
    draw_skyship(b, kCx + 20, kCy + 20);
    CHECK(a.count_black() == b.count_black());
    CHECK(a.count_black() > 40);
}

// The blip says above or below, inside the advisory's window or not, and which way it is going.
namespace {
int ink(const TestPanel& canvas) {
    int n = 0;
    for (int y = 0; y < TestPanel::kH; y++)
        for (int x = 0; x < TestPanel::kW; x++) n += canvas.get_pixel(x, y) ? 1 : 0;
    return n;
}

TestPanel blip(Vertical vertical, Trend trend, bool alarm = false) {
    TestPanel canvas;
    canvas.clear(/*white=*/true);
    draw_blip(canvas, kCx, kCy, vertical, trend, alarm);
    return canvas;
}
}  // namespace

TEST_CASE("blip: a blip below is the blip above, flipped about the plot point") {
    for (const Vertical above : {Vertical::Above, Vertical::FarAbove}) {
        const Vertical below = above == Vertical::Above ? Vertical::Below : Vertical::FarBelow;
        for (const Trend trend : {Trend::Climbing, Trend::Steady, Trend::Sinking}) {
            const Trend opposite = trend == Trend::Climbing  ? Trend::Sinking
                                   : trend == Trend::Sinking ? Trend::Climbing
                                                             : Trend::Steady;
            const TestPanel up = blip(above, trend);
            const TestPanel down = blip(below, opposite);
            for (int y = 0; y < TestPanel::kH; y++)
                for (int x = 0; x < TestPanel::kW; x++)
                    CHECK(up.get_pixel(x, y) == down.get_pixel(x, 2 * kCy - y));
        }
    }
}

TEST_CASE("blip: the caret keeps its place whatever the body under it is") {
    const int expected_above = blip_above(Vertical::Above, Trend::Climbing);
    const int expected_below = blip_below(Vertical::Above, Trend::Sinking);
    for (const Vertical vertical : {Vertical::Above, Vertical::Level, Vertical::Below}) {
        CHECK(blip_above(vertical, Trend::Climbing) == expected_above);
        CHECK(blip_below(vertical, Trend::Sinking) == expected_below);
    }
    // The caret pair is symmetric about the plot point, so a flipped body keeps both places.
    CHECK(blip(Vertical::Below, Trend::Climbing).get_pixel(kCx, kCy - expected_above));
    CHECK(blip(Vertical::Below, Trend::Sinking).get_pixel(kCx, kCy + expected_below));
}

TEST_CASE("blip: past the advisory's window the blip is smaller, and carries no less meaning") {
    CHECK(blip_above(Vertical::FarAbove, Trend::Steady) <
          blip_above(Vertical::Above, Trend::Steady));
    CHECK(blip_beside(Vertical::FarAbove) < blip_beside(Vertical::Above));
    CHECK(ink(blip(Vertical::FarAbove, Trend::Climbing)) <
          ink(blip(Vertical::Above, Trend::Climbing)));
    // Still points up: apex on the centreline, bar under the plot point.
    CHECK(blip(Vertical::FarAbove, Trend::Steady)
              .get_pixel(kCx, kCy - blip_above(Vertical::FarAbove, Trend::Steady)));
    CHECK(blip(Vertical::FarAbove, Trend::Steady).get_pixel(kCx, kCy + 1));
}

TEST_CASE("blip: the alarm fills the blip it was already drawing") {
    const TestPanel hollow = blip(Vertical::Level, Trend::Steady);
    const TestPanel filled = blip(Vertical::Level, Trend::Steady, /*alarm=*/true);
    CHECK_FALSE(hollow.get_pixel(kCx, kCy));
    CHECK(filled.get_pixel(kCx, kCy));
    CHECK(ink(filled) > ink(hollow));
    // Filling is ink, never reach: an advisory must not move a tag off its target.
    for (const Vertical vertical : {Vertical::Above, Vertical::Level, Vertical::Below}) {
        const TestPanel a = blip(vertical, Trend::Climbing);
        const TestPanel b = blip(vertical, Trend::Climbing, /*alarm=*/true);
        for (int y = 0; y < TestPanel::kH; y++)
            for (int x = 0; x < TestPanel::kW; x++)
                if (a.get_pixel(x, y)) CHECK(b.get_pixel(x, y));
    }
}
