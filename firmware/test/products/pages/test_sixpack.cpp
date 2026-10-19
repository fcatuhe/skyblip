// Read against a pilot's habit, not pixels: two dials on one value looks right and lies.
#include <cmath>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/sixpack.h"

using namespace skyblip::go;

namespace {
// The six dial centres, in the order the panel draws them.
struct Tile {
    int centre_x, centre_y;
};
const Tile kTiles[6] = {{34, 67}, {100, 67}, {166, 67}, {34, 133}, {100, 133}, {166, 133}};

// The number sits outside the glass: above the top row, below the bottom one.
const int kValueScale = 2;
int value_y(const Tile& tile) {
    return tile.centre_y < 100 ? tile.centre_y - 31 - 7 - 7 * kValueScale : tile.centre_y + 31 + 7;
}

// Ink at the point `deg` clockwise from the top: only the needle and the shading reach kFaceR.
const int kFaceRadius = 20;
const int kBandRadius = 27;

bool ink_at(const Glass& canvas, Tile tile, double degrees, int radius = kFaceRadius) {
    const double radians = degrees * 3.14159265358979 / 180.0;
    const int x = tile.centre_x + static_cast<int>(std::lround(radius * std::sin(radians)));
    const int y = tile.centre_y - static_cast<int>(std::lround(radius * std::cos(radians)));
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (canvas.get_pixel(x + dx, y + dy)) return true;
    return false;
}

bool title_matches(const Glass& canvas, Tile tile, const char* text) {
    const int y0 =
        tile.centre_y < 100 ? value_y(tile) - 4 - 7 : value_y(tile) + 7 * kValueScale + 4;
    Glass expected;
    expected.clear(true);
    int count = 0;
    while (text[count]) count++;
    expected.draw_text(tile.centre_x - (count * 6) / 2, y0, text, true, 1);
    for (int y = y0; y < y0 + 7; y++)
        for (int x = tile.centre_x - 32; x <= tile.centre_x + 32; x++)
            if (canvas.get_pixel(x, y) != expected.get_pixel(x, y)) return false;
    return true;
}

int black_in(const Glass& canvas, Tile tile, int radius) {
    int count = 0;
    for (int y = tile.centre_y - radius; y <= tile.centre_y + radius; y++)
        for (int x = tile.centre_x - radius; x <= tile.centre_x + radius; x++)
            if (canvas.get_pixel(x, y)) count++;
    return count;
}

// The number under a dial, matched as the ink the page would draw for it.
bool value_matches(const Glass& canvas, Tile tile, const char* text) {
    const int y0 = value_y(tile);
    Glass expected;
    expected.clear(true);
    int count = 0;
    while (text[count]) count++;
    expected.draw_text(tile.centre_x - (count * 6 * kValueScale) / 2, y0, text, true, kValueScale);
    for (int y = y0; y < y0 + 7 * kValueScale; y++)
        for (int x = tile.centre_x - 32; x <= tile.centre_x + 32; x++)
            if (canvas.get_pixel(x, y) != expected.get_pixel(x, y)) return false;
    return true;
}

SixPackSnapshot flying() {
    SixPackSnapshot snapshot;
    snapshot.fix_valid = true;
    snapshot.units = skyblip::go::Units::Nautical;
    snapshot.speed_kt = 90;
    snapshot.altitude_ft = 3450;
    snapshot.climb_fpm = 500;
    snapshot.climb_valid = true;
    snapshot.track_deg = 270;
    snapshot.turn_cdps = 300;
    snapshot.battery_percent = 64;
    snapshot.battery_valid = true;
    return snapshot;
}
}  // namespace

TEST_CASE("sixpack page: six dials are drawn, each with its own needle") {
    const SixPackSnapshot snapshot = flying();

    Glass canvas;
    draw_sixpack(canvas, snapshot);
    for (const Tile& tile : kTiles) CHECK(black_in(canvas, tile, 30) > 60);
}

// An instrument with no needle reads as a broken instrument, so a lost fix parks them at rest.
TEST_CASE("sixpack page: without a fix the needles park at zero and the numbers withhold") {
    SixPackSnapshot none;
    Glass canvas;
    draw_sixpack(canvas, none);

    for (const Tile& tile : kTiles) CHECK(black_in(canvas, tile, 30) > 60);

    // Zero speed hangs its needle down, zero altitude stands its own up.
    CHECK(canvas.get_pixel(kTiles[0].centre_x, kTiles[0].centre_y + kFaceRadius));
    CHECK_FALSE(canvas.get_pixel(kTiles[0].centre_x - 1, kTiles[0].centre_y + kFaceRadius));
    CHECK(canvas.get_pixel(kTiles[2].centre_x, kTiles[2].centre_y - kFaceRadius));
    CHECK_FALSE(canvas.get_pixel(kTiles[2].centre_x - 2, kTiles[2].centre_y - kFaceRadius));
    CHECK(ink_at(canvas, kTiles[5], -90));  // a level vario points at its zero
    CHECK(ink_at(canvas, kTiles[3], 90));   // and the turn coordinator's wings are level
    CHECK(ink_at(canvas, kTiles[3], -90));

    for (const Tile& tile : kTiles)
        if (&tile != &kTiles[1]) CHECK(value_matches(canvas, tile, "---"));
    CHECK(value_matches(canvas, kTiles[1], "--%"));
}

// The barometer measures a climb of its own, so the vario is not a GNSS instrument.
TEST_CASE("sixpack page: a baro vertical speed reads on without a fix") {
    SixPackSnapshot searching;
    searching.climb_fpm = -1000;
    searching.climb_valid = true;
    Glass canvas;
    draw_sixpack(canvas, searching);

    CHECK(value_matches(canvas, kTiles[5], "-1000"));
    CHECK(canvas.get_pixel(kTiles[5].centre_x, kTiles[5].centre_y + kFaceRadius));
    CHECK(value_matches(canvas, kTiles[0], "---"));
    CHECK(value_matches(canvas, kTiles[2], "---"));

    // No barometer and no fix is no vertical speed, needle at rest.
    SixPackSnapshot blind = searching;
    blind.climb_valid = false;
    Glass none;
    draw_sixpack(none, blind);
    CHECK(value_matches(none, kTiles[5], "---"));
    CHECK(none.get_pixel(kTiles[5].centre_x - kFaceRadius, kTiles[5].centre_y));
}

// Five dials read the receiver and one reads the cell, and a lost fix is the receiver's.
TEST_CASE("sixpack page: a fix lost in flight withholds the dials and leaves the cell alone") {
    SixPackSnapshot lost = flying();
    lost.fix_valid = false;
    Glass canvas;
    draw_sixpack(canvas, lost);
    CHECK(value_matches(canvas, kTiles[1], "64%"));
    CHECK(value_matches(canvas, kTiles[0], "---"));
}

TEST_CASE("sixpack page: needles move with the data they show") {
    SixPackSnapshot before;
    before.fix_valid = true;
    before.units = skyblip::go::Units::Nautical;
    before.speed_kt = 40;
    before.altitude_ft = 1200;
    before.track_deg = 0;
    SixPackSnapshot after = before;
    after.speed_kt = 140;
    after.altitude_ft = 1900;
    after.track_deg = 180;

    Glass before_glass, after_glass;
    draw_sixpack(before_glass, before);
    draw_sixpack(after_glass, after);

    bool differs[6] = {};
    for (int i = 0; i < 6; i++) {
        for (int y = kTiles[i].centre_y - 28; y <= kTiles[i].centre_y + 28 && !differs[i]; y++)
            for (int x = kTiles[i].centre_x - 28; x <= kTiles[i].centre_x + 28; x++)
                if (before_glass.get_pixel(x, y) != after_glass.get_pixel(x, y)) {
                    differs[i] = true;
                    break;
                }
    }
    CHECK(differs[0]);  // airspeed
    CHECK(differs[2]);  // altimeter
    CHECK(differs[4]);  // heading
}

TEST_CASE("sixpack page: the altimeter reads like a three-pointer, the card like a compass") {
    SixPackSnapshot snapshot;
    snapshot.fix_valid = true;
    snapshot.units = skyblip::go::Units::Nautical;
    snapshot.altitude_ft = 2500;  // long hand at 500 ft (down), short hand at 2.5/10 (right)
    Glass canvas;
    draw_sixpack(canvas, snapshot);

    const Tile altitude = kTiles[2];
    CHECK(canvas.get_pixel(altitude.centre_x,
                           altitude.centre_y + 20));  // hundreds hand, straight down
    CHECK(canvas.get_pixel(altitude.centre_x + 12,
                           altitude.centre_y));  // thousands hand, quarter turn
    CHECK_FALSE(
        canvas.get_pixel(altitude.centre_x + 20, altitude.centre_y));  // and it is the SHORT one
    CHECK_FALSE(canvas.get_pixel(altitude.centre_x, altitude.centre_y - 20));

    // The compass card turns with the track: north swings to the right when the
    // aircraft flies west.
    SixPackSnapshot west = snapshot;
    west.track_deg = 270;
    Glass west_glass;
    draw_sixpack(west_glass, west);
    const Tile heading = kTiles[4];
    int right = 0, left = 0;
    for (int y = heading.centre_y - 8; y <= heading.centre_y + 8; y++)
        for (int offset = 12; offset <= 24; offset++) {
            if (west_glass.get_pixel(heading.centre_x + offset, y)) right++;
            if (west_glass.get_pixel(heading.centre_x - offset, y)) left++;
        }
    CHECK(right > left);
}

// B4. A dial has one needle and one number, so the km/h pilot has to ask for it.
TEST_CASE("sixpack page: the unit setting decides the speed dial, and only the speed dial") {
    SixPackSnapshot nautical;
    nautical.fix_valid = true;
    nautical.units = skyblip::go::Units::Nautical;
    nautical.speed_kt = 90;  // 166.7 km/h
    nautical.altitude_ft = 3450;
    nautical.climb_fpm = 500;
    nautical.climb_valid = true;
    nautical.track_deg = 7;
    SixPackSnapshot metric = nautical;
    metric.units = skyblip::go::Units::Metric;

    Glass nautical_glass, metric_glass;
    draw_sixpack(nautical_glass, nautical);
    draw_sixpack(metric_glass, metric);

    // The number under a dial is the converted one, drawn where the page draws
    // it: 90 kt reads 167, and it is not the same ink as 90.
    CHECK(value_matches(nautical_glass, kTiles[0], "90"));
    CHECK(value_matches(metric_glass, kTiles[0], "167"));

    const Glass* faces[2] = {&nautical_glass, &metric_glass};
    for (const Glass* canvas : faces) {
        CHECK(value_matches(*canvas, kTiles[2], "3450"));
        CHECK(value_matches(*canvas, kTiles[5], "+500"));
        CHECK(value_matches(*canvas, kTiles[4], "007"));
    }
    for (int i = 1; i < 6; i++)
        CHECK(black_in(metric_glass, kTiles[i], 30) == black_in(nautical_glass, kTiles[i], 30));
}

TEST_CASE("sixpack page: the middle number is the state of charge, with the unit on the figure") {
    SixPackSnapshot snapshot = flying();
    Glass canvas;
    draw_sixpack(canvas, snapshot);
    CHECK(value_matches(canvas, kTiles[1], "64%"));

    SixPackSnapshot full = snapshot;
    full.battery_percent = 100;
    Glass full_glass;
    draw_sixpack(full_glass, full);
    CHECK(value_matches(full_glass, kTiles[1], "100%"));
}

// A unit with no divider fitted draws the shape of the reading, never a zero that reads as flat.
TEST_CASE("sixpack page: a cell nobody measured withholds the figure and keeps the sign") {
    SixPackSnapshot snapshot = flying();
    snapshot.battery_valid = false;
    Glass canvas;
    draw_sixpack(canvas, snapshot);
    CHECK(value_matches(canvas, kTiles[1], "--%"));
}

// On the cable the divider reads the USB rail, which drawn as a percentage reads as the cell.
TEST_CASE(
    "sixpack page: on the cable the middle dial reads the charge rail in volts, not the cell") {
    SixPackSnapshot cable = flying();
    cable.battery_valid = false;
    cable.charging = true;
    cable.battery_mv = 4802;
    Glass canvas;
    draw_sixpack(canvas, cable);
    CHECK(title_matches(canvas, kTiles[1], "CHARGE V"));
    CHECK(value_matches(canvas, kTiles[1], "4.80"));

    // Hundredths are rounded, not truncated: 4755 mV is 4.755 V, which reads 4.76.
    cable.battery_mv = 4717;
    Glass reads_4_72;
    draw_sixpack(reads_4_72, cable);
    CHECK(value_matches(reads_4_72, kTiles[1], "4.72"));
    cable.battery_mv = 4755;
    Glass reads_4_76;
    draw_sixpack(reads_4_76, cable);
    CHECK(value_matches(reads_4_76, kTiles[1], "4.76"));

    // The horizon is the same instrument on the cable as off it.
    Glass off;
    draw_sixpack(off, flying());
    CHECK(black_in(canvas, kTiles[1], 28) == black_in(off, kTiles[1], 28));
}

// Two dashes and a hub read as a needle: what leans here is an aeroplane seen from behind.
TEST_CASE("sixpack page: the turn coordinator flies an aeroplane between four marks") {
    const Tile turn = kTiles[3];
    SixPackSnapshot level = flying();
    level.turn_cdps = 0;
    Glass canvas;
    draw_sixpack(canvas, level);

    CHECK(canvas.get_pixel(turn.centre_x - 22, turn.centre_y));  // wings
    CHECK(canvas.get_pixel(turn.centre_x + 22, turn.centre_y));
    CHECK_FALSE(canvas.get_pixel(turn.centre_x + 24, turn.centre_y));
    CHECK_FALSE(canvas.get_pixel(turn.centre_x + 7, turn.centre_y + 1));
    CHECK_FALSE(canvas.get_pixel(turn.centre_x + 7, turn.centre_y - 1));
    CHECK(canvas.get_pixel(turn.centre_x, turn.centre_y - 6));      // fin, standing above
    CHECK(canvas.get_pixel(turn.centre_x - 8, turn.centre_y - 3));  // the tailplane on the fuselage
    CHECK(canvas.get_pixel(turn.centre_x + 8, turn.centre_y - 3));
    CHECK_FALSE(canvas.get_pixel(turn.centre_x, turn.centre_y + 6));

    CHECK(canvas.get_pixel(turn.centre_x - 27, turn.centre_y));  // wings-level marks
    CHECK(canvas.get_pixel(turn.centre_x + 27, turn.centre_y));
    CHECK(canvas.get_pixel(turn.centre_x - 25,
                           turn.centre_y + 9));  // standard-rate marks, 20 degrees down
    CHECK(canvas.get_pixel(turn.centre_x + 25, turn.centre_y + 9));
    CHECK_FALSE(canvas.get_pixel(turn.centre_x, turn.centre_y - 27));  // and no scale anywhere else
    CHECK_FALSE(canvas.get_pixel(turn.centre_x - 25, turn.centre_y - 9));
}

// An empty cage is the reading before the sensor has one: a centred ball claims coordination.
TEST_CASE("sixpack page: the ball sits between its cage lines, and is absent without a reading") {
    const Tile turn = kTiles[3];
    SixPackSnapshot coordinated = flying();
    coordinated.inclinometer_fitted = true;
    coordinated.lateral_valid = true;
    Glass canvas;
    draw_sixpack(canvas, coordinated);

    CHECK(canvas.get_pixel(turn.centre_x - 6, turn.centre_y + 18));  // cage
    CHECK(canvas.get_pixel(turn.centre_x + 6, turn.centre_y + 18));
    CHECK(canvas.get_pixel(turn.centre_x, turn.centre_y + 18));  // ball, centred

    SixPackSnapshot skidding = coordinated;
    skidding.lateral_mg = 200;  // full scale, a ball's width and more out
    Glass skidding_glass;
    draw_sixpack(skidding_glass, skidding);
    CHECK(skidding_glass.get_pixel(turn.centre_x + 16, turn.centre_y + 18));
    CHECK_FALSE(skidding_glass.get_pixel(turn.centre_x - 2, turn.centre_y + 18));

    SixPackSnapshot warming = coordinated;
    warming.lateral_valid = false;
    Glass blind;
    draw_sixpack(blind, warming);
    CHECK(blind.get_pixel(turn.centre_x - 6, turn.centre_y + 18));
    CHECK_FALSE(blind.get_pixel(turn.centre_x, turn.centre_y + 18));

    // A device with no IMU fitted has no inclinometer to draw, not an empty one.
    Glass plain;
    draw_sixpack(plain, flying());
    for (int x = -16; x <= 16; x++)
        for (int y = 13; y <= 23; y++)
            CHECK_FALSE(plain.get_pixel(turn.centre_x + x, turn.centre_y + y));
}

// Standard rate is 3 deg/s, the two-minute turn: at it, the wing is on the mark.
TEST_CASE("sixpack page: the turn coordinator puts a standard rate turn on its index marks") {
    const Tile turn = kTiles[3];
    SixPackSnapshot right = flying();  // 3 deg/s
    Glass canvas;
    draw_sixpack(canvas, right);
    CHECK(canvas.get_pixel(turn.centre_x + 21, turn.centre_y + 8));
    CHECK_FALSE(canvas.get_pixel(turn.centre_x + 21, turn.centre_y));

    SixPackSnapshot left = flying();
    left.turn_cdps = -300;
    Glass left_glass;
    draw_sixpack(left_glass, left);
    CHECK(left_glass.get_pixel(turn.centre_x - 21, turn.centre_y + 8));

    // Speed does not enter it: a rate gyro's dial reads the same at any airspeed.
    SixPackSnapshot fast = flying();
    fast.speed_kt = 160;
    Glass full_glass;
    draw_sixpack(full_glass, fast);
    CHECK(full_glass.get_pixel(turn.centre_x + 21, turn.centre_y + 8));
}

// The reference symbol is read against the horizon behind it: pitch is what it measures.
TEST_CASE("sixpack page: the horizon carries two wing bars and a dot, clear of each other") {
    const Tile attitude = kTiles[1];
    SixPackSnapshot climbing = flying();
    climbing.turn_cdps = 0;
    climbing.climb_fpm = 1000;  // 8 degrees up at 90 kt, so the ground is well below the bars
    Glass canvas;
    draw_sixpack(canvas, climbing);

    for (int i = 7; i <= 20; i++) {
        CHECK(canvas.get_pixel(attitude.centre_x - i, attitude.centre_y));
        CHECK(canvas.get_pixel(attitude.centre_x + i, attitude.centre_y));
    }
    for (int i = 3; i <= 6; i++) {
        CHECK_FALSE(canvas.get_pixel(attitude.centre_x - i, attitude.centre_y));
        CHECK_FALSE(canvas.get_pixel(attitude.centre_x + i, attitude.centre_y));
    }
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            CHECK(canvas.get_pixel(attitude.centre_x + dx, attitude.centre_y + dy) ==
                  (dx * dx + dy * dy < 8));
    CHECK_FALSE(canvas.get_pixel(attitude.centre_x - 21, attitude.centre_y));
    CHECK_FALSE(canvas.get_pixel(attitude.centre_x + 21, attitude.centre_y));
}

// The label names the quantity and the figure carries its unit: the radar keeps the clock.
TEST_CASE("sixpack page: the middle dial is titled BATTERY whatever the cell reads") {
    SixPackSnapshot snapshot = flying();
    Glass canvas;
    draw_sixpack(canvas, snapshot);
    CHECK(title_matches(canvas, kTiles[1], "BATTERY"));

    SixPackSnapshot flat = snapshot;
    flat.battery_percent = 4;
    Glass full_glass;
    draw_sixpack(full_glass, flat);
    CHECK(title_matches(full_glass, kTiles[1], "BATTERY"));

    SixPackSnapshot unmeasured = snapshot;
    unmeasured.battery_valid = false;
    Glass unmeasured_glass;
    draw_sixpack(unmeasured_glass, unmeasured);
    CHECK(title_matches(unmeasured_glass, kTiles[1], "BATTERY"));
    CHECK(black_in(unmeasured_glass, kTiles[1], 28) == black_in(canvas, kTiles[1], 28));
}

// North is three-six-zero on every other instrument a pilot reads, and 000 is nobody's heading.
TEST_CASE("sixpack page: a track due north reads 360") {
    SixPackSnapshot snapshot = flying();
    snapshot.track_deg = 0;
    Glass canvas;
    draw_sixpack(canvas, snapshot);
    CHECK(value_matches(canvas, kTiles[4], "360"));

    snapshot.track_deg = 359;
    Glass f359;
    draw_sixpack(f359, snapshot);
    CHECK(value_matches(f359, kTiles[4], "359"));
}

// The horizon is drawn off a track rate and a climb rate, so nothing else may move it.
TEST_CASE("sixpack page: the horizon banks with the turn and pitches with climb") {
    SixPackSnapshot level = flying();
    level.speed_kt = 100;
    level.turn_cdps = 0;
    level.climb_fpm = 0;
    SixPackSnapshot turning = level;
    turning.turn_cdps = 300;  // standard rate at 100 kt is ~15 deg of bank
    SixPackSnapshot climbing = level;
    climbing.climb_fpm = 1000;

    Glass level_glass, turning_glass, climbing_glass;
    draw_sixpack(level_glass, level);
    draw_sixpack(turning_glass, turning);
    draw_sixpack(climbing_glass, climbing);

    const Tile attitude = kTiles[1];
    CHECK(black_in(turning_glass, attitude, 28) != black_in(level_glass, attitude, 28));
    // Climbing shows more sky: the ground area shrinks.
    CHECK(black_in(climbing_glass, attitude, 28) < black_in(level_glass, attitude, 28));

    // The figure the pilot reads above it is none of the horizon's business.
    SixPackSnapshot later = level;
    later.battery_percent = 12;
    Glass later_glass;
    draw_sixpack(later_glass, later);
    CHECK(black_in(later_glass, attitude, 28) == black_in(level_glass, attitude, 28));
    CHECK(value_matches(later_glass, attitude, "12%"));
}

TEST_CASE("sixpack page: the speed dial rests at the bottom and stands 100 kt straight up") {
    const Tile asi = kTiles[0];
    SixPackSnapshot snapshot = flying();
    snapshot.speed_kt = 100;
    Glass canvas;
    draw_sixpack(canvas, snapshot);
    CHECK(canvas.get_pixel(asi.centre_x, asi.centre_y - kFaceRadius));
    CHECK_FALSE(canvas.get_pixel(asi.centre_x - 2, asi.centre_y - kFaceRadius));
    CHECK_FALSE(canvas.get_pixel(asi.centre_x + 2, asi.centre_y - kFaceRadius));

    // Stopped on the apron it hangs straight down, and reads through the shading, not under it.
    SixPackSnapshot stopped = snapshot;
    stopped.speed_kt = 0;
    Glass skidding_glass;
    draw_sixpack(skidding_glass, stopped);
    CHECK(skidding_glass.get_pixel(asi.centre_x, asi.centre_y + kBandRadius));
    CHECK_FALSE(skidding_glass.get_pixel(asi.centre_x - 2, asi.centre_y + kBandRadius));
    CHECK_FALSE(skidding_glass.get_pixel(asi.centre_x + 2, asi.centre_y + kBandRadius));
}

// The arc no aeroplane reaches belongs at the bottom of the glass, not across the top of it.
TEST_CASE("sixpack page: the speed dial shades the arc past 180 kt, and nothing below it") {
    const Tile asi = kTiles[0];
    SixPackSnapshot snapshot = flying();  // 90 kt, half the scale, so half way round
    Glass canvas;
    draw_sixpack(canvas, snapshot);
    CHECK(ink_at(canvas, asi, -18));
    CHECK(ink_at(canvas, asi, 162, kBandRadius));
    CHECK_FALSE(ink_at(canvas, asi, -160, kBandRadius));
    CHECK_FALSE(ink_at(canvas, asi, -110, kBandRadius));
    CHECK_FALSE(ink_at(canvas, asi, 60, kBandRadius));
}

TEST_CASE(
    "sixpack page: the vertical speed dial stands a 1000 climb up and hangs a 1000 descent down") {
    const Tile vsi = kTiles[5];
    SixPackSnapshot snapshot = flying();

    snapshot.climb_fpm = 1000;
    Glass up;
    draw_sixpack(up, snapshot);
    CHECK(up.get_pixel(vsi.centre_x, vsi.centre_y - kFaceRadius));
    CHECK_FALSE(up.get_pixel(vsi.centre_x + 3, vsi.centre_y - kFaceRadius));
    CHECK_FALSE(up.get_pixel(vsi.centre_x - 3, vsi.centre_y - kFaceRadius));

    snapshot.climb_fpm = -1000;
    Glass down;
    draw_sixpack(down, snapshot);
    CHECK(down.get_pixel(vsi.centre_x, vsi.centre_y + kFaceRadius));
    CHECK_FALSE(down.get_pixel(vsi.centre_x + 3, vsi.centre_y + kFaceRadius));
    CHECK_FALSE(down.get_pixel(vsi.centre_x - 3, vsi.centre_y + kFaceRadius));

    snapshot.climb_fpm = 0;
    Glass level;
    draw_sixpack(level, snapshot);
    CHECK(level.get_pixel(vsi.centre_x - kFaceRadius, vsi.centre_y));
    CHECK_FALSE(level.get_pixel(vsi.centre_x - kFaceRadius, vsi.centre_y + 3));
}

TEST_CASE("sixpack page: the vertical speed dial is marked every 500 fpm and shaded past 2000") {
    const Tile vsi = kTiles[5];
    SixPackSnapshot snapshot = flying();
    snapshot.climb_fpm = 1000;
    Glass canvas;
    draw_sixpack(canvas, snapshot);

    const double marks[] = {-90, -45, 0, 40, 80, -135, 180, 140, 100};
    for (double mark : marks) CHECK(ink_at(canvas, vsi, mark, 29));
    const double gaps[] = {-67.5, 20, -112.5, 160};
    for (double gap : gaps) CHECK_FALSE(ink_at(canvas, vsi, gap, 29));

    // Ten degrees either side of the horizontal is all the arc no rate can reach.
    CHECK(ink_at(canvas, vsi, 90, kBandRadius));
    CHECK_FALSE(ink_at(canvas, vsi, -90));

    // The shading is angled by arctangent, the scale by cordic: they used to disagree by 8 degrees.
    CHECK_FALSE(ink_at(canvas, vsi, 72, kBandRadius));
    CHECK_FALSE(ink_at(canvas, vsi, 108, kBandRadius));
}

// A rate of zero is a rate, not a direction: +0 and -0 are both noise on a glance.
TEST_CASE("sixpack page: a zero rate carries no sign") {
    SixPackSnapshot snapshot = flying();
    snapshot.turn_cdps = 0;
    snapshot.climb_fpm = 0;
    Glass canvas;
    draw_sixpack(canvas, snapshot);
    CHECK(value_matches(canvas, kTiles[3], "0"));
    CHECK(value_matches(canvas, kTiles[5], "0"));

    snapshot.turn_cdps = -200;
    snapshot.climb_fpm = -200;
    Glass signed_glass;
    draw_sixpack(signed_glass, snapshot);
    CHECK(value_matches(signed_glass, kTiles[3], "-2"));
    CHECK(value_matches(signed_glass, kTiles[5], "-200"));

    snapshot.climb_fpm = -4;
    Glass jitter;
    draw_sixpack(jitter, snapshot);
    CHECK(value_matches(jitter, kTiles[5], "0"));
}

TEST_CASE("sixpack page: the vertical speed figure moves in tens of feet a minute") {
    SixPackSnapshot snapshot = flying();
    snapshot.climb_fpm = -243;
    Glass canvas;
    draw_sixpack(canvas, snapshot);
    CHECK(value_matches(canvas, kTiles[5], "-240"));
}
