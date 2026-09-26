// The satellites page: a bar for every satellite in view, and what stands in before any GSV.
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/sats.h"
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
