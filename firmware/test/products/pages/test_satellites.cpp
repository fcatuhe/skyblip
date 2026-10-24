// The satellites page: a bar for every satellite in view, and what stands in before any GSV.
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/satellites.h"
#include "test/support/glass_ink.h"
#include "test/support/glass_text.h"

using namespace skyblip::go;
using skyblip::ink_in;
using skyblip::reads_in;

namespace {

skyblip::gnss::SkyView sky_of(int gps, int beidou, int used) {
    using namespace skyblip::gnss;
    SkyView sky;
    for (int i = 0; i < used; i++) sky.solving(System::Gps, static_cast<uint8_t>(1 + i));
    sky.open(System::Gps);
    for (int i = 0; i < gps; i++) {
        SatelliteView satellite;
        satellite.id = static_cast<uint8_t>(1 + i);
        satellite.system = System::Gps;
        satellite.cn0_dbhz = static_cast<uint8_t>(i < gps - 1 ? 45 - i : 0);
        sky.add(satellite);
    }
    sky.open(System::Beidou);
    for (int i = 0; i < beidou; i++) {
        SatelliteView satellite;
        satellite.id = static_cast<uint8_t>(7 + i);
        satellite.system = System::Beidou;
        satellite.cn0_dbhz = 38;
        sky.add(satellite);
    }
    return sky;
}

}  // namespace

// The page a pilot on the apron opens: what is up there, how loud, and which ones solved.
TEST_CASE(
    "satellites page: a bar for every satellite in view, filled for the ones in the solution") {
    const skyblip::gnss::SkyView sky = sky_of(6, 4, 3);
    SatellitesSnapshot snapshot;
    snapshot.levels_live = true;
    snapshot.sky = &sky;
    snapshot.hdop_e2 = 90;
    snapshot.vdop_e2 = 150;
    snapshot.solution_phase_ms = 98;
    snapshot.solution_phase_valid = true;
    Glass canvas;
    draw_satellites(canvas, snapshot);

    CHECK(reads_in(canvas, "SATELLITES", 0, 0, 120, 12));
    CHECK(reads_in(canvas, "NAV 098MS", 130, 150, 200, 168));
    CHECK(reads_in(canvas, "USED 3 OF 10", 60, 0, 200, 12));
    CHECK(reads_in(canvas, "GPS", 0, 130, 60, 145));
    CHECK(reads_in(canvas, "BDS", 0, 130, 120, 145));
    CHECK(reads_in(canvas, "HDOP 0.90", 0, 150, 130, 168));

    // A filled bar carries more ink than the hollow one beside it at the same height.
    const int first = ink_in(canvas, 4, 22, 9, 132);
    const int fourth = ink_in(canvas, 4 + 3 * 5, 22, 9 + 3 * 5, 132);
    CHECK(first > fourth);
}

// A level nobody has measured yet is not drawn as a level of zero.
TEST_CASE("satellites page: before the first GSV set the solution stands in for the bars") {
    const skyblip::gnss::SkyView sky = sky_of(6, 4, 3);
    SatellitesSnapshot snapshot;
    snapshot.fix_valid = true;
    snapshot.levels_live = false;
    snapshot.sky = &sky;
    snapshot.satellites = 9;
    Glass canvas;
    draw_satellites(canvas, snapshot);

    CHECK(reads_in(canvas, "LEVELS COMING UP", 0, 176, 200, 196));
    CHECK(reads_in(canvas, "USED 3", 60, 0, 200, 12));
    CHECK(reads_in(canvas, "GPS 3", 0, 18, 120, 32));
    CHECK(ink_in(canvas, 4, 40, 196, 130) == 0);
}

TEST_CASE(
    "satellites page: a receiver that heard nothing says so rather than drawing an empty chart") {
    SatellitesSnapshot snapshot;
    snapshot.stage = skyblip::gnss::Stage::Blind;
    Glass canvas;
    draw_satellites(canvas, snapshot);
    CHECK(reads_in(canvas, "NO SATELLITE HEARD", 0, 176, 200, 196));
    CHECK(reads_in(canvas, "BLIND", 0, 140, 80, 158));
    CHECK(reads_in(canvas, "HDOP ---", 0, 150, 130, 168));
    // No PPS edge to measure the solution against is no figure, never a zero.
    CHECK(ink_in(canvas, 130, 150, 200, 168) == 0);
}
