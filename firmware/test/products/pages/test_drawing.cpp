// The drawing stack end to end, from a pixel to what reaches the glass. The radar
// cases are the load-bearing ones: on a 200-pixel span there is no centre pixel,
// so own-ship sits on the 99|100 boundary and every ring and bearing is measured
// from there. Half a pixel of drift is half a pixel of parallax on every target.
// The status cases pin the units a pilot reads first, and the widest position on
// earth still fitting its row.
#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/radar.h"
#include "products/skyblip_go/pages/sats.h"
#include "products/skyblip_go/pages/status.h"
#include "test/support/glass_text.h"

using namespace skyblip::go;
using skyblip::reads_in;
using skyblip::traffic::Level;

namespace {

int ink_in(const Glass& fb, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += fb.get_pixel(x, y) ? 1 : 0;
    return n;
}

RadarSnapshot flying(uint16_t track_deg) {
    RadarSnapshot snap;
    snap.fix_valid = true;
    snap.range_step = kDefaultRangeStep;
    snap.track_cdeg = track_deg * 100;
    snap.flight_time_valid = true;
    snap.airborne = true;
    snap.flight_seconds = 42 * 60;
    snap.receiver_listening = true;
    return snap;
}

Glass radar(const RadarSnapshot& snap) {
    Glass fb;
    draw_radar(fb, snap);
    return fb;
}

int differing_in(const Glass& a, const Glass& b, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += a.get_pixel(x, y) != b.get_pixel(x, y) ? 1 : 0;
    return n;
}

RadarSnapshot with_threat(RadarTarget* one, Level level) {
    RadarSnapshot snap = flying(0);
    one->alarm_level = level;
    snap.n_targets = 1;
    snap.targets = one;
    return snap;
}

}  // namespace

TEST_CASE("fb: pixel set/get and clear") {
    Glass fb;
    fb.clear(true);
    CHECK(fb.count_black() == 0);
    fb.set_pixel(10, 20, true);
    CHECK(fb.get_pixel(10, 20));
    CHECK(fb.count_black() == 1);
    fb.set_pixel(10, 20, false);
    CHECK_FALSE(fb.get_pixel(10, 20));
    fb.set_pixel(-1, -1, true);  // out of bounds no-op
    fb.set_pixel(999, 999, true);
    CHECK(fb.count_black() == 0);
}

TEST_CASE("fb: primitives draw something") {
    Glass fb;
    fb.clear(true);
    fb.line(0, 0, 199, 199, true);
    CHECK(fb.get_pixel(0, 0));
    CHECK(fb.get_pixel(199, 199));
    int before = fb.count_black();
    fb.circle(100, 100, 50, true, false);
    CHECK(fb.count_black() > before);
    fb.rect(10, 10, 30, 20, true, true);
    CHECK(fb.get_pixel(25, 20));
}

TEST_CASE("fb: text advances and draws glyph pixels") {
    Glass fb;
    fb.clear(true);
    int x = fb.draw_text(5, 5, "AB1", true, 1);
    CHECK(x == 5 + 3 * 6);
    CHECK(fb.count_black() > 0);
    // space draws nothing
    Glass fb2;
    fb2.clear(true);
    fb2.draw_char(0, 0, ' ', true, 1);
    CHECK(fb2.count_black() == 0);
}

// A character with no glyph prints as a space, and only the glass ever notices.
TEST_CASE("fb: every character the pages print has a glyph of its own") {
    for (const char* c = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.:/+%"; *c != 0; c++) {
        Glass fb;
        fb.clear(true);
        fb.draw_char(0, 0, *c, true, 1);
        CAPTURE(*c);
        CHECK(fb.count_black() > 0);
    }
}

TEST_CASE("radar: renders rings, own symbol and plots targets") {
    Glass fb;
    RadarTarget targets[2] = {
        {2000, 0, 100, Level::Advisory},    // north, above
        {0, -3000, -100, Level::Advisory},  // west, below, urgent
    };
    // Mirror-image targets must land mirror-image distances from the centre
    // point: east of it starts at pixel 100, west of it at 99.
    {
        RadarTarget pair[2] = {{0, 4000, 0, Level::Advisory}, {0, -4000, 0, Level::Advisory}};
        RadarSnapshot s2;
        s2.fix_valid = true;
        s2.range_step = 3;
        s2.n_targets = 2;
        s2.targets = pair;
        Glass f2;
        draw_radar(f2, s2);
        int e = -1, w = -1;
        for (int x = 100; x < 200; x++)
            if (f2.get_pixel(x, 99)) {
                e = x;
                break;
            }
        for (int x = 99; x >= 0; x--)
            if (f2.get_pixel(x, 99)) {
                w = x;
                break;
            }
        CHECK(e - 100 == 99 - w);
    }
    RadarSnapshot snap;
    snap.fix_valid = true;
    snap.range_step = 3;
    snap.n_targets = 2;
    snap.targets = targets;
    draw_radar(fb, snap);
    CHECK(fb.count_black() > 100);

    // no-fix path shows text, few pixels but non-empty
    Glass fb2;
    RadarSnapshot ns;
    ns.fix_valid = false;
    draw_radar(fb2, ns);
    CHECK(fb2.count_black() > 0);
}

TEST_CASE("radar: everything is centred on the 99|100 point, not on a pixel") {
    // 200x200 is an EVEN grid: there is no middle pixel. The centre is the point
    // where pixels 99 and 100 meet on both axes, so anything "on" the centre is
    // a PAIR of pixels. No alarm and no fix, so only rings and ship mark the edges.
    Glass fb;
    RadarSnapshot snap;
    draw_radar(fb, snap);
    // the own ship straddles the centre: its fuselage is a pair of columns
    CHECK(fb.get_pixel(99, 99));
    CHECK(fb.get_pixel(100, 99));
    CHECK(fb.get_pixel(99, 100));
    CHECK(fb.get_pixel(100, 100));
    // ... and the glyph is mirror-symmetric about that point, not about a column
    int mism = 0;
    for (int y = 88; y < 118; y++)
        for (int k = 0; k < 14; k++)
            if (fb.get_pixel(99 - k, y) != fb.get_pixel(100 + k, y)) mism++;
    CHECK(mism == 0);
    // The hot spot (the wing = the widest row, i.e. the aircraft's position)
    // sits ON the centre point, not at the glyph's bounding-box centre: targets
    // are plotted as offsets from it, so bbox-centring a long-tailed aeroplane
    // puts the wing several px forward and skews every bearing on screen.
    int widest = 0, widest_row = -1;
    for (int y = 88; y < 118; y++) {
        int n = 0;
        for (int x = 86; x < 114; x++) n += fb.get_pixel(x, y) ? 1 : 0;
        if (n > widest) {
            widest = n;
            widest_row = y;
        }
    }
    CHECK(widest_row == 99);
    // The rings are concentric with the same point: equal margin on all sides.
    int left = -1, right = -1, top = -1, bottom = -1;
    for (int x = 0; x < 200; x++)
        if (fb.get_pixel(x, 99)) {
            if (left < 0) left = x;
            right = x;
        }
    // column 50: clear of the flight clock, which erases the ring where it sits
    for (int y = 0; y < 200; y++)
        if (fb.get_pixel(50, y)) {
            if (top < 0) top = y;
            bottom = y;
        }
    CHECK(left == 199 - right);
    CHECK(top == 199 - bottom);
}

TEST_CASE("radar: the range ring is one unbroken stroke, and the only ring on the glass") {
    Glass fb;
    draw_radar(fb, flying(0));  // an empty sky in flight: the ring and own ship, nothing else
    int thinnest = 200, thickest = 0, inside_ink = 0;
    for (int y = 60; y <= 140; y++) {
        int stroke = 0;
        for (int x = 0; x < 20; x++) stroke += fb.get_pixel(x, y) ? 1 : 0;
        thinnest = stroke < thinnest ? stroke : thinnest;
        thickest = stroke > thickest ? stroke : thickest;
        for (int x = 20; x < 80; x++) inside_ink += fb.get_pixel(x, y) ? 1 : 0;
    }
    CHECK(thinnest >= 2);
    CHECK(thickest <= 4);
    CHECK(inside_ink == 0);

    // A stroke that steps diagonally reads as speckled glass, holes and all.
    int speckles = 0;
    for (int y = 1; y < 170; y++)  // above the footer, where no label bites the arc
        for (int x = 1; x < 199; x++) {
            if (!fb.get_pixel(x, y)) continue;
            const int dx = x < 100 ? 99 - x : x - 100, dy = y < 100 ? 99 - y : y - 100;
            const int r2 = dx * dx + dy * dy;
            if (r2 < 80 * 80 || r2 > 95 * 95) continue;
            const int neighbours = fb.get_pixel(x - 1, y) + fb.get_pixel(x + 1, y) +
                                   fb.get_pixel(x, y - 1) + fb.get_pixel(x, y + 1);
            if (neighbours < 2) speckles++;
        }
    CHECK(speckles == 0);
}

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

TEST_CASE("radar: the plot turns with the track, so what is ahead is up the glass") {
    RadarTarget east[1] = {{0, 2 * kMetresPerNm, 0, Level::Advisory}};
    RadarSnapshot flying_east = flying(90);
    flying_east.n_targets = 1;
    flying_east.targets = east;
    const Glass ahead = radar(flying_east);

    // 2 NM on the 4 NM ring is 46 px, and the nose is the top of the glass.
    CHECK(ahead.get_pixel(99, 99 - 46 + 1 - 2));
    CHECK(ahead.get_pixel(100, 99 - 46 + 1 - 2));
    CHECK_FALSE(ahead.get_pixel(100 + 46 + 2, 100));

    RadarSnapshot flying_north = flying_east;
    flying_north.track_cdeg = 0;
    const Glass beam = radar(flying_north);
    CHECK(beam.get_pixel(100 + 46 + 2, 100));
    CHECK_FALSE(beam.get_pixel(99, 99 - 46 + 1 - 2));
}

TEST_CASE("radar: the flight time reads in the bottom-left, and dashes before a flight") {
    CHECK(reads_in(radar(flying(47)), "0:42", 0, 176, 60, 198, 2));

    RadarSnapshot long_flight = flying(47);
    long_flight.flight_seconds = 3 * 3600 + 7 * 60 + 59;
    CHECK(reads_in(radar(long_flight), "3:07", 0, 176, 60, 198, 2));

    RadarSnapshot no_fix;
    CHECK(reads_in(radar(no_fix), "-:--", 0, 176, 60, 198, 2));

    // The sector a pilot is flying into carries the plot and nothing else.
    CHECK_FALSE(reads_in(radar(flying(47)), "0:42", 40, 0, 160, 90, 2));
}

// Back on the ground the clock is a logbook entry, and a logbook is filled to the second.
TEST_CASE("radar: a finished flight carries its seconds, a running one does not") {
    RadarSnapshot landed = flying(0);
    landed.airborne = false;
    landed.flight_seconds = 42 * 60 + 37;
    const Glass fb = radar(landed);
    CHECK(reads_in(fb, "0:42", 0, 176, 60, 198, 2));
    CHECK(reads_in(fb, "37", 45, 183, 80, 198));

    RadarSnapshot airborne_again = landed;
    airborne_again.airborne = true;
    CHECK_FALSE(reads_in(radar(airborne_again), "37", 0, 170, 80, 199));

    RadarSnapshot never_flown;
    CHECK_FALSE(reads_in(radar(never_flown), "00", 0, 170, 80, 199));
}

// The seconds are half the height of the minutes and clear only their own row of the ring.
TEST_CASE("radar: the seconds take no more ring than they cover") {
    RadarSnapshot landed = flying(0);
    landed.airborne = false;
    landed.flight_seconds = 42 * 60 + 37;
    const Glass fb = radar(landed);
    const Glass no_seconds = radar(flying(0));

    CHECK(ink_in(fb, 52, 170, 72, 186) > 0);
    for (int y = 170; y < 186; y++)
        for (int x = 52; x < 72; x++) CHECK(fb.get_pixel(x, y) == no_seconds.get_pixel(x, y));
}

TEST_CASE("radar: the range labels the ring, centred on it and cleared off it") {
    const Glass fb = radar(flying(0));
    CHECK(reads_in(fb, "4", 70, 176, 100, 198, 2));
    CHECK(reads_in(fb, "NM", 95, 183, 130, 198));

    RadarSnapshot wider = flying(0);
    wider.range_step = 3;
    CHECK(reads_in(radar(wider), "8", 70, 176, 100, 198, 2));

    // The ring is cleared off the label rather than read through it.
    for (int y = 186; y < 196; y++)
        for (int x = 82; x < 88; x++) CHECK_FALSE(fb.get_pixel(x, y));
}

// B4. One ring, picked in the unit it is read in: whole miles or whole kilometres.
TEST_CASE("radar: the ring is labelled and sized in the unit a pilot set") {
    RadarSnapshot metric = flying(0);
    metric.units = skyblip::go::Units::Metric;
    const Glass km = radar(metric);

    CHECK(reads_in(km, "8", 70, 176, 100, 198, 2));
    CHECK(reads_in(km, "KM", 95, 183, 140, 198));
    CHECK_FALSE(reads_in(km, "NM", 60, 176, 140, 198));

    // The ring is 8 km rather than the 7.4 the same step converts to, so the plot is its own.
    CHECK(range_metres(metric.range_step, metric.units) == 8000);
    CHECK(range_metres(metric.range_step, skyblip::go::Units::Nautical) == 4 * 1852);
}

TEST_CASE("radar: the footer counts what is on the glass, either side of the clock") {
    RadarTarget targets[3] = {
        {2000, 0, 0, Level::Advisory},
        {0, -3000, 0, Level::Advisory},
        {40000, 0, 0, Level::Advisory},  // four rings out: heard, and off the picture
    };
    RadarSnapshot snap = flying(0);
    snap.n_targets = 3;
    snap.targets = targets;
    const Glass fb = radar(snap);

    CHECK(reads_in(fb, "FLIGHT", 0, 168, 50, 182));
    CHECK(reads_in(fb, "0:42", 0, 176, 60, 198, 2));
    CHECK(reads_in(fb, "2", 170, 170, 200, 200, 3));
    CHECK(reads_in(fb, "ACT", 140, 175, 190, 200));

    RadarSnapshot closer = flying(0);
    closer.range_step = 1;
    const Glass near = radar(closer);
    CHECK(reads_in(near, "0", 170, 170, 200, 200, 3));
}

// An empty sky and a radio that is not listening yet look the same on the plot.
TEST_CASE("radar: a radio not yet listening counts no aircraft, it dashes") {
    RadarSnapshot deaf = flying(0);
    deaf.receiver_listening = false;
    const Glass fb = radar(deaf);

    CHECK(reads_in(fb, "-", 170, 170, 200, 200, 3));
    CHECK_FALSE(reads_in(fb, "0", 170, 170, 200, 200, 3));
    CHECK(reads_in(fb, "ACT", 140, 175, 190, 200));

    RadarSnapshot no_position = flying(0);
    no_position.fix_valid = false;
    CHECK(reads_in(radar(no_position), "-", 170, 170, 200, 200, 3));

    CHECK(reads_in(radar(flying(0)), "0", 170, 170, 200, 200, 3));
}

TEST_CASE("radar: the footer sits on one baseline, a margin clear of the glass edge") {
    const Glass fb = radar(flying(0));
    int clock_bottom = -1, range_bottom = -1, count_bottom = -1;
    for (int y = 180; y < 200; y++)
        for (int x = 0; x < 200; x++)
            if (fb.get_pixel(x, y)) {
                if (x < 60) clock_bottom = y;
                if (x > 80 && x < 120) range_bottom = y;
                if (x > 170) count_bottom = y;
            }
    CHECK(clock_bottom == count_bottom);
    CHECK(range_bottom == count_bottom);
    for (int y = count_bottom + 1; y < 200; y++)
        for (int x = 0; x < 200; x++) CHECK_FALSE(fb.get_pixel(x, y));
    CHECK(199 - count_bottom >= 4);
}

// A pilot must not have to read the footer to learn the plot is not being fed.
TEST_CASE("radar: anything but a flight is said in the ring, and a flight over the clock") {
    RadarSnapshot searching;
    searching.airborne = true;  // stale from the last flight: no fix outranks it
    const Glass no_fix = radar(searching);
    CHECK(reads_in(no_fix, "NO FIX", 40, 120, 160, 160, 2));
    CHECK_FALSE(reads_in(no_fix, "NO FIX", 0, 160, 60, 199));
    CHECK_FALSE(reads_in(no_fix, "FLIGHT", 0, 160, 60, 199));

    RadarSnapshot parked = flying(0);
    parked.airborne = false;
    const Glass ground = radar(parked);
    CHECK(reads_in(ground, "GROUND", 40, 120, 160, 160, 2));
    CHECK_FALSE(reads_in(ground, "GROUND", 0, 160, 60, 199));

    RadarSnapshot rolling = parked;
    rolling.taxiing = true;
    const Glass taxi = radar(rolling);
    CHECK(reads_in(taxi, "TAXI", 40, 120, 160, 160, 2));
    CHECK_FALSE(reads_in(taxi, "GROUND", 0, 0, 200, 199, 2));

    const Glass airborne = radar(flying(0));
    CHECK(reads_in(airborne, "FLIGHT", 0, 168, 50, 182));
    CHECK_FALSE(reads_in(airborne, "FLIGHT", 20, 20, 180, 160, 2));
}

// The one thing on this page a pilot can act on from the cockpit: land, or plug it in.
TEST_CASE("radar: a warned cell takes the ring, at the size the ring is read at") {
    RadarSnapshot low = flying(47);
    low.battery_low = true;
    low.battery_percent = 4;
    const Glass warned = radar(low);
    CHECK(reads_in(warned, "BAT 4%", 40, 120, 160, 160, 2));
    // The clock keeps its corner, and the flight keeps its word over it.
    CHECK(reads_in(warned, "0:42", 0, 176, 60, 198, 2));
    CHECK(reads_in(warned, "FLIGHT", 0, 168, 50, 182));

    // The stack moves down a slot rather than losing a reading: the cell takes the
    // banner, the state word takes the small line, and the receiver's stage drops.
    RadarSnapshot parked = low;
    parked.airborne = false;
    const Glass ground = radar(parked);
    CHECK(reads_in(ground, "BAT 4%", 40, 120, 160, 160, 2));
    CHECK(reads_in(ground, "GROUND", 40, 145, 160, 170));
    CHECK_FALSE(reads_in(ground, "GROUND", 40, 120, 160, 160, 2));

    RadarSnapshot rolling = parked;
    rolling.taxiing = true;
    CHECK(reads_in(radar(rolling), "TAXI", 40, 145, 160, 170));

    RadarSnapshot blind = low;
    blind.fix_valid = false;
    blind.stage = skyblip::gnss::Stage::Blind;
    const Glass searching = radar(blind);
    CHECK(reads_in(searching, "BAT 4%", 40, 120, 160, 160, 2));
    CHECK(reads_in(searching, "NO FIX", 40, 145, 160, 170));
    CHECK_FALSE(reads_in(searching, "BLIND", 0, 0, 200, 199));

    // A healthy cell writes nothing there at all.
    CHECK_FALSE(reads_in(radar(flying(47)), "BAT", 0, 0, 200, 199, 2));

    // The stack is read every frame, never latched: a cable clears the level in
    // core/power, the banner goes back to the state word and the stage returns.
    RadarSnapshot cabled = blind;
    cabled.battery_low = false;
    const Glass charging = radar(cabled);
    CHECK_FALSE(reads_in(charging, "BAT", 0, 0, 200, 199, 2));
    CHECK(reads_in(charging, "NO FIX", 40, 120, 160, 160, 2));
    CHECK(reads_in(charging, "BLIND", 40, 145, 160, 170));
}

// NO FIX says the plot is not being fed; the word under it says whether that is going anywhere.
TEST_CASE("radar: under NO FIX stands how far the receiver has got") {
    RadarSnapshot searching;
    searching.stage = skyblip::gnss::Stage::Blind;
    const Glass looking = radar(searching);
    CHECK(reads_in(looking, "NO FIX", 40, 120, 160, 160, 2));
    CHECK(reads_in(looking, "BLIND", 40, 145, 160, 170));

    RadarSnapshot reading = searching;
    reading.stage = skyblip::gnss::Stage::Solving;
    CHECK(reads_in(radar(reading), "SOLVING", 40, 145, 160, 170));

    RadarSnapshot quiet = searching;
    quiet.stage = skyblip::gnss::Stage::Silent;
    CHECK(reads_in(radar(quiet), "SILENT", 40, 145, 160, 170));

    // A fix is the plot itself: no word, and no seconds ticking a partial refresh out of the panel.
    RadarSnapshot fixed = flying(0);
    fixed.stage = skyblip::gnss::Stage::Fixed;
    const Glass plotted = radar(fixed);
    CHECK_FALSE(reads_in(plotted, "FIX", 0, 0, 200, 199));
    CHECK_FALSE(reads_in(plotted, "BLIND", 0, 0, 200, 199));
}

TEST_CASE("radar: a blip lands off the state word rather than erasing it") {
    // 4428 m behind is 54 px on the 4 NM ring, which is where the word stands.
    RadarTarget behind[1] = {{-4428, 0, 100, Level::None}};
    RadarSnapshot parked = flying(0);
    parked.airborne = false;
    parked.n_targets = 1;
    parked.targets = behind;

    CHECK(reads_in(radar(parked), "GROUND", 40, 120, 160, 160, 2));
}

TEST_CASE("status: every value reads in the aeronautical unit first, then SI") {
    // 1500 m = 4921 ft, 20 m/s = 39 kt, +2.0 m/s =
    // +394 fpm. Rendering is 5x7 glyphs, so the check is on the row's ink: the
    // dual-unit row is wider than a single-unit one would be.
    Glass both;
    StatusSnapshot s;
    s.fix_valid = true;
    s.utc_valid = true;
    s.sats = 9;
    s.alt_mm = 1500000;
    s.speed_mm_s = 20000;
    s.climb_mm_s = 2000;
    s.track_cdeg = 9000;
    draw_status(both, s);

    // The barometric rows only exist when a barometer answered: the pressure it
    // reads, and pressure altitude on the 1013.25 standard setting.
    Glass with_baro;
    StatusSnapshot b = s;
    b.baro_valid = true;
    b.pressure_mpa = 84556000;
    b.alt_std_m = 1500;
    draw_status(with_baro, b);
    CHECK(with_baro.count_black() > both.count_black());
}

TEST_CASE("status: the barometer row reads what the sensor resolves") {
    Glass fb;
    StatusSnapshot s;
    s.baro_valid = true;
    s.pressure_mpa = 101325253;  // the BME280's own tenths of a pascal
    s.climb_mm_s = -1234;        // -243 fpm, a rate the 0.125 m/s of ADS-L cannot hold
    draw_status(fb, s);

    CHECK(reads_in(fb, "1013.253", 0, 85, 200, 105));
    CHECK(reads_in(fb, "-243", 0, 149, 200, 169));
    CHECK(reads_in(fb, "-1.23", 0, 149, 200, 169));
}

TEST_CASE("status: the IMU field reads the hub's own bring-up word and the ball it feeds") {
    StatusSnapshot s;
    s.imu_stage = "RUN";
    s.slip_valid = true;
    s.slip_mg = -120;
    Glass running;
    draw_status(running, s);
    CHECK(reads_in(running, "IMU RUN -120mg", 0, 55, 200, 70));

    // A hub that answered and never delivered a sample: the word without a ball.
    StatusSnapshot quiet = s;
    quiet.slip_valid = false;
    Glass no_data;
    draw_status(no_data, quiet);
    CHECK(reads_in(no_data, "IMU RUN B0", 0, 55, 200, 70));
    CHECK_FALSE(reads_in(no_data, "mg", 0, 55, 200, 70));

    StatusSnapshot absent;
    Glass none;
    draw_status(none, absent);
    CHECK(reads_in(none, "IMU NONE", 0, 55, 200, 70));
}

// A silent hub and one whose FIFO nobody can step through are two different faults.
TEST_CASE("status: a hub reporting nothing says what the FIFO gave it") {
    StatusSnapshot s;
    s.imu_stage = "RUN";
    s.imu_fifo_bytes = 4096;
    Glass feeding;
    draw_status(feeding, s);
    CHECK(reads_in(feeding, "IMU RUN B99", 0, 55, 200, 70));

    StatusSnapshot stuck = s;
    stuck.imu_unparsed = 3;
    stuck.imu_error = 0x1A;
    Glass unparsed;
    draw_status(unparsed, stuck);
    CHECK(reads_in(unparsed, "IMU RUN U3 E1A", 0, 55, 200, 70));
    CHECK(reads_in(unparsed, "TRUE", 0, 55, 200, 70));
}

// The bench run that asked for this: a hub up, 18 bytes drained and no ball.
TEST_CASE("status: a silent hub reports the last thing it said about itself") {
    StatusSnapshot s;
    s.imu_stage = "RUN";
    s.imu_fifo_bytes = 18;
    s.imu_meta = 12;
    Glass overflowed;
    draw_status(overflowed, s);
    CHECK(reads_in(overflowed, "IMU RUN B18 M12", 0, 55, 200, 70));

    StatusSnapshot errored = s;
    errored.imu_errored_sensor = 4;
    errored.imu_sensor_error = 0x23;
    Glass refused;
    draw_status(refused, errored);
    CHECK(reads_in(refused, "IMU RUN SE4:23", 0, 55, 200, 70));
    CHECK(reads_in(refused, "TRUE", 0, 55, 200, 70));
}

// Only the interrupt register separates a hub producing nothing from a FIFO nobody read.
TEST_CASE("status: an announced hub shows what the hub says it is holding") {
    StatusSnapshot s;
    s.imu_stage = "RUN";
    s.imu_fifo_bytes = 18;
    s.imu_meta = 16;
    s.imu_interrupt = 0x18;
    Glass holding;
    draw_status(holding, s);
    CHECK(reads_in(holding, "IMU RUN B18 I18", 0, 55, 200, 70));
    CHECK(reads_in(holding, "TRUE", 0, 55, 200, 70));

    StatusSnapshot quiet = s;
    quiet.imu_interrupt = 0;
    Glass empty;
    draw_status(empty, quiet);
    CHECK(reads_in(empty, "IMU RUN B18 I00", 0, 55, 200, 70));
}

TEST_CASE("status: a bring-up that named the fault stops on the stage that found it") {
    StatusSnapshot s;
    s.imu_stage = "CONF";
    s.imu_fault = "NOSENS";
    Glass absent;
    draw_status(absent, s);
    CHECK(reads_in(absent, "IMU CONF NOSENS", 0, 55, 200, 70));
    CHECK(reads_in(absent, "TRUE", 0, 55, 200, 70));

    StatusSnapshot refused = s;
    refused.imu_fault = "NOCFG";
    Glass dropped;
    draw_status(dropped, refused);
    CHECK(reads_in(dropped, "IMU CONF NOCFG", 0, 55, 200, 70));
}

TEST_CASE("status: the widest sensor error still leaves the track's datum readable") {
    StatusSnapshot s;
    s.imu_stage = "RUN";
    s.imu_errored_sensor = 255;
    s.imu_sensor_error = 0xFF;
    Glass fb;
    draw_status(fb, s);

    CHECK(reads_in(fb, "IMU RUN SE255:FF", 0, 55, 200, 70));
    CHECK(reads_in(fb, "TRUE", 0, 55, 200, 70));
}

TEST_CASE("status: the widest IMU failure still leaves the track's datum readable") {
    StatusSnapshot s;
    s.imu_stage = "LOAD";
    s.imu_fault = "TIMEOUT";
    Glass fb;
    draw_status(fb, s);

    CHECK(reads_in(fb, "IMU LOAD TIMEOUT", 0, 55, 200, 70));
    CHECK(reads_in(fb, "TRUE", 0, 55, 200, 70));
}

TEST_CASE("status: the battery row states the voltage, the charge and which curve") {
    // 4.00 V is nearly full off charge and about half full on it, so the two rows
    // must not read the same - and the charging one carries the CHG marker, which
    // is more ink either way.
    StatusSnapshot s;
    s.battery_valid = true;
    s.battery_mv = 4000;
    s.battery_percent = 89;

    Glass resting;
    draw_status(resting, s);

    StatusSnapshot c = s;
    c.charging = true;
    c.battery_percent = 55;
    Glass charging;
    draw_status(charging, c);
    CHECK(charging.count_black() != resting.count_black());

    // A board with no divider fitted says so rather than reading empty.
    StatusSnapshot absent;
    Glass no_sensor;
    draw_status(no_sensor, absent);
    CHECK(no_sensor.count_black() != resting.count_black());

    // The cutoff monitor's warning, on the page: a cell at 3.45 V is low and
    // says so, and the same cell on the cable is charging, not low.
    StatusSnapshot l = s;
    l.battery_mv = 3450;
    l.battery_percent = 12;
    l.battery_low = true;
    Glass low;
    draw_status(low, l);
    StatusSnapshot q = l;
    q.battery_low = false;
    Glass quiet;
    draw_status(quiet, q);
    CHECK(low.count_black() > quiet.count_black());

    StatusSnapshot on_cable = l;
    on_cable.charging = true;
    Glass cable;
    draw_status(cable, on_cable);
    CHECK(cable.count_black() != low.count_black());

    // The row is the last one on the panel: it has to fit inside it.
    for (int y = 194; y < Glass::kH; y++)
        for (int x = 0; x < Glass::kW; x++) CHECK_FALSE(charging.get_pixel(x, y));
}

TEST_CASE("status: a receiver with no fix says how far it has got, and for how long") {
    StatusSnapshot s;
    s.sats = 9;
    s.stage = skyblip::gnss::Stage::Blind;
    s.stage_s = 48;
    Glass searching;
    draw_status(searching, s);

    CHECK(reads_in(searching, "BLIND 0:48", 0, 24, 140, 40));
    CHECK_FALSE(reads_in(searching, "SAT", 0, 24, 140, 40));
    CHECK_FALSE(reads_in(searching, "3D", 0, 24, 140, 40));

    // A date decoded is a satellite read, which is the rung a bare NO FIX hid.
    StatusSnapshot timed = s;
    timed.stage = skyblip::gnss::Stage::Solving;
    timed.stage_s = 80;
    Glass reading;
    draw_status(reading, timed);
    CHECK(reads_in(reading, "SOLVING 1:20", 0, 24, 140, 40));

    StatusSnapshot silent = s;
    silent.stage = skyblip::gnss::Stage::Silent;
    silent.stage_s = 5;
    Glass quiet;
    draw_status(quiet, silent);
    CHECK(reads_in(quiet, "SILENT 0:05", 0, 24, 140, 40));

    StatusSnapshot fixed = s;
    fixed.fix_valid = true;
    fixed.fix_mode = skyblip::gnss::kFixMode3D;
    Glass solved;
    draw_status(solved, fixed);
    CHECK(reads_in(solved, "3D", 0, 24, 140, 40));
    CHECK(reads_in(solved, "9 SAT", 0, 24, 140, 40));
    CHECK_FALSE(reads_in(solved, "BLIND", 0, 24, 140, 40));
}

// The receiver's own GSA answer where it gave one, the satellite count where it did not.
TEST_CASE("status: a solution without height reads 2D") {
    StatusSnapshot s;
    s.fix_valid = true;
    s.sats = 9;
    s.fix_mode = skyblip::gnss::kFixMode2D;
    Glass two_d;
    draw_status(two_d, s);
    CHECK(reads_in(two_d, "2D", 0, 24, 140, 40));

    StatusSnapshot unreported = s;
    unreported.fix_mode = 0;
    unreported.sats = 3;
    Glass inferred;
    draw_status(inferred, unreported);
    CHECK(reads_in(inferred, "2D", 0, 24, 140, 40));

    unreported.sats = 9;
    Glass plenty;
    draw_status(plenty, unreported);
    CHECK(reads_in(plenty, "3D", 0, 24, 140, 40));
}

// The page used to report PPS lock, a pin a pilot cannot act on.
TEST_CASE("status: the page reports whether own-ship is transmitting, not the PPS pin") {
    StatusSnapshot s;
    s.fix_valid = true;
    s.sats = 9;
    Glass silent;
    draw_status(silent, s);
    CHECK(reads_in(silent, "TX OFF", 100, 65, 200, 85));
    CHECK_FALSE(reads_in(silent, "PPS", 0, 65, 200, 85));

    StatusSnapshot seen = s;
    seen.transmitting = true;
    Glass on_air;
    draw_status(on_air, seen);
    CHECK(reads_in(on_air, "TX ON", 100, 65, 200, 85));
    CHECK_FALSE(reads_in(on_air, "TX OFF", 100, 65, 200, 85));
}

TEST_CASE("panel model: the driver's own output is what the model shows") {
    Glass fb;
    StatusSnapshot s;
    s.fix_valid = true;
    s.alt_mm = 900000;
    s.baro_valid = true;
    s.pressure_mpa = 90810000;
    draw_status(fb, s);

    skyblip::models::Ssd1681 panel;
    skyblip::parts::Ssd1681 driver(panel, panel, panel, panel.dc, panel.rst, panel.busy);
    driver.begin();
    driver.present(fb, skyblip::ports::Refresh::Full, 0);

    CHECK(panel.present_count == 1);
    CHECK(panel.last_full);
    // Round trip through the driver's inversion: what the panel holds must be
    // pixel-for-pixel what the UI drew.
    CHECK(panel.framebuffer().count_black() == fb.count_black());
    CHECK(panel.save_pgm("build/status.pgm"));
}

// B4. ADS-L carries no callsign, so the setting has exactly one job: telling
// three devices on a bench apart. It shares the header with the identity that
// does go on the air.
TEST_CASE("status: the callsign shares the header with the address, and never crowds it") {
    StatusSnapshot s;
    s.device_addr = 0xED1234;

    Glass bare;
    draw_status(bare, s);

    StatusSnapshot named = s;
    named.callsign = "D-KXYZ";
    Glass with_name;
    draw_status(with_name, named);
    CHECK(with_name.count_black() > bare.count_black());

    // The address is drawn at scale 2 from the left margin; the name is
    // right-aligned on the same row. Neither may touch the other or the rule
    // under them.
    StatusSnapshot widest = s;
    widest.callsign = "123456789";
    Glass full;
    draw_status(full, widest);
    for (int y = 3; y < 21; y++)
        for (int x = 116; x < 128; x++) CHECK_FALSE(full.get_pixel(x, y));
    for (int y = 3; y < 21; y++) CHECK_FALSE(full.get_pixel(Glass::kW - 1, y));

    // An empty callsign is a header with nothing extra on it, not a blank box.
    StatusSnapshot empty = s;
    empty.callsign = "";
    Glass none;
    draw_status(none, empty);
    CHECK(none.count_black() == bare.count_black());
}

// The wedge is the whole alarm on the glass: which way to look, from the first grade.
TEST_CASE("radar: an alarm flashes a wedge on the bearing of the threat") {
    RadarTarget east[1] = {{0, 1500, 0}};
    RadarSnapshot snap = with_threat(east, Level::Advisory);

    snap.alarm_flash = false;
    const Glass between = radar(snap);
    snap.alarm_flash = true;
    const Glass lit = radar(snap);

    CHECK(differing_in(between, lit, 130, 90, 190, 110) > 100);
    // The sky behind the pilot is not what they are being told to look at.
    CHECK(differing_in(between, lit, 10, 90, 70, 110) == 0);
    // Own ship is never inverted: it is the one mark true whatever the radio heard.
    CHECK(differing_in(between, lit, 88, 94, 112, 110) == 0);
}

// A quarter of the glass flipping is seen without looking. A narrow slice has to be read.
TEST_CASE("radar: the wedge opens 45 degrees each side of the bearing") {
    RadarTarget east[1] = {{0, 1500, 0}};
    RadarSnapshot snap = with_threat(east, Level::Advisory);

    snap.alarm_flash = false;
    const Glass between = radar(snap);
    snap.alarm_flash = true;
    const Glass lit = radar(snap);

    // 5 px boxes 70 px out, on the rays 40 and 55 degrees north of a bearing due east
    CHECK(differing_in(between, lit, 152, 53, 157, 58) == 25);
    CHECK(differing_in(between, lit, 140, 43, 145, 48) == 0);
}

// A sector that inverted the ring too flashed white gaps into the one closed curve on the page.
TEST_CASE("radar: the sector stops under the ring, which stays black through the flash") {
    RadarTarget east[1] = {{0, 1500, 0}};
    RadarSnapshot snap = with_threat(east, Level::Advisory);

    snap.alarm_flash = false;
    const Glass between = radar(snap);
    snap.alarm_flash = true;
    const Glass lit = radar(snap);

    // the 2 px stroke on the bearing, 16 rows of it
    CHECK(ink_in(lit, 190, 92, 192, 108) == 32);
    CHECK(differing_in(between, lit, 190, 92, 192, 108) == 0);
    // and the fill runs up to it, with no white channel left inside the stroke
    CHECK(differing_in(between, lit, 189, 92, 190, 108) == 16);
}

// The square is own ship, and a sector that ran through it broke the box and reversed its counts.
TEST_CASE("radar: a flashing sector keeps off the formation square, not the glass around it") {
    RadarTarget flight[2] = {
        {0, 3000, 0, Level::Advisory},
        {900, 400, 0, Level::Advisory, 0, false, 40000, 9000, 0, false, true},
    };
    RadarSnapshot snap = flying(0);
    snap.n_targets = 2;
    snap.targets = flight;
    snap.formation_members = 1;

    snap.alarm_flash = false;
    const Glass between = radar(snap);
    snap.alarm_flash = true;
    const Glass lit = radar(snap);

    // the square is 86..113 on both axes: its stroke, its counts and the airframe stand as drawn
    CHECK(differing_in(between, lit, 113, 91, 114, 109) == 0);
    CHECK(differing_in(between, lit, 88, 88, 112, 112) == 0);
    // and the sector flips the glass right up to the stroke
    CHECK(differing_in(between, lit, 114, 95, 120, 105) == 60);
    // including the glass the corner arc rounds away, which kept a white pixel of its own
    CHECK(differing_in(between, lit, 112, 87, 113, 88) == 1);
}

// The grade that fills the diamond is the grade that starts the search.
TEST_CASE("radar: the wedge is flashing by the time a target reads as a filled diamond") {
    for (const Level level : {Level::Advisory, Level::Advisory, Level::Advisory}) {
        RadarTarget east[1] = {{0, 1500, 0}};
        RadarSnapshot snap = with_threat(east, level);

        snap.alarm_flash = false;
        const Glass between = radar(snap);
        snap.alarm_flash = true;
        CHECK(differing_in(between, radar(snap), 130, 90, 190, 110) > 100);
    }

    // A contact nobody graded is a hollow diamond and no wedge at all.
    RadarTarget quiet_one[1] = {{0, 1500, 0}};
    RadarSnapshot quiet = with_threat(quiet_one, Level::None);
    quiet.alarm_flash = false;
    const Glass off_phase = radar(quiet);
    quiet.alarm_flash = true;
    CHECK(differing_in(off_phase, radar(quiet), 0, 0, Glass::kW, Glass::kH) == 0);
}

// The silence is the whole mark: no word stands in for the sector that went out.
TEST_CASE("radar: a dismissed aircraft keeps its symbol and takes its sector with it") {
    RadarTarget east[1] = {{0, 1500, 0}};
    RadarSnapshot snap = with_threat(east, Level::Advisory);
    east[0].alarm_dismissed = true;

    snap.alarm_flash = false;
    const Glass held = radar(snap);
    snap.alarm_flash = true;
    CHECK(differing_in(held, radar(snap), 0, 0, Glass::kW, Glass::kH) == 0);

    // The page is the one a target nobody graded draws, symbol apart.
    RadarTarget quiet_one[1] = {{0, 1500, 0}};
    const Glass quiet = radar(with_threat(quiet_one, Level::None));
    CHECK(differing_in(quiet, held, 130, 90, 190, 110) == 0);
    CHECK(differing_in(quiet, held, 0, 120, Glass::kW, 171) == 0);
}

// Two aircraft are two places to look, and a pilot told only about the louder one looks once.
TEST_CASE("radar: every graded aircraft flashes a sector of its own") {
    RadarTarget pair[2] = {{0, 1500, 0}, {0, -1500, 0}};
    pair[1].alarm_level = Level::Advisory;
    RadarSnapshot snap = with_threat(pair, Level::Advisory);
    snap.n_targets = 2;

    snap.alarm_flash = false;
    const Glass between = radar(snap);
    snap.alarm_flash = true;
    const Glass lit = radar(snap);

    CHECK(differing_in(between, lit, 130, 90, 190, 110) > 100);
    CHECK(differing_in(between, lit, 10, 90, 70, 110) > 100);

    // The one the pilot has in sight drops out, the other keeps flashing.
    pair[1].alarm_dismissed = true;
    snap.alarm_flash = false;
    const Glass one_left = radar(snap);
    snap.alarm_flash = true;
    CHECK(differing_in(one_left, radar(snap), 130, 90, 190, 110) > 100);
    CHECK(differing_in(one_left, radar(snap), 10, 90, 70, 110) == 0);
}

TEST_CASE("radar: a threat astern flashes its wedge down to the ring, around the range") {
    RadarTarget behind[1] = {{-1500, 0, 0}};
    RadarSnapshot snap = with_threat(behind, Level::Advisory);

    snap.alarm_flash = false;
    const Glass between = radar(snap);
    snap.alarm_flash = true;
    const Glass lit = radar(snap);

    CHECK(differing_in(between, lit, 90, 130, 110, 165) > 100);
    // glass the old wedge stopped short of: inside the ring, left of the range plaque
    CHECK(differing_in(between, lit, 62, 175, 80, 190) > 100);

    // "4 NM" is 24 px wide and keeps 3 px around it: x 85..114, y 179 down
    CHECK(differing_in(between, lit, 86, 180, 114, 198) == 0);
    // and the square corner is taken back, which is what rounds it
    CHECK(differing_in(between, lit, 85, 179, 86, 180) == 1);

    // a clock wide enough to reach inside the ring: 10:59 is five cells, 4:20 is four
    snap.flight_seconds = 10 * 3600 + 59 * 60;
    snap.alarm_flash = false;
    const Glass long_flight = radar(snap);
    snap.alarm_flash = true;
    CHECK(differing_in(long_flight, radar(snap), 4, 182, 62, 196) == 0);
}

TEST_CASE("status: the widest position on earth still fits its row") {
    // -90.0000000 and -180.0000000: eleven and twelve characters, the most the
    // format can produce. The latitude ends on the first column's unit edge and
    // the longitude block is anchored to the margin, so the worst case is where
    // they nearly meet.
    Glass fb;
    StatusSnapshot s;
    s.fix_valid = true;
    s.lat_1e7 = -900000000;
    s.lon_1e7 = -1800000000;
    draw_status(fb, s);

    const int y0 = 43, y1 = 50;  // the LAT/LON row, one glyph tall
    for (int y = y0; y < y1; y++)
        for (int x = 196; x < Glass::kW; x++) CHECK_FALSE(fb.get_pixel(x, y));

    // At least one blank column between the latitude and the LON block, and the
    // label is not touched either.
    int blank = 0;
    for (int x = 88; x < 106; x++) {
        bool ink = false;
        for (int y = y0; y < y1; y++) ink = ink || fb.get_pixel(x, y);
        if (!ink) blank++;
    }
    CHECK(blank >= 1);
    for (int y = y0; y < y1; y++) CHECK_FALSE(fb.get_pixel(23, y));
}

namespace {

skyblip::gnss::SkyView sky_of(int gps, int beidou, int used) {
    using namespace skyblip::gnss;
    SkyView sky;
    for (int i = 0; i < used; i++) sky.solving(System::Gps, static_cast<uint8_t>(1 + i));
    sky.open(System::Gps);
    for (int i = 0; i < gps; i++) {
        SatelliteView sat;
        sat.id = static_cast<uint8_t>(1 + i);
        sat.system = System::Gps;
        sat.cn0_dbhz = static_cast<uint8_t>(i < gps - 1 ? 45 - i : 0);
        sky.add(sat);
    }
    sky.open(System::Beidou);
    for (int i = 0; i < beidou; i++) {
        SatelliteView sat;
        sat.id = static_cast<uint8_t>(7 + i);
        sat.system = System::Beidou;
        sat.cn0_dbhz = 38;
        sky.add(sat);
    }
    return sky;
}

}  // namespace

// The page a pilot on the apron opens: what is up there, how loud, and which ones solved.
TEST_CASE("sats: a bar for every satellite in view, filled for the ones in the solution") {
    const skyblip::gnss::SkyView sky = sky_of(6, 4, 3);
    SatsSnapshot snap;
    snap.levels_live = true;
    snap.sky = &sky;
    snap.hdop_e2 = 90;
    snap.vdop_e2 = 150;
    snap.nav_ms = 98;
    snap.nav_valid = true;
    Glass fb;
    draw_sats(fb, snap);

    CHECK(reads_in(fb, "SATELLITES", 0, 0, 120, 12));
    CHECK(reads_in(fb, "NAV 098MS", 130, 150, 200, 168));
    CHECK(reads_in(fb, "USED 3 OF 10", 60, 0, 200, 12));
    CHECK(reads_in(fb, "GPS", 0, 130, 60, 145));
    CHECK(reads_in(fb, "BDS", 0, 130, 120, 145));
    CHECK(reads_in(fb, "HDOP 0.90", 0, 150, 130, 168));

    // A filled bar carries more ink than the hollow one beside it at the same height.
    const int first = ink_in(fb, 4, 22, 9, 132);
    const int fourth = ink_in(fb, 4 + 3 * 5, 22, 9 + 3 * 5, 132);
    CHECK(first > fourth);
}

// A level nobody has measured yet is not drawn as a level of zero.
TEST_CASE("sats: before the first GSV set the solution stands in for the bars") {
    const skyblip::gnss::SkyView sky = sky_of(6, 4, 3);
    SatsSnapshot snap;
    snap.fix_valid = true;
    snap.levels_live = false;
    snap.sky = &sky;
    snap.sats = 9;
    Glass fb;
    draw_sats(fb, snap);

    CHECK(reads_in(fb, "LEVELS COMING UP", 0, 176, 200, 196));
    CHECK(reads_in(fb, "USED 3", 60, 0, 200, 12));
    CHECK(reads_in(fb, "GPS 3", 0, 18, 120, 32));
    CHECK(ink_in(fb, 4, 40, 196, 130) == 0);
}

TEST_CASE("sats: a receiver that has heard nothing says so rather than drawing an empty chart") {
    SatsSnapshot snap;
    snap.stage = skyblip::gnss::Stage::Blind;
    Glass fb;
    draw_sats(fb, snap);
    CHECK(reads_in(fb, "NO SATELLITE HEARD", 0, 176, 200, 196));
    CHECK(reads_in(fb, "BLIND", 0, 140, 80, 158));
    CHECK(reads_in(fb, "HDOP ---", 0, 150, 130, 168));
    // No PPS edge to measure the solution against is no figure, never a zero.
    CHECK(ink_in(fb, 130, 150, 200, 168) == 0);
}
