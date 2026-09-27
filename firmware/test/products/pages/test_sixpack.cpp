// Read against a pilot's habit, not pixels: two dials on one value looks right and lies.
#include <cmath>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/sixpack.h"

using namespace skyblip::go;

namespace {
// The six dial centres, in the order the panel draws them.
struct Tile {
    int cx, cy;
};
const Tile kTiles[6] = {{34, 67}, {100, 67}, {166, 67}, {34, 133}, {100, 133}, {166, 133}};

// The number sits outside the glass: above the top row, below the bottom one.
const int kValueScale = 2;
int value_y(const Tile& t) { return t.cy < 100 ? t.cy - 31 - 7 - 7 * kValueScale : t.cy + 31 + 7; }

// Ink at the point `deg` clockwise from the top: only the needle and the shading reach kFaceR.
const int kFaceR = 20;
const int kBandR = 27;

bool ink_at(const Glass& fb, Tile t, double deg, int r = kFaceR) {
    const double a = deg * 3.14159265358979 / 180.0;
    const int x = t.cx + static_cast<int>(std::lround(r * std::sin(a)));
    const int y = t.cy - static_cast<int>(std::lround(r * std::cos(a)));
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (fb.get_pixel(x + dx, y + dy)) return true;
    return false;
}

bool title_matches(const Glass& fb, Tile t, const char* text) {
    const int y0 = t.cy < 100 ? value_y(t) - 4 - 7 : value_y(t) + 7 * kValueScale + 4;
    Glass expected;
    expected.clear(true);
    int n = 0;
    while (text[n]) n++;
    expected.draw_text(t.cx - (n * 6) / 2, y0, text, true, 1);
    for (int y = y0; y < y0 + 7; y++)
        for (int x = t.cx - 32; x <= t.cx + 32; x++)
            if (fb.get_pixel(x, y) != expected.get_pixel(x, y)) return false;
    return true;
}

int black_in(const Glass& fb, Tile t, int r) {
    int n = 0;
    for (int y = t.cy - r; y <= t.cy + r; y++)
        for (int x = t.cx - r; x <= t.cx + r; x++)
            if (fb.get_pixel(x, y)) n++;
    return n;
}

// The number under a dial, matched as the ink the page would draw for it.
bool value_matches(const Glass& fb, Tile t, const char* text) {
    const int y0 = value_y(t);
    Glass expected;
    expected.clear(true);
    int n = 0;
    while (text[n]) n++;
    expected.draw_text(t.cx - (n * 6 * kValueScale) / 2, y0, text, true, kValueScale);
    for (int y = y0; y < y0 + 7 * kValueScale; y++)
        for (int x = t.cx - 32; x <= t.cx + 32; x++)
            if (fb.get_pixel(x, y) != expected.get_pixel(x, y)) return false;
    return true;
}

SixPackSnapshot flying() {
    SixPackSnapshot s;
    s.data_valid = true;
    s.units = skyblip::go::Units::Nautical;
    s.speed_kt = 90;
    s.alt_ft = 3450;
    s.vs_fpm = 500;
    s.vs_valid = true;
    s.track_deg = 270;
    s.turn_cdps = 300;
    s.battery_percent = 64;
    s.battery_valid = true;
    return s;
}
}  // namespace

TEST_CASE("sixpack: six dials are drawn, each with its own needle") {
    const SixPackSnapshot s = flying();

    Glass fb;
    draw_sixpack(fb, s);
    for (const Tile& t : kTiles) CHECK(black_in(fb, t, 30) > 60);
}

// An instrument with no needle reads as a broken instrument, so a lost fix parks them at rest.
TEST_CASE("sixpack: without a fix the needles park at zero and the numbers withhold") {
    SixPackSnapshot none;
    Glass fb;
    draw_sixpack(fb, none);

    for (const Tile& t : kTiles) CHECK(black_in(fb, t, 30) > 60);

    // Zero speed hangs its needle down, zero altitude stands its own up.
    CHECK(fb.get_pixel(kTiles[0].cx, kTiles[0].cy + kFaceR));
    CHECK_FALSE(fb.get_pixel(kTiles[0].cx - 1, kTiles[0].cy + kFaceR));
    CHECK(fb.get_pixel(kTiles[2].cx, kTiles[2].cy - kFaceR));
    CHECK_FALSE(fb.get_pixel(kTiles[2].cx - 2, kTiles[2].cy - kFaceR));
    CHECK(ink_at(fb, kTiles[5], -90));  // a level vario points at its zero
    CHECK(ink_at(fb, kTiles[3], 90));   // and the turn coordinator's wings are level
    CHECK(ink_at(fb, kTiles[3], -90));

    for (const Tile& t : kTiles)
        if (&t != &kTiles[1]) CHECK(value_matches(fb, t, "---"));
    CHECK(value_matches(fb, kTiles[1], "--%"));
}

// The barometer measures a climb of its own, so the vario is not a GNSS instrument.
TEST_CASE("sixpack: a baro vertical speed reads on without a fix") {
    SixPackSnapshot searching;
    searching.vs_fpm = -1000;
    searching.vs_valid = true;
    Glass fb;
    draw_sixpack(fb, searching);

    CHECK(value_matches(fb, kTiles[5], "-1000"));
    CHECK(fb.get_pixel(kTiles[5].cx, kTiles[5].cy + kFaceR));
    CHECK(value_matches(fb, kTiles[0], "---"));
    CHECK(value_matches(fb, kTiles[2], "---"));

    // No barometer and no fix is no vertical speed, needle at rest.
    SixPackSnapshot blind = searching;
    blind.vs_valid = false;
    Glass none;
    draw_sixpack(none, blind);
    CHECK(value_matches(none, kTiles[5], "---"));
    CHECK(none.get_pixel(kTiles[5].cx - kFaceR, kTiles[5].cy));
}

// Five dials read the receiver and one reads the cell, and a lost fix is the receiver's.
TEST_CASE("sixpack: a fix lost in flight withholds the dials and leaves the cell alone") {
    SixPackSnapshot lost = flying();
    lost.data_valid = false;
    Glass fb;
    draw_sixpack(fb, lost);
    CHECK(value_matches(fb, kTiles[1], "64%"));
    CHECK(value_matches(fb, kTiles[0], "---"));
}

TEST_CASE("sixpack: needles move with the data they show") {
    SixPackSnapshot a;
    a.data_valid = true;
    a.units = skyblip::go::Units::Nautical;
    a.speed_kt = 40;
    a.alt_ft = 1200;
    a.track_deg = 0;
    SixPackSnapshot b = a;
    b.speed_kt = 140;
    b.alt_ft = 1900;
    b.track_deg = 180;

    Glass fa, fbuf;
    draw_sixpack(fa, a);
    draw_sixpack(fbuf, b);

    bool differs[6] = {};
    for (int i = 0; i < 6; i++) {
        for (int y = kTiles[i].cy - 28; y <= kTiles[i].cy + 28 && !differs[i]; y++)
            for (int x = kTiles[i].cx - 28; x <= kTiles[i].cx + 28; x++)
                if (fa.get_pixel(x, y) != fbuf.get_pixel(x, y)) {
                    differs[i] = true;
                    break;
                }
    }
    CHECK(differs[0]);  // airspeed
    CHECK(differs[2]);  // altimeter
    CHECK(differs[4]);  // heading
}

TEST_CASE("sixpack: the altimeter reads like a three-pointer, the card like a compass") {
    SixPackSnapshot s;
    s.data_valid = true;
    s.units = skyblip::go::Units::Nautical;
    s.alt_ft = 2500;  // long hand at 500 ft (down), short hand at 2.5/10 (right)
    Glass fb;
    draw_sixpack(fb, s);

    const Tile alt = kTiles[2];
    CHECK(fb.get_pixel(alt.cx, alt.cy + 20));        // hundreds hand, straight down
    CHECK(fb.get_pixel(alt.cx + 12, alt.cy));        // thousands hand, quarter turn
    CHECK_FALSE(fb.get_pixel(alt.cx + 20, alt.cy));  // and it is the SHORT one
    CHECK_FALSE(fb.get_pixel(alt.cx, alt.cy - 20));

    // The compass card turns with the track: north swings to the right when the
    // aircraft flies west.
    SixPackSnapshot west = s;
    west.track_deg = 270;
    Glass fw;
    draw_sixpack(fw, west);
    const Tile hdg = kTiles[4];
    int right = 0, left = 0;
    for (int y = hdg.cy - 8; y <= hdg.cy + 8; y++)
        for (int d = 12; d <= 24; d++) {
            if (fw.get_pixel(hdg.cx + d, y)) right++;
            if (fw.get_pixel(hdg.cx - d, y)) left++;
        }
    CHECK(right > left);
}

// B4. A dial has one needle and one number, so the km/h pilot has to ask for it.
TEST_CASE("sixpack: the unit setting decides the speed dial, and only the speed dial") {
    SixPackSnapshot nautical;
    nautical.data_valid = true;
    nautical.units = skyblip::go::Units::Nautical;
    nautical.speed_kt = 90;  // 166.7 km/h
    nautical.alt_ft = 3450;
    nautical.vs_fpm = 500;
    nautical.vs_valid = true;
    nautical.track_deg = 7;
    SixPackSnapshot metric = nautical;
    metric.units = skyblip::go::Units::Metric;

    Glass fi, fm;
    draw_sixpack(fi, nautical);
    draw_sixpack(fm, metric);

    // The number under a dial is the converted one, drawn where the page draws
    // it: 90 kt reads 167, and it is not the same ink as 90.
    CHECK(value_matches(fi, kTiles[0], "90"));
    CHECK(value_matches(fm, kTiles[0], "167"));

    const Glass* faces[2] = {&fi, &fm};
    for (const Glass* fb : faces) {
        CHECK(value_matches(*fb, kTiles[2], "3450"));
        CHECK(value_matches(*fb, kTiles[5], "+500"));
        CHECK(value_matches(*fb, kTiles[4], "007"));
    }
    for (int i = 1; i < 6; i++) CHECK(black_in(fm, kTiles[i], 30) == black_in(fi, kTiles[i], 30));
}

TEST_CASE("sixpack: the middle number is the state of charge, with the unit on the figure") {
    SixPackSnapshot s = flying();
    Glass fb;
    draw_sixpack(fb, s);
    CHECK(value_matches(fb, kTiles[1], "64%"));

    SixPackSnapshot full = s;
    full.battery_percent = 100;
    Glass ff;
    draw_sixpack(ff, full);
    CHECK(value_matches(ff, kTiles[1], "100%"));
}

// A unit with no divider fitted draws the shape of the reading, never a zero that reads as flat.
TEST_CASE("sixpack: a cell nobody measured withholds the figure and keeps the sign") {
    SixPackSnapshot s = flying();
    s.battery_valid = false;
    Glass fb;
    draw_sixpack(fb, s);
    CHECK(value_matches(fb, kTiles[1], "--%"));
}

// Two dashes and a hub read as a needle: what leans here is an aeroplane seen from behind.
TEST_CASE("sixpack: the turn coordinator flies an aeroplane between four marks") {
    const Tile turn = kTiles[3];
    SixPackSnapshot level = flying();
    level.turn_cdps = 0;
    Glass fb;
    draw_sixpack(fb, level);

    CHECK(fb.get_pixel(turn.cx - 22, turn.cy));  // wings
    CHECK(fb.get_pixel(turn.cx + 22, turn.cy));
    CHECK_FALSE(fb.get_pixel(turn.cx + 24, turn.cy));
    CHECK_FALSE(fb.get_pixel(turn.cx + 7, turn.cy + 1));
    CHECK_FALSE(fb.get_pixel(turn.cx + 7, turn.cy - 1));
    CHECK(fb.get_pixel(turn.cx, turn.cy - 6));      // fin, standing above
    CHECK(fb.get_pixel(turn.cx - 8, turn.cy - 3));  // the tailplane on the fuselage
    CHECK(fb.get_pixel(turn.cx + 8, turn.cy - 3));
    CHECK_FALSE(fb.get_pixel(turn.cx, turn.cy + 6));

    CHECK(fb.get_pixel(turn.cx - 27, turn.cy));  // wings-level marks
    CHECK(fb.get_pixel(turn.cx + 27, turn.cy));
    CHECK(fb.get_pixel(turn.cx - 25, turn.cy + 9));  // standard-rate marks, 20 degrees down
    CHECK(fb.get_pixel(turn.cx + 25, turn.cy + 9));
    CHECK_FALSE(fb.get_pixel(turn.cx, turn.cy - 27));  // and no scale anywhere else
    CHECK_FALSE(fb.get_pixel(turn.cx - 25, turn.cy - 9));
}

// An empty cage is the reading before the sensor has one: a centred ball claims coordination.
TEST_CASE("sixpack: the ball sits between its cage lines, and is absent without a reading") {
    const Tile turn = kTiles[3];
    SixPackSnapshot coordinated = flying();
    coordinated.inclinometer_fitted = true;
    coordinated.lateral_valid = true;
    Glass fb;
    draw_sixpack(fb, coordinated);

    CHECK(fb.get_pixel(turn.cx - 6, turn.cy + 18));  // cage
    CHECK(fb.get_pixel(turn.cx + 6, turn.cy + 18));
    CHECK(fb.get_pixel(turn.cx, turn.cy + 18));  // ball, centred

    SixPackSnapshot skidding = coordinated;
    skidding.lateral_mg = 200;  // full scale, a ball's width and more out
    Glass fs;
    draw_sixpack(fs, skidding);
    CHECK(fs.get_pixel(turn.cx + 16, turn.cy + 18));
    CHECK_FALSE(fs.get_pixel(turn.cx - 2, turn.cy + 18));

    SixPackSnapshot warming = coordinated;
    warming.lateral_valid = false;
    Glass blind;
    draw_sixpack(blind, warming);
    CHECK(blind.get_pixel(turn.cx - 6, turn.cy + 18));
    CHECK_FALSE(blind.get_pixel(turn.cx, turn.cy + 18));

    // A device with no IMU fitted has no inclinometer to draw, not an empty one.
    Glass plain;
    draw_sixpack(plain, flying());
    for (int x = -16; x <= 16; x++)
        for (int y = 13; y <= 23; y++) CHECK_FALSE(plain.get_pixel(turn.cx + x, turn.cy + y));
}

// Standard rate is 3 deg/s, the two-minute turn: at it, the wing is on the mark.
TEST_CASE("sixpack: the turn coordinator puts a standard rate turn on its index marks") {
    const Tile turn = kTiles[3];
    SixPackSnapshot right = flying();  // 3 deg/s
    Glass fb;
    draw_sixpack(fb, right);
    CHECK(fb.get_pixel(turn.cx + 21, turn.cy + 8));
    CHECK_FALSE(fb.get_pixel(turn.cx + 21, turn.cy));

    SixPackSnapshot left = flying();
    left.turn_cdps = -300;
    Glass fl;
    draw_sixpack(fl, left);
    CHECK(fl.get_pixel(turn.cx - 21, turn.cy + 8));

    // Speed does not enter it: a rate gyro's dial reads the same at any airspeed.
    SixPackSnapshot fast = flying();
    fast.speed_kt = 160;
    Glass ff;
    draw_sixpack(ff, fast);
    CHECK(ff.get_pixel(turn.cx + 21, turn.cy + 8));
}

// The reference symbol is read against the horizon behind it: pitch is what it measures.
TEST_CASE("sixpack: the horizon carries two wing bars and a dot, clear of each other") {
    const Tile att = kTiles[1];
    SixPackSnapshot climbing = flying();
    climbing.turn_cdps = 0;
    climbing.vs_fpm = 1000;  // 8 degrees up at 90 kt, so the ground is well below the bars
    Glass fb;
    draw_sixpack(fb, climbing);

    for (int i = 7; i <= 20; i++) {
        CHECK(fb.get_pixel(att.cx - i, att.cy));
        CHECK(fb.get_pixel(att.cx + i, att.cy));
    }
    for (int i = 3; i <= 6; i++) {
        CHECK_FALSE(fb.get_pixel(att.cx - i, att.cy));
        CHECK_FALSE(fb.get_pixel(att.cx + i, att.cy));
    }
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            CHECK(fb.get_pixel(att.cx + dx, att.cy + dy) == (dx * dx + dy * dy < 8));
    CHECK_FALSE(fb.get_pixel(att.cx - 21, att.cy));
    CHECK_FALSE(fb.get_pixel(att.cx + 21, att.cy));
}

// The label names the quantity and the figure carries its unit: the radar keeps the clock.
TEST_CASE("sixpack: the middle dial is titled BATTERY whatever the cell reads") {
    SixPackSnapshot s = flying();
    Glass fb;
    draw_sixpack(fb, s);
    CHECK(title_matches(fb, kTiles[1], "BATTERY"));

    SixPackSnapshot flat = s;
    flat.battery_percent = 4;
    Glass ff;
    draw_sixpack(ff, flat);
    CHECK(title_matches(ff, kTiles[1], "BATTERY"));

    SixPackSnapshot unmeasured = s;
    unmeasured.battery_valid = false;
    Glass fu;
    draw_sixpack(fu, unmeasured);
    CHECK(title_matches(fu, kTiles[1], "BATTERY"));
    CHECK(black_in(fu, kTiles[1], 28) == black_in(fb, kTiles[1], 28));
}

// North is three-six-zero on every other instrument a pilot reads, and 000 is nobody's heading.
TEST_CASE("sixpack: a track due north reads 360") {
    SixPackSnapshot s = flying();
    s.track_deg = 0;
    Glass fb;
    draw_sixpack(fb, s);
    CHECK(value_matches(fb, kTiles[4], "360"));

    s.track_deg = 359;
    Glass f359;
    draw_sixpack(f359, s);
    CHECK(value_matches(f359, kTiles[4], "359"));
}

// The horizon is drawn off a track rate and a climb rate, so nothing else may move it.
TEST_CASE("sixpack: the horizon banks with the turn and pitches with climb") {
    SixPackSnapshot level = flying();
    level.speed_kt = 100;
    level.turn_cdps = 0;
    level.vs_fpm = 0;
    SixPackSnapshot turning = level;
    turning.turn_cdps = 300;  // standard rate at 100 kt is ~15 deg of bank
    SixPackSnapshot climbing = level;
    climbing.vs_fpm = 1000;

    Glass f0, f1, f2;
    draw_sixpack(f0, level);
    draw_sixpack(f1, turning);
    draw_sixpack(f2, climbing);

    const Tile att = kTiles[1];
    CHECK(black_in(f1, att, 28) != black_in(f0, att, 28));
    // Climbing shows more sky: the ground area shrinks.
    CHECK(black_in(f2, att, 28) < black_in(f0, att, 28));

    // The figure the pilot reads above it is none of the horizon's business.
    SixPackSnapshot later = level;
    later.battery_percent = 12;
    Glass f3;
    draw_sixpack(f3, later);
    CHECK(black_in(f3, att, 28) == black_in(f0, att, 28));
    CHECK(value_matches(f3, att, "12%"));
}

TEST_CASE("sixpack: the speed dial rests at the bottom and stands 100 kt straight up") {
    const Tile asi = kTiles[0];
    SixPackSnapshot s = flying();
    s.speed_kt = 100;
    Glass fb;
    draw_sixpack(fb, s);
    CHECK(fb.get_pixel(asi.cx, asi.cy - kFaceR));
    CHECK_FALSE(fb.get_pixel(asi.cx - 2, asi.cy - kFaceR));
    CHECK_FALSE(fb.get_pixel(asi.cx + 2, asi.cy - kFaceR));

    // Stopped on the apron it hangs straight down, and reads through the shading, not under it.
    SixPackSnapshot stopped = s;
    stopped.speed_kt = 0;
    Glass fs;
    draw_sixpack(fs, stopped);
    CHECK(fs.get_pixel(asi.cx, asi.cy + kBandR));
    CHECK_FALSE(fs.get_pixel(asi.cx - 2, asi.cy + kBandR));
    CHECK_FALSE(fs.get_pixel(asi.cx + 2, asi.cy + kBandR));
}

// The arc no aeroplane reaches belongs at the bottom of the glass, not across the top of it.
TEST_CASE("sixpack: the speed dial shades the arc past 180 kt, and nothing below it") {
    const Tile asi = kTiles[0];
    SixPackSnapshot s = flying();  // 90 kt, half the scale, so half way round
    Glass fb;
    draw_sixpack(fb, s);
    CHECK(ink_at(fb, asi, -18));
    CHECK(ink_at(fb, asi, 162, kBandR));
    CHECK_FALSE(ink_at(fb, asi, -160, kBandR));
    CHECK_FALSE(ink_at(fb, asi, -110, kBandR));
    CHECK_FALSE(ink_at(fb, asi, 60, kBandR));
}

TEST_CASE("sixpack: the vertical speed dial stands a 1000 climb up and hangs a 1000 descent down") {
    const Tile vsi = kTiles[5];
    SixPackSnapshot s = flying();

    s.vs_fpm = 1000;
    Glass up;
    draw_sixpack(up, s);
    CHECK(up.get_pixel(vsi.cx, vsi.cy - kFaceR));
    CHECK_FALSE(up.get_pixel(vsi.cx + 3, vsi.cy - kFaceR));
    CHECK_FALSE(up.get_pixel(vsi.cx - 3, vsi.cy - kFaceR));

    s.vs_fpm = -1000;
    Glass down;
    draw_sixpack(down, s);
    CHECK(down.get_pixel(vsi.cx, vsi.cy + kFaceR));
    CHECK_FALSE(down.get_pixel(vsi.cx + 3, vsi.cy + kFaceR));
    CHECK_FALSE(down.get_pixel(vsi.cx - 3, vsi.cy + kFaceR));

    s.vs_fpm = 0;
    Glass level;
    draw_sixpack(level, s);
    CHECK(level.get_pixel(vsi.cx - kFaceR, vsi.cy));
    CHECK_FALSE(level.get_pixel(vsi.cx - kFaceR, vsi.cy + 3));
}

TEST_CASE("sixpack: the vertical speed dial is marked every 500 fpm and shaded past 2000") {
    const Tile vsi = kTiles[5];
    SixPackSnapshot s = flying();
    s.vs_fpm = 1000;
    Glass fb;
    draw_sixpack(fb, s);

    const double marks[] = {-90, -45, 0, 40, 80, -135, 180, 140, 100};
    for (double m : marks) CHECK(ink_at(fb, vsi, m, 29));
    const double gaps[] = {-67.5, 20, -112.5, 160};
    for (double g : gaps) CHECK_FALSE(ink_at(fb, vsi, g, 29));

    // Ten degrees either side of the horizontal is all the arc no rate can reach.
    CHECK(ink_at(fb, vsi, 90, kBandR));
    CHECK_FALSE(ink_at(fb, vsi, -90));

    // The shading is angled by iatan2, the scale by cordic: they used to disagree by 8 degrees.
    CHECK_FALSE(ink_at(fb, vsi, 72, kBandR));
    CHECK_FALSE(ink_at(fb, vsi, 108, kBandR));
}

// A rate of zero is a rate, not a direction: +0 and -0 are both noise on a glance.
TEST_CASE("sixpack: a zero rate carries no sign") {
    SixPackSnapshot s = flying();
    s.turn_cdps = 0;
    s.vs_fpm = 0;
    Glass fb;
    draw_sixpack(fb, s);
    CHECK(value_matches(fb, kTiles[3], "0"));
    CHECK(value_matches(fb, kTiles[5], "0"));

    s.turn_cdps = -200;
    s.vs_fpm = -200;
    Glass signed_fb;
    draw_sixpack(signed_fb, s);
    CHECK(value_matches(signed_fb, kTiles[3], "-2"));
    CHECK(value_matches(signed_fb, kTiles[5], "-200"));

    s.vs_fpm = -4;
    Glass jitter;
    draw_sixpack(jitter, s);
    CHECK(value_matches(jitter, kTiles[5], "0"));
}

TEST_CASE("sixpack: the vertical speed figure moves in tens of feet a minute") {
    SixPackSnapshot s = flying();
    s.vs_fpm = -243;
    Glass fb;
    draw_sixpack(fb, s);
    CHECK(value_matches(fb, kTiles[5], "-240"));
}
