// PLOT ALL: every aircraft past the ring stands on the rim at its bearing, whatever its distance.
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/radar.h"
#include "test/support/glass_ink.h"
#include "test/support/glass_text.h"
#include "test/support/radar_rig.h"

using namespace skyblip::go;
using skyblip::ink_in;
using skyblip::reads_in;
using skyblip::traffic::Level;

namespace {

// The rim is 87 px out, so dead ahead a mark stands on (100, 13), the ring's top stroke at 24.
constexpr int kRimAheadY = 13;
constexpr int kRingTopY = 24;

RadarSnapshot plotting_all(RadarTarget* targets, int n) {
    RadarSnapshot snap = flying(0);
    snap.plot = RadarPlot::All;
    snap.targets = targets;
    snap.n_targets = n;
    snap.heard = n;
    return snap;
}

bool diamond_on(const Glass& fb, int x, int y) {
    return fb.get_pixel(x - 7, y) && fb.get_pixel(x + 7, y) && fb.get_pixel(x, y - 7) &&
           fb.get_pixel(x, y + 7) && !fb.get_pixel(x, y);
}

}  // namespace

TEST_CASE("radar rim: an aircraft past the ring stands on the rim at its bearing") {
    RadarTarget six_ahead[1] = {{6 * kMetresPerNm, 0, 0, Level::None}};
    CHECK(diamond_on(radar(plotting_all(six_ahead, 1)), 100, kRimAheadY));

    RadarTarget right[1] = {{0, 6 * kMetresPerNm, 0, Level::None}};
    const Glass abeam = radar(plotting_all(right, 1));
    CHECK(diamond_on(abeam, 187, 100));
    CHECK(ink_in(abeam, 0, 0, 200, 60) == ink_in(radar(plotting_all(right, 0)), 0, 0, 200, 60));
}

// To scale, forty kilometres is off the glass and nothing is drawn: the rim is what shows it.
TEST_CASE("radar rim: distance does not take an aircraft off the glass") {
    RadarTarget far_ahead[1] = {{40000, 0, 0, Level::None}};
    CHECK(diamond_on(radar(plotting_all(far_ahead, 1)), 100, kRimAheadY));

    RadarSnapshot to_scale = plotting_all(far_ahead, 1);
    to_scale.plot = RadarPlot::ToScale;
    CHECK(ink_in(radar(to_scale), 90, 11, 110, 30) == 0);
}

TEST_CASE("radar rim: the ring shrinks to leave the rim its band, and the range stays on it") {
    const Glass empty = radar(plotting_all(nullptr, 0));
    CHECK(empty.get_pixel(100, kRingTopY));
    CHECK_FALSE(empty.get_pixel(100, 8));
    CHECK(reads_in(empty, "4", 80, 160, 120, 190, 2));
}

// The minute is drawn where it would be to scale, from where the aircraft really is.
TEST_CASE("radar rim: a pinned aircraft's leader is the part of its minute inside the ring") {
    // 5 NM ahead at 70 m/s south is 2.73 NM in a minute: 52 px out, inside the 76 px ring.
    RadarTarget inbound[1] = {{5 * kMetresPerNm, 0, 0, Level::None, 0, false, 70000, 18000}};
    const Glass fb = radar(plotting_all(inbound, 1));
    CHECK(ink_in(fb, 98, 30, 102, 44) > 0);
    CHECK(ink_in(fb, 98, 56, 102, 90) == 0);
    CHECK(ink_in(fb, 98, 21, 102, kRingTopY) == 0);

    RadarTarget crossing[1] = {{5 * kMetresPerNm, 0, 0, Level::None, 0, false, 30000, 9000}};
    const Glass passing = radar(plotting_all(crossing, 1));
    CHECK(ink_in(passing, 75, 32, 125, 85) == 0);
}

TEST_CASE("radar rim: marks that would overlap are one mark, with the count beside it") {
    RadarTarget pair[2] = {{6 * kMetresPerNm, 0, 0, Level::None},
                           {6 * kMetresPerNm + 800, 200, 250, Level::None}};
    const Glass fb = radar(plotting_all(pair, 2));
    CHECK(diamond_on(fb, 100, kRimAheadY));
    CHECK(reads_in(fb, "2", 108, 5, 125, 22));
}

// The one drawn is the one a pilot needs first: the advisory, then the nearest in altitude.
TEST_CASE("radar rim: a group wears the advisory's mark, else the closest in altitude") {
    RadarTarget level_last[2] = {{6 * kMetresPerNm + 800, 0, 250, Level::None},
                                 {6 * kMetresPerNm, 0, 0, Level::None}};
    CHECK(diamond_on(radar(plotting_all(level_last, 2)), 100, kRimAheadY));

    RadarTarget advisory[2] = {{6 * kMetresPerNm, 0, 0, Level::None},
                               {6 * kMetresPerNm + 800, 0, 250, Level::Advisory}};
    const Glass alarmed = radar(plotting_all(advisory, 2));
    CHECK_FALSE(diamond_on(alarmed, 100, kRimAheadY));
    CHECK(alarmed.get_pixel(100, kRimAheadY));
}

TEST_CASE("radar rim: a count gives way to another aircraft's mark") {
    // 12 degrees right of the nose is 18 px along the rim: a mark where the count wanted to go.
    RadarTarget crowded[3] = {{6 * kMetresPerNm, 0, 0, Level::None},
                              {6 * kMetresPerNm + 800, 200, 250, Level::None},
                              {10870, 2310, 0, Level::None}};
    const Glass fb = radar(plotting_all(crowded, 3));
    CHECK_FALSE(reads_in(fb, "2", 105, 3, 130, 25));
    CHECK(reads_in(fb, "2", 75, 3, 95, 25));
}

TEST_CASE("radar rim: abeam, the count stands under its mark") {
    RadarTarget pair[2] = {{0, 6 * kMetresPerNm, 0, Level::None},
                           {200, 6 * kMetresPerNm + 800, 250, Level::None}};
    CHECK(reads_in(radar(plotting_all(pair, 2)), "2", 180, 108, 196, 125));
}

TEST_CASE("radar rim: an aircraft astern steps out from under the range label") {
    RadarTarget astern[1] = {{-6 * kMetresPerNm, 0, 0, Level::None}};
    const Glass fb = radar(plotting_all(astern, 1));
    const Glass empty = radar(plotting_all(nullptr, 0));
    CHECK(ink_in(fb, 82, 162, 119, 188) == ink_in(empty, 82, 162, 119, 188));
    CHECK(ink_in(fb, 119, 172, 137, 192) > 0);
}
