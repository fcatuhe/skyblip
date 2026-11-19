// What the radar says of a paired Bravo: a framed B by the count while live, else BRAVO and why.
#include "core/comms/peer.h"
#include "core/gnss/acquisition.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/radar.h"
#include "test/support/glass_text.h"
#include "test/support/radar_rig.h"

using namespace skyblip::go;
using skyblip::reads_in;
using skyblip::comms::BravoState;

TEST_CASE("radar page: a live Bravo is a B by the traffic count, and the corner stays empty") {
    for (const RadarPlot plot : {RadarPlot::ToScale, RadarPlot::All}) {
        RadarSnapshot live = flying(0);
        live.plot = plot;
        live.bravo = BravoState::Live;
        live.bravo_heard = true;
        const Glass canvas = radar(live);
        CHECK(marks_bravo_live(canvas));
        CHECK_FALSE(corner_reads(canvas, "B"));
        CHECK_FALSE(reads_in(canvas, "BRAVO", 0, 0, 200, 200));
    }

    RadarSnapshot unpaired = flying(0);
    CHECK_FALSE(marks_bravo_live(radar(unpaired)));
    CHECK_FALSE(reads_in(radar(unpaired), "BRAVO", 0, 0, 200, 200));
}

// The B is drawn with no patch cleared under it, so it has to land on empty glass.
TEST_CASE("radar page: a live Bravo's B stands over TRAFFIC, or over the digits beside TFC") {
    struct Case {
        RadarPlot plot;
        int heard;
        const char* word;
        bool over_digits;
    };
    for (const Case each :
         {Case{RadarPlot::ToScale, 3, "TRAFFIC", false}, Case{RadarPlot::ToScale, 12, "TFC", true},
          Case{RadarPlot::All, 3, "TRAFFIC", false}, Case{RadarPlot::All, 12, "TRAFFIC", false}}) {
        CAPTURE(each.heard);
        CAPTURE(each.word);
        RadarSnapshot unpaired = flying(0);
        unpaired.plot = each.plot;
        unpaired.heard_count = each.heard;
        RadarSnapshot live = unpaired;
        live.bravo = BravoState::Live;
        live.bravo_heard = true;
        const Glass bare = radar(unpaired), marked = radar(live);

        const int digits_x = each.heard > 9 ? 163 : 181;
        const int footer_y = 189, digits_top = 175;
        CHECK(reads_in(marked, each.word, 100, footer_y, digits_x, kGlassHeight));
        if (each.over_digits)
            CHECK(reads_in(marked, "B", digits_x, 150, kGlassWidth, digits_top));
        else
            CHECK(reads_in(marked, "B", digits_x - 16, 165, digits_x, footer_y));

        int left = kGlassWidth, top = kGlassHeight, right = -1, bottom = -1;
        for (int y = 0; y < kGlassHeight; y++)
            for (int x = 0; x < kGlassWidth; x++)
                if (bare.get_pixel(x, y) != marked.get_pixel(x, y)) {
                    left = x < left ? x : left;
                    right = x > right ? x : right;
                    top = y < top ? y : top;
                    bottom = y > bottom ? y : bottom;
                }
        CHECK(right - left + 1 == 11);
        CHECK(bottom - top + 1 == 13);
        int under = 0;
        for (int y = top - 1; y <= bottom + 1; y++)
            for (int x = left - 1; x <= right + 1; x++) under += bare.get_pixel(x, y) ? 1 : 0;
        CHECK(under == 0);
    }
}

// White while Bravo comes up by itself, reversed once the pilot counts on it.
TEST_CASE("radar page: a Bravo not live says BRAVO and why in the corner, reversed when lost" *
          doctest::test_suite("main")) {
    struct Case {
        BravoState state;
        bool heard;
        const char* why;
        bool lost;
    };
    for (const Case each : {Case{BravoState::Waiting, false, "NO LINK", false},
                            Case{BravoState::Waiting, true, "MOVE OUT", false},
                            Case{BravoState::LinkDown, false, "NO LINK", true},
                            Case{BravoState::NoUtc, true, "MOVE OUT", true},
                            Case{BravoState::ReceiverDown, true, "RX DOWN", true}}) {
        CAPTURE(each.why);
        CAPTURE(each.lost);
        for (const RadarPlot plot : {RadarPlot::ToScale, RadarPlot::All}) {
            RadarSnapshot snapshot = flying(0);
            snapshot.plot = plot;
            snapshot.bravo = each.state;
            snapshot.bravo_heard = each.heard;
            const Glass canvas = radar(snapshot);
            CHECK(corner_reads(canvas, "BRAVO", !each.lost));
            CHECK(corner_reads(canvas, each.why, !each.lost));
            CHECK_FALSE(corner_reads(canvas, "BRAVO", each.lost));
            CHECK_FALSE(marks_bravo_live(canvas));
        }
    }
}

TEST_CASE("radar page: the ring speaks for the Go alone, whatever Bravo says") {
    RadarSnapshot parked = flying(0);
    parked.flight_running = false;
    parked.bravo = BravoState::LinkDown;
    const Glass ground = radar(parked);
    CHECK(reads_in(ground, "GROUND", 20, 110, 180, 165, 2));
    CHECK_FALSE(reads_in(ground, "BRAVO", 0, 30, 200, 200, 2));

    RadarSnapshot blind = parked;
    blind.fix_valid = false;
    blind.stage = skyblip::gnss::Stage::Blind;
    blind.bravo = BravoState::Waiting;
    const Glass searching = radar(blind);
    CHECK(reads_in(searching, "NO FIX", 40, 120, 160, 160, 2));
    CHECK(reads_in(searching, "MOVE OUT", 20, 140, 180, 175));
    CHECK(corner_reads(searching, "NO LINK"));
}

// Bravo's lamp winks red from its knee, and a Bravo in a flight bag shows it to nobody.
TEST_CASE("radar page: a live Bravo on a low cell says BRAVO over BAT and its charge") {
    for (const RadarPlot plot : {RadarPlot::ToScale, RadarPlot::All}) {
        RadarSnapshot low = flying(0);
        low.plot = plot;
        low.bravo = BravoState::Live;
        low.bravo_battery_low = true;
        low.bravo_battery_percent = 9;
        const Glass canvas = radar(low);
        CHECK(corner_reads(canvas, "BRAVO"));
        CHECK(corner_reads(canvas, "BAT 9%"));
        CHECK(marks_bravo_live(canvas));
    }

    RadarSnapshot unread = flying(0);
    unread.bravo = BravoState::Live;
    unread.bravo_battery_low = true;
    CHECK(corner_reads(radar(unread), "BAT LOW"));

    RadarSnapshot both = unread;
    both.battery_low = true;
    both.battery_percent = 4;
    const Glass warned = radar(both);
    CHECK(reads_in(warned, "BAT 4%", 40, 120, 160, 160, 2));
    CHECK(corner_reads(warned, "BAT LOW"));
}

// The block is reversed after the plot, so an aircraft over it is not lost in the black.
TEST_CASE("radar page: traffic over a lost Bravo's corner still shows, white on black") {
    RadarTarget corner_target[1] = {{7085, 6040, 0, skyblip::traffic::Level::None}};
    RadarSnapshot lost = flying(0);
    lost.bravo = BravoState::LinkDown;
    const Glass empty = radar(lost);
    lost.target_count = 1;
    lost.targets = corner_target;
    const Glass busy = radar(lost);
    int differ = 0;
    for (int y = 2; y < 22; y++)
        for (int x = 150; x < 198; x++)
            if (empty.get_pixel(x, y) != busy.get_pixel(x, y)) differ++;
    CHECK(differ > 10);
}
