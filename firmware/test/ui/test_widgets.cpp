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
constexpr int kCentreX = 100;
constexpr int kCentreY = 100;
constexpr int kSpan = 24;
constexpr int kOriginRow = 5;

// Inked rows of the wing band at one column: the chord the planform draws there.
int chord_at(const TestPanel& canvas, int x) {
    int chord = 0;
    for (int y = kCentreY - 1; y <= kCentreY + 3; y++) chord += canvas.get_pixel(x, y) ? 1 : 0;
    return chord;
}
}  // namespace

TEST_CASE("skyship: the hot spot lands on the point it was given") {
    TestPanel canvas;
    canvas.clear(/*white=*/true);
    draw_skyship(canvas, kCentreX, kCentreY);

    // The wing's full-span row IS the hot spot, so it must be inked at cy.
    CHECK(canvas.get_pixel(kCentreX, kCentreY));
    CHECK(canvas.get_pixel(kCentreX - 1, kCentreY));
    // And nothing may sit above the nose or below the tail.
    for (int x = 0; x < TestPanel::kWidth; x++) {
        CHECK_FALSE(canvas.get_pixel(x, kCentreY - kOriginRow - 1));
        CHECK_FALSE(canvas.get_pixel(x, kCentreY - kOriginRow + 16));
    }
}

TEST_CASE("skyship: the sprite is symmetric about the centre pixel pair") {
    TestPanel canvas;
    canvas.clear(true);
    draw_skyship(canvas, kCentreX, kCentreY);

    for (int y = kCentreY - kOriginRow; y < kCentreY - kOriginRow + 16; y++)
        for (int offset = 0; offset < kSpan / 2; offset++)
            CHECK(canvas.get_pixel(kCentreX + offset, y) ==
                  canvas.get_pixel(kCentreX - 1 - offset, y));
}

// The trailing taper was once deleted as ink aft of the hot spot, leaving a slab.
TEST_CASE("skyship: the wing is elliptic, deepest at the root") {
    TestPanel canvas;
    canvas.clear(true);
    draw_skyship(canvas, kCentreX, kCentreY);

    const int tip = chord_at(canvas, kCentreX - kSpan / 2);
    const int mid = chord_at(canvas, kCentreX - kSpan / 2 + 3);
    const int root = chord_at(canvas, kCentreX - kSpan / 2 + 6);
    CHECK(tip < mid);
    CHECK(mid < root);
}

TEST_CASE("skyship: it draws the same aircraft wherever it is asked to") {
    TestPanel climbing, alarmed;
    climbing.clear(true);
    alarmed.clear(true);
    draw_skyship(climbing, kCentreX, kCentreY);
    draw_skyship(alarmed, kCentreX + 20, kCentreY + 20);
    CHECK(climbing.count_black() == alarmed.count_black());
    CHECK(climbing.count_black() > 40);
}

// The blip says above or below, inside the advisory's window or not, and which way it is going.
namespace {
int ink(const TestPanel& canvas) {
    int count = 0;
    for (int y = 0; y < TestPanel::kHeight; y++)
        for (int x = 0; x < TestPanel::kWidth; x++) count += canvas.get_pixel(x, y) ? 1 : 0;
    return count;
}

TestPanel blip(Vertical vertical, Trend trend, bool alarm = false) {
    TestPanel canvas;
    canvas.clear(/*white=*/true);
    draw_blip(canvas, kCentreX, kCentreY, vertical, trend, alarm);
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
            for (int y = 0; y < TestPanel::kHeight; y++)
                for (int x = 0; x < TestPanel::kWidth; x++)
                    CHECK(up.get_pixel(x, y) == down.get_pixel(x, 2 * kCentreY - y));
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
    CHECK(blip(Vertical::Below, Trend::Climbing).get_pixel(kCentreX, kCentreY - expected_above));
    CHECK(blip(Vertical::Below, Trend::Sinking).get_pixel(kCentreX, kCentreY + expected_below));
}

TEST_CASE("blip: past the advisory's window the blip is smaller, and carries no less meaning") {
    CHECK(blip_above(Vertical::FarAbove, Trend::Steady) <
          blip_above(Vertical::Above, Trend::Steady));
    CHECK(blip_beside(Vertical::FarAbove) < blip_beside(Vertical::Above));
    CHECK(ink(blip(Vertical::FarAbove, Trend::Climbing)) <
          ink(blip(Vertical::Above, Trend::Climbing)));
    // Still points up: apex on the centreline, bar under the plot point.
    CHECK(blip(Vertical::FarAbove, Trend::Steady)
              .get_pixel(kCentreX, kCentreY - blip_above(Vertical::FarAbove, Trend::Steady)));
    CHECK(blip(Vertical::FarAbove, Trend::Steady).get_pixel(kCentreX, kCentreY + 1));
}

TEST_CASE("blip: the alarm fills the blip it was already drawing") {
    const TestPanel hollow = blip(Vertical::Level, Trend::Steady);
    const TestPanel filled = blip(Vertical::Level, Trend::Steady, /*alarm=*/true);
    CHECK_FALSE(hollow.get_pixel(kCentreX, kCentreY));
    CHECK(filled.get_pixel(kCentreX, kCentreY));
    CHECK(ink(filled) > ink(hollow));
    // Filling is ink, never reach: an advisory must not move a tag off its target.
    for (const Vertical vertical : {Vertical::Above, Vertical::Level, Vertical::Below}) {
        const TestPanel climbing = blip(vertical, Trend::Climbing);
        const TestPanel alarmed = blip(vertical, Trend::Climbing, /*alarm=*/true);
        for (int y = 0; y < TestPanel::kHeight; y++)
            for (int x = 0; x < TestPanel::kWidth; x++)
                if (climbing.get_pixel(x, y)) CHECK(alarmed.get_pixel(x, y));
    }
}
