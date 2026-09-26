// What the radar plots: traffic blips and their leaders, the formation square, own ship's vector.
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

// 2 NM ahead on the 4 NM ring is 46 px, so every case below plots on (100, 54).
constexpr int kPlotX = 100;
constexpr int kPlotY = 54;

RadarTarget abeam[1] = {{0, 3000, 0, Level::None}};

// The dots are only struck when the plot has company, so every case has some.
RadarSnapshot cruising(int32_t speed_mps) {
    RadarSnapshot snap = flying(0);
    snap.speed_mm_s = speed_mps * 1000;
    snap.n_targets = 1;
    snap.targets = abeam;
    return snap;
}

// Own-ship's vector and nothing else: the same plot without the run under it.
int marks_in(const RadarSnapshot& snap, int x0, int y0, int x1, int y1) {
    RadarSnapshot still = snap;
    still.speed_mm_s = 0;
    const Glass with = radar(snap), without = radar(still);
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (with.get_pixel(x, y) && !without.get_pixel(x, y)) n++;
    return n;
}

RadarSnapshot one_target(RadarTarget* t) {
    RadarSnapshot snap = flying(0);
    snap.n_targets = 1;
    snap.targets = t;
    return snap;
}

// One blip, three ways up: a diamond at your level, pointing up above you, down below.
TEST_CASE("radar: traffic is a blip, and the advisory fills it") {
    RadarTarget level[1] = {{2 * kMetresPerNm, 0, 0, Level::None}};
    const Glass diamond = radar(one_target(level));
    CHECK_FALSE(diamond.get_pixel(kPlotX, kPlotY));
    CHECK(diamond.get_pixel(kPlotX - 7, kPlotY));
    CHECK(diamond.get_pixel(kPlotX + 7, kPlotY));
    CHECK(diamond.get_pixel(kPlotX, kPlotY - 7));
    CHECK(diamond.get_pixel(kPlotX, kPlotY + 7));

    RadarTarget above[1] = {{2 * kMetresPerNm, 0, 300, Level::None}};
    const Glass points_up = radar(one_target(above));
    CHECK(points_up.get_pixel(kPlotX, kPlotY - 7));
    CHECK(points_up.get_pixel(kPlotX - 8, kPlotY + 1));
    CHECK_FALSE(points_up.get_pixel(kPlotX, kPlotY + 3));

    // Below you is that same blip, flipped about the point it is plotted on.
    RadarTarget below[1] = {{2 * kMetresPerNm, 0, -300, Level::None}};
    const Glass points_down = radar(one_target(below));
    for (int dy = -11; dy <= 11; dy++)
        for (int dx = -9; dx <= 9; dx++)
            CHECK(points_up.get_pixel(kPlotX + dx, kPlotY + dy) ==
                  points_down.get_pixel(kPlotX + dx, kPlotY - dy));

    RadarTarget advisory[1] = {{2 * kMetresPerNm, 0, 0, Level::Advisory}};
    const Glass filled = radar(one_target(advisory));
    CHECK(filled.get_pixel(kPlotX, kPlotY));
    CHECK(filled.get_pixel(kPlotX - 7, kPlotY));
    CHECK(ink_in(filled, kPlotX - 8, kPlotY - 8, kPlotX + 9, kPlotY + 9) >
          ink_in(diamond, kPlotX - 8, kPlotY - 8, kPlotX + 9, kPlotY + 9));
}

// Near-size covers exactly the separation that can alarm, so size is a fact and not a flourish.
TEST_CASE("radar: past the advisory's own altitude window the blip is the small one") {
    RadarTarget inside[1] = {{2 * kMetresPerNm, 0, skyblip::traffic::kAdvisoryAltM, Level::None}};
    const Glass near = radar(one_target(inside));
    CHECK(near.get_pixel(kPlotX, kPlotY - 7));
    CHECK(near.get_pixel(kPlotX - 8, kPlotY + 1));

    RadarTarget outside[1] = {
        {2 * kMetresPerNm, 0, skyblip::traffic::kAdvisoryAltM + 1, Level::None}};
    const Glass far = radar(one_target(outside));
    CHECK(far.get_pixel(kPlotX, kPlotY - 5));
    CHECK_FALSE(far.get_pixel(kPlotX, kPlotY - 7));
    CHECK(far.get_pixel(kPlotX - 6, kPlotY + 1));
    CHECK_FALSE(far.get_pixel(kPlotX - 8, kPlotY + 1));
}

TEST_CASE("radar: a leader line runs the minute ahead of the target, out to the glass") {
    // 30 m/s for 60 s is 1800 m, which is 22 px on the 4 NM ring.
    RadarTarget north[1] = {{2 * kMetresPerNm, 0, 0, Level::None, 0, false, 30000, 0}};
    const Glass ahead = radar(one_target(north));
    CHECK(ahead.get_pixel(kPlotX, kPlotY - 16));
    CHECK(ahead.get_pixel(kPlotX, kPlotY - 22));
    CHECK_FALSE(ahead.get_pixel(kPlotX, kPlotY - 30));

    RadarTarget crossing[1] = {{2 * kMetresPerNm, 0, 0, Level::None, 0, false, 30000, 9000}};
    const Glass east = radar(one_target(crossing));
    CHECK(east.get_pixel(kPlotX + 20, kPlotY));
    CHECK_FALSE(east.get_pixel(kPlotX, kPlotY - 16));

    RadarTarget parked[1] = {{2 * kMetresPerNm, 0, 0, Level::None, 0, false, 0, 0}};
    CHECK_FALSE(radar(one_target(parked)).get_pixel(kPlotX, kPlotY - 16));

    // 100 m/s from 3.8 NM out runs off the top: the ring is a scale, not a wall.
    RadarTarget fast[1] = {{(38 * kMetresPerNm) / 10, 0, 0, Level::None, 0, false, 100000, 0}};
    const Glass running_out = radar(one_target(fast));
    CHECK(running_out.get_pixel(kPlotX, 8));
    CHECK(running_out.get_pixel(kPlotX, 0));
}

// Own ship becomes the formation: one 28 px square, one digit per quadrant, and the airframe
// untouched.
TEST_CASE("radar: a formation is own ship, counted by quadrant") {
    RadarTarget flight[4] = {
        {900, 400, 0, Level::Advisory, 0, false, 40000, 9000, 0, false, true},
        {700, 600, 0, Level::Advisory, 0, false, 40000, 9000, 0, false, true},
        {-600, 500, 0, Level::Advisory, 0, false, 40000, 9000, 0, false, true},
        {-700, -400, 0, Level::Advisory, 0, false, 40000, 9000, 0, false, true},
    };
    RadarSnapshot snap = flying(0);
    snap.speed_mm_s = 40000;
    snap.n_targets = 4;
    snap.targets = flight;
    snap.formation_members = 4;
    const Glass fb = radar(snap);

    // The square is 86..113 on both axes, one blank pixel clear of the wingtips.
    CHECK(fb.get_pixel(86, 100));
    CHECK(fb.get_pixel(113, 100));
    CHECK(fb.get_pixel(100, 86));
    CHECK(fb.get_pixel(100, 113));
    CHECK_FALSE(fb.get_pixel(87, 99));
    CHECK_FALSE(fb.get_pixel(112, 99));

    // Two off the right nose, one on each quarter behind, none off the left nose.
    CHECK(reads_in(fb, "2", 106, 89, 111, 96, 1));
    CHECK(reads_in(fb, "1", 89, 104, 94, 111, 1));
    CHECK(reads_in(fb, "1", 106, 104, 111, 111, 1));
    CHECK(ink_in(fb, 89, 89, 94, 96) == 0);

    // A member is not also plotted as traffic: the square is its depiction.
    RadarSnapshot apart = snap;
    apart.formation_members = 0;
    RadarTarget loose[4] = {flight[0], flight[1], flight[2], flight[3]};
    for (RadarTarget& t : loose) t.in_formation = false;
    apart.targets = loose;
    const Glass separate = radar(apart);
    CHECK(ink_in(separate, 100, 80, 120, 95) > ink_in(fb, 100, 80, 120, 95));
    CHECK_FALSE(separate.get_pixel(86, 100));
    CHECK_FALSE(separate.get_pixel(113, 100));

    // The footer still counts them: they are aircraft, and they are on the glass.
    CHECK(reads_in(fb, "4", 170, 170, 200, 200, 3));
}

// The leader is the arc the alarm grades, so a target in a turn does not draw a tangent.
TEST_CASE("radar: a turning target's leader is its arc") {
    RadarTarget straight[1] = {{2 * kMetresPerNm, 0, 0, Level::None, 0, false, 30000, 0}};
    RadarTarget arcing[1] = {{2 * kMetresPerNm, 0, 0, Level::None, 0, false, 30000, 0, 200, true}};
    RadarSnapshot snap = flying(0);
    snap.n_targets = 1;

    snap.targets = straight;
    const Glass tangent = radar(snap);
    snap.targets = arcing;
    const Glass curved = radar(snap);

    // 30 m/s at 2 deg/s is an 859 m radius: the minute ends 16 px right, 9 px up.
    CHECK(tangent.get_pixel(kPlotX, kPlotY - 22));
    CHECK_FALSE(curved.get_pixel(kPlotX, kPlotY - 22));
    CHECK(ink_in(curved, kPlotX + 6, kPlotY - 12, kPlotX + 18, kPlotY) > 0);

    // A target whose turn rate nobody has measured yet is flown straight.
    RadarTarget unknown[1] = {
        {2 * kMetresPerNm, 0, 0, Level::None, 0, false, 30000, 0, 200, false}};
    snap.targets = unknown;
    CHECK(radar(snap).get_pixel(kPlotX, kPlotY - 22));
}

// The ring is the scale the footer reads in, and the glass around it is spare.
TEST_CASE("radar: traffic past the ring still draws, and the count stays on the ring") {
    // 4.2 NM abeam is off the 4 NM ring and still on the glass, at 96 px.
    RadarTarget beside[1] = {{0, (42 * kMetresPerNm) / 10, 0, Level::Advisory}};
    RadarSnapshot snap = flying(0);
    snap.speed_mm_s = 30000;
    snap.n_targets = 1;
    snap.targets = beside;
    const Glass fb = radar(snap);

    CHECK(fb.get_pixel(196, 100));
    CHECK(fb.get_pixel(192, 100));
    CHECK(reads_in(fb, "0", 170, 170, 200, 200, 3));
    CHECK_FALSE(fb.get_pixel(99, 77));

    // The footer owns the bottom band, so traffic outside the ring keeps off it.
    RadarTarget behind[1] = {{-6500, -4000, -200, Level::None, 0, false, 30000, 2000}};
    RadarSnapshot low = snap;
    low.targets = behind;
    RadarSnapshot none = snap;
    none.n_targets = 0;
    CHECK(ink_in(radar(low), 20, 150, 90, 199) == ink_in(radar(none), 20, 150, 90, 199));
    CHECK(reads_in(radar(low), "0:42", 0, 176, 60, 198, 2));

    // Past the glass it is gone altogether, count and all.
    RadarTarget far_out[1] = {{0, 8 * kMetresPerNm, 0, Level::Advisory}};
    RadarSnapshot beyond = snap;
    beyond.targets = far_out;
    const Glass empty = radar(beyond);
    CHECK_FALSE(empty.get_pixel(196, 100));
    CHECK_FALSE(empty.get_pixel(192, 100));
    CHECK(reads_in(empty, "0", 170, 170, 200, 200, 3));
}

TEST_CASE("radar: own ship's track is a line, with a ball on each of the next two minutes") {
    const Glass fb = radar(cruising(30));

    // 30 m/s for 60 s is 1800 m, 22 px up the glass: the ball straddles the 99|100 pair.
    CHECK(fb.get_pixel(99, 78));
    CHECK(fb.get_pixel(100, 78));
    CHECK(fb.get_pixel(97, 78));
    CHECK(fb.get_pixel(102, 78));
    CHECK_FALSE(fb.get_pixel(96, 78));
    CHECK_FALSE(fb.get_pixel(103, 78));
    // Six rows across, 75..80, and round: the corners are off it.
    CHECK(fb.get_pixel(100, 75));
    CHECK(fb.get_pixel(100, 80));
    CHECK_FALSE(fb.get_pixel(100, 74));
    CHECK_FALSE(fb.get_pixel(97, 75));

    // The line joins it to the aeroplane, and stands clear of the nose at 94.
    CHECK(fb.get_pixel(100, 85));
    CHECK(fb.get_pixel(100, 91));
    CHECK_FALSE(fb.get_pixel(100, 92));

    // Two pixels across, on that same pair, and dashed: 3 px of line, 3 px of glass.
    CHECK(fb.get_pixel(99, 85));
    CHECK_FALSE(fb.get_pixel(98, 85));
    CHECK_FALSE(fb.get_pixel(101, 85));
    CHECK(fb.get_pixel(100, 83));
    CHECK_FALSE(fb.get_pixel(99, 87));
    CHECK_FALSE(fb.get_pixel(100, 87));

    // The second minute is twice as far up the same line, and the line ends on it.
    CHECK(fb.get_pixel(100, 56));
    CHECK(fb.get_pixel(97, 56));
    CHECK(fb.get_pixel(100, 65));
    CHECK_FALSE(fb.get_pixel(100, 52));

    // Twice the speed puts the first ball where the second one was.
    CHECK(radar(cruising(60)).get_pixel(97, 56));

    // A second minute beyond the ring is dropped, and the line runs out to the stroke.
    const Glass clipped = radar(cruising(80));
    CHECK(clipped.get_pixel(100, 41));
    CHECK(clipped.get_pixel(100, 12));
    CHECK_FALSE(clipped.get_pixel(97, 12));
    CHECK_FALSE(clipped.get_pixel(100, 10));

    CHECK_FALSE(radar(cruising(0)).get_pixel(100, 78));

    RadarSnapshot searching;
    searching.speed_mm_s = 30000;
    CHECK_FALSE(radar(searching).get_pixel(100, 78));
}

// A pilot in a turn is not going where the nose points, and the line is where they will be.
TEST_CASE("radar: the vector rides own ship's turn, not its nose") {
    // 30 m/s at 1 deg/s is a 1719 m radius: 60 deg of it is 10 px right, 18 px up.
    RadarTarget behind[1] = {{-3000, 0, 0, Level::Advisory}};
    RadarSnapshot right = flying(0);
    right.speed_mm_s = 30000;
    right.n_targets = 1;
    right.targets = behind;
    right.turn_cdps = 100;
    CHECK(radar(right).get_pixel(110, 82));
    // 120 deg of it is 1.5 radii right, and the same 18 px up.
    CHECK(radar(right).get_pixel(131, 82));
    CHECK(marks_in(right, 60, 30, 99, 95) == 0);
    CHECK_FALSE(radar(right).get_pixel(100, 78));

    RadarSnapshot left = right;
    left.turn_cdps = -100;
    CHECK(radar(left).get_pixel(89, 82));
    CHECK(marks_in(left, 101, 30, 140, 95) == 0);

    RadarSnapshot straight_on = right;
    straight_on.turn_cdps = 0;
    CHECK(radar(straight_on).get_pixel(100, 78));

    // A thermalling turn closes its circle inside the aeroplane: nothing to draw.
    RadarSnapshot circling = right;
    circling.turn_cdps = 600;
    CHECK(marks_in(circling, 0, 0, 200, 200) == 0);
}

// An empty ring needs no scale: the vector is read against traffic or not at all.
TEST_CASE("radar: own ship's vector keeps off a plot with nothing on it") {
    RadarSnapshot alone = flying(0);
    alone.speed_mm_s = 30000;
    CHECK_FALSE(radar(alone).get_pixel(100, 78));
    CHECK_FALSE(radar(alone).get_pixel(100, 85));

    CHECK(radar(cruising(30)).get_pixel(100, 78));

    // Heard but outside the ring is not on the plot, and does not bring it back.
    RadarTarget far_out[1] = {{40000, 0, 0, Level::None}};
    RadarSnapshot beyond = flying(0);
    beyond.speed_mm_s = 30000;
    beyond.n_targets = 1;
    beyond.targets = far_out;
    CHECK_FALSE(radar(beyond).get_pixel(100, 78));
}

// The caret rides the blip, not the tag: a crowded glass drops tags, and a climb through your
// level is not droppable.
TEST_CASE("radar: a caret on the blip says climbing or sinking, past 500 fpm") {
    RadarTarget steady[1] = {{2 * kMetresPerNm, 0, 300, Level::Advisory, 0, true}};
    const Glass flat = radar(one_target(steady));
    CHECK_FALSE(flat.get_pixel(kPlotX, kPlotY - 11));
    CHECK_FALSE(flat.get_pixel(kPlotX, kPlotY + 11));

    // 2.5 m/s is 492 fpm: the caret is for a rate a pilot has to act on.
    RadarTarget slow[1] = {{2 * kMetresPerNm, 0, 300, Level::Advisory, 19, true}};
    CHECK_FALSE(radar(one_target(slow)).get_pixel(kPlotX, kPlotY - 11));

    RadarTarget climbing[1] = {{2 * kMetresPerNm, 0, 300, Level::Advisory, 20, true}};
    const Glass up = radar(one_target(climbing));
    CHECK(up.get_pixel(kPlotX, kPlotY - 11));
    CHECK(up.get_pixel(kPlotX - 7, kPlotY - 4));
    CHECK(up.get_pixel(kPlotX + 7, kPlotY - 4));

    RadarTarget sinking[1] = {{2 * kMetresPerNm, 0, 300, Level::Advisory, -20, true}};
    const Glass down = radar(one_target(sinking));
    CHECK(down.get_pixel(kPlotX, kPlotY + 11));
    CHECK_FALSE(down.get_pixel(kPlotX, kPlotY - 11));

    // The small cut carries the same caret, drawn at its own size.
    RadarTarget distant[1] = {{2 * kMetresPerNm, 0, 2000, Level::None, 20, true}};
    const Glass far = radar(one_target(distant));
    CHECK(far.get_pixel(kPlotX, kPlotY - 9));
    CHECK_FALSE(far.get_pixel(kPlotX, kPlotY - 11));

    // A target that never reported a rate is not credited with one.
    RadarTarget silent[1] = {{2 * kMetresPerNm, 0, 300, Level::Advisory, 40, false}};
    CHECK_FALSE(radar(one_target(silent)).get_pixel(kPlotX, kPlotY - 11));
}
