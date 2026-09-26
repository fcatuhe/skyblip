// The radar's frame: the ring, the plot's rotation, the footer and the words in the ring. On a
// 200-pixel span there is no centre pixel, so own-ship sits on the 99|100 boundary and every
// ring and bearing is measured from there. Half a pixel of drift is half a pixel of parallax on
// every target.
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
    landed.in_flight = false;
    landed.flight_seconds = 42 * 60 + 37;
    const Glass fb = radar(landed);
    CHECK(reads_in(fb, "0:42", 0, 176, 60, 198, 2));
    CHECK(reads_in(fb, "37", 45, 183, 80, 198));

    RadarSnapshot airborne_again = landed;
    airborne_again.in_flight = true;
    CHECK_FALSE(reads_in(radar(airborne_again), "37", 0, 170, 80, 199));

    RadarSnapshot never_flown;
    CHECK_FALSE(reads_in(radar(never_flown), "00", 0, 170, 80, 199));
}

// The seconds are half the height of the minutes and clear only their own row of the ring.
TEST_CASE("radar: the seconds take no more ring than they cover") {
    RadarSnapshot landed = flying(0);
    landed.in_flight = false;
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
    searching.in_flight = true;  // stale from the last flight: no fix outranks it
    const Glass no_fix = radar(searching);
    CHECK(reads_in(no_fix, "NO FIX", 40, 120, 160, 160, 2));
    CHECK_FALSE(reads_in(no_fix, "NO FIX", 0, 160, 60, 199));
    CHECK_FALSE(reads_in(no_fix, "FLIGHT", 0, 160, 60, 199));

    RadarSnapshot parked = flying(0);
    parked.in_flight = false;
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
    parked.in_flight = false;
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
    parked.in_flight = false;
    parked.n_targets = 1;
    parked.targets = behind;

    CHECK(reads_in(radar(parked), "GROUND", 40, 120, 160, 160, 2));
}
