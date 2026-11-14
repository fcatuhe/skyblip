// What the radar says of a paired Bravo: its mark while live, NO BRAVO and why, or that it waits.
#include "core/comms/peer.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/radar.h"
#include "test/support/glass_text.h"
#include "test/support/radar_rig.h"

using namespace skyblip::go;
using skyblip::reads_in;
using skyblip::comms::BravoState;

TEST_CASE("radar page: a live Bravo stands over the count, in both plots, and nothing else does") {
    RadarSnapshot live = flying(0);
    live.bravo = BravoState::Live;
    live.heard_count = 7;
    const Glass to_scale = radar(live);
    CHECK(reads_in(to_scale, "BRAVO", 160, 160, 200, 178));
    CHECK(reads_in(to_scale, "7", 170, 170, 200, 200, 3));

    live.plot = RadarPlot::All;
    CHECK(reads_in(radar(live), "BRAVO", 160, 160, 200, 178));

    for (const BravoState state : {BravoState::Unpaired, BravoState::LinkDown}) {
        RadarSnapshot other = flying(0);
        other.bravo = state;
        CHECK_FALSE(reads_in(radar(other), "BRAVO", 0, 0, 200, 199));
    }
}

TEST_CASE("radar page: a lost Bravo takes the ring's banner, and the line under it says why") {
    struct Loss {
        BravoState state;
        const char* why;
    };
    for (const Loss loss :
         {Loss{BravoState::LinkDown, "LINK DOWN"}, Loss{BravoState::ReceiverDown, "RECEIVER DOWN"},
          Loss{BravoState::NoUtc, "NO UTC"}}) {
        CAPTURE(loss.why);
        for (const RadarPlot plot : {RadarPlot::ToScale, RadarPlot::All}) {
            RadarSnapshot lost = flying(0);
            lost.plot = plot;
            lost.bravo = loss.state;
            const Glass canvas = radar(lost);
            CHECK(reads_in(canvas, "NO BRAVO", 20, 110, 180, 165, 2));
            CHECK(reads_in(canvas, loss.why, 20, 130, 180, 175));
        }
    }
    RadarSnapshot waiting = flying(0);
    waiting.bravo = BravoState::Waiting;
    CHECK_FALSE(reads_in(radar(waiting), "NO BRAVO", 0, 0, 200, 199, 2));
}

TEST_CASE("radar page: a waiting Bravo takes the loss's place, WAIT FOR over BRAVO at one size") {
    for (const RadarPlot plot : {RadarPlot::ToScale, RadarPlot::All}) {
        RadarSnapshot waiting = flying(0);
        waiting.plot = plot;
        waiting.bravo = BravoState::Waiting;
        const Glass canvas = radar(waiting);
        CHECK(reads_in(canvas, "WAIT FOR", 20, 100, 180, 165, 2));
        // One message on two lines keeps one size: the small line is for a second message.
        CHECK(reads_in(canvas, "BRAVO", 20, 110, 180, 180, 2));
        CHECK_FALSE(reads_in(canvas, "BRAVO", 0, 0, 200, 200, 1));
    }

    RadarSnapshot blind = flying(0);
    blind.fix_valid = false;
    blind.bravo = BravoState::Waiting;
    const Glass searching = radar(blind);
    CHECK(reads_in(searching, "NO FIX", 40, 120, 160, 160, 2));
    CHECK(reads_in(searching, "WAIT FOR BRAVO", 20, 140, 180, 175));
}

// The ring's two slots read top down: the cell, NO FIX, NO BRAVO and its reason, then the rest.
TEST_CASE("radar page: a lost Bravo yields the banner to a warned cell and to NO FIX") {
    RadarSnapshot parked = flying(0);
    parked.flight_running = false;
    parked.bravo = BravoState::LinkDown;
    const Glass ground = radar(parked);
    CHECK(reads_in(ground, "NO BRAVO", 20, 110, 180, 165, 2));
    CHECK_FALSE(reads_in(ground, "GROUND", 0, 0, 200, 199, 2));
    CHECK_FALSE(reads_in(ground, "GROUND", 0, 0, 200, 199));

    RadarSnapshot low = parked;
    low.battery_low = true;
    low.battery_percent = 4;
    const Glass warned = radar(low);
    CHECK(reads_in(warned, "BAT 4%", 40, 120, 160, 160, 2));
    CHECK(reads_in(warned, "NO BRAVO", 20, 140, 180, 175));
    CHECK_FALSE(reads_in(warned, "LINK DOWN", 0, 0, 200, 199));

    RadarSnapshot blind = parked;
    blind.fix_valid = false;
    const Glass searching = radar(blind);
    CHECK(reads_in(searching, "NO FIX", 40, 120, 160, 160, 2));
    CHECK(reads_in(searching, "NO BRAVO", 20, 140, 180, 175));
}
