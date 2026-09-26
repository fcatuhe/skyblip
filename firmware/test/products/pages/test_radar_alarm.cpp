// An alarm on the radar: a flashing sector on the bearing of every graded aircraft.
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/radar.h"
#include "test/support/glass_ink.h"
#include "test/support/radar_rig.h"

using namespace skyblip::go;
using skyblip::ink_in;
using skyblip::traffic::Level;

namespace {

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
