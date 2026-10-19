// The list a pilot reads when the plot says something is there: who, how far, how much above.
#include "core/model/aircraft.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/nearby.h"
#include "test/support/glass_text.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

constexpr int kRowH = 7 * kNearbyScale;

int row_y(int index) { return kNearbyFirstRowY + index * kNearbyLineH; }

// Ink inside one row band, so a drawn row is told from an empty one without asserting on glyphs.
int ink_in_row(const Glass& fb, int index) {
    const int y0 = row_y(index);
    int n = 0;
    for (int y = y0; y < y0 + kRowH; y++)
        for (int x = 0; x < Glass::kW; x++)
            if (fb.get_pixel(x, y)) n++;
    return n;
}

// A case claims a column reads "2.3", not that there is ink in it.
bool reads_right_of(const Glass& fb, int x_end, int y, const char* text, int scale) {
    int n = 0;
    while (text[n]) n++;
    Glass wanted;
    wanted.clear(true);
    wanted.draw_text(0, 0, text, true, scale);
    const int x0 = x_end - n * 6 * scale;
    for (int dy = 0; dy < 7 * scale; dy++)
        for (int dx = 0; dx < n * 6 * scale - 1; dx++)
            if (fb.get_pixel(x0 + dx, y + dy) != wanted.get_pixel(dx, dy)) return false;
    return true;
}

bool reads_at(const Glass& fb, int x, int y, const char* text, int scale) {
    int n = 0;
    while (text[n]) n++;
    return reads_right_of(fb, x + n * 6 * scale, y, text, scale);
}

bool banner_reads(const Glass& fb, const char* text) {
    int n = 0;
    while (text[n]) n++;
    const int w = n * 6 * kNearbyScale;
    return reads_at(fb, (Glass::kW - w) / 2, kNearbyBannerY, text, kNearbyScale);
}

// Where the small address lands: after the name it stands beside, on the same bottom.
int own_address_x() {
    return kNearbyIdX + kNearbyOwnNameChars * 6 * kNearbyTitleScale + kNearbyWordGap;
}

NearbySnapshot listing(const traffic::RangeRow* rows, int n, go::Units units) {
    NearbySnapshot snap;
    snap.own_address = 0x4B1F07;
    snap.fix_valid = true;
    snap.units = units;
    snap.n_heard = n;
    snap.n_rows = n;
    snap.rows = rows;
    return snap;
}

traffic::RangeRow row_at(int32_t ground_m, int32_t up_m) {
    traffic::RangeRow r;
    r.address = 0x3FA21C;
    r.source = model::Source::AdslDirect;
    r.ground_m = ground_m;
    r.up_m = up_m;
    return r;
}

}  // namespace

TEST_CASE("nearby: one row per emitter heard, and none for the rest") {
    traffic::RangeRow rows[3] = {row_at(400, 120), row_at(4300, -366), row_at(9100, 0)};

    Glass fb;
    draw_nearby(fb, listing(rows, 3, go::Units::Nautical));

    for (int i = 0; i < 3; i++) CHECK(ink_in_row(fb, i) > 20);
    CHECK(ink_in_row(fb, 3) == 0);
}

// The address is what a pilot reads back to a controller or matches against a club list.
TEST_CASE("nearby: a row names the system that was heard and the whole address") {
    traffic::RangeRow rows[3] = {row_at(400, 0), row_at(900, 0), row_at(1400, 0)};
    rows[1].source = model::Source::Alptas;
    rows[2].source = model::Source::AdslUplink;

    Glass fb;
    draw_nearby(fb, listing(rows, 3, go::Units::Nautical));

    CHECK(reads_at(fb, kNearbyIdX, row_y(0), "A 3FA21C", kNearbyScale));
    CHECK(reads_at(fb, kNearbyIdX, row_y(1), "F 3FA21C", kNearbyScale));
    CHECK(reads_at(fb, kNearbyIdX, row_y(2), "U 3FA21C", kNearbyScale));
}

// ADS-L carries no name, so one only reaches this column from a Type 66 burst.
TEST_CASE("nearby: a row wears the callsign in place of the address once one is heard") {
    traffic::RangeRow rows[2] = {row_at(400, 0), row_at(900, 0)};
    rows[0].callsign = "D-KXYZ";

    Glass fb;
    draw_nearby(fb, listing(rows, 2, go::Units::Nautical));

    CHECK(reads_at(fb, kNearbyIdX, row_y(0), "A D-KXYZ", kNearbyScale));
    CHECK(reads_at(fb, kNearbyIdX, row_y(1), "A 3FA21C", kNearbyScale));
}

// The column ends where DIST begins, and a name that would run into it is cut, not wrapped.
TEST_CASE("nearby: a callsign wider than the column is drawn as far as the column goes") {
    traffic::RangeRow rows[1] = {row_at(400, 0)};
    rows[0].callsign = "ABCDEFGHIJKLMN";

    Glass fb;
    draw_nearby(fb, listing(rows, 1, go::Units::Nautical));

    CHECK(reads_at(fb, kNearbyIdX, row_y(0), "A ABCDEF", kNearbyScale));
    CHECK(reads_right_of(fb, kNearbyDistEnd, row_y(0), "0.2", kNearbyScale));
}

TEST_CASE("nearby: no fix means no range, and the page says so instead of listing") {
    traffic::RangeRow rows[1] = {row_at(4300, 120)};
    NearbySnapshot snap = listing(rows, 1, go::Units::Nautical);
    snap.fix_valid = false;
    snap.n_heard = 4;

    Glass fb;
    draw_nearby(fb, snap);
    CHECK(banner_reads(fb, "NO FIX"));
    CHECK(ink_in_row(fb, 0) == 0);
}

// An empty list and a page that is not listing look alike, so the empty one says which it is.
TEST_CASE("nearby: an empty sky is a word on the second row, not an empty list") {
    Glass fb;
    draw_nearby(fb, listing(nullptr, 0, go::Units::Nautical));
    CHECK(banner_reads(fb, "NO TRAFFIC"));
}

TEST_CASE("nearby: the header counts what was heard, not what fits") {
    NearbySnapshot snap;
    snap.fix_valid = true;
    snap.n_heard = 26;
    snap.n_rows = 0;
    snap.rows = nullptr;

    Glass fb;
    draw_nearby(fb, snap);
    // The figure is read, the word only says what was counted: the radar's own ACT arrangement.
    CHECK(reads_right_of(fb, kNearbyRelEnd, kNearbyTitleY, "26", 2));
    CHECK(reads_in(fb, "TRAFFIC", 0, kNearbyTitleY, kNearbyRelEnd, kNearbyHeadY, 1));
    CHECK(ink_in_row(fb, 0) == 0);
}

// Own address is read back on the radio, and this is the page a pilot is already on to find it.
TEST_CASE("nearby: the top line is own address, and nothing stands in front of it") {
    traffic::RangeRow rows[1] = {row_at(900, 0)};

    Glass fb;
    draw_nearby(fb, listing(rows, 1, go::Units::Nautical));

    CHECK(reads_at(fb, kNearbyIdX, kNearbyTitleY, "4B1F07", kNearbyTitleScale));
    CHECK(reads_at(fb, kNearbyIdX, row_y(0), "A 3FA21C", kNearbyScale));
}

// The name is what own-ship is called on the radio, the address is what it transmits under.
TEST_CASE("nearby: a named own-ship reads its callsign, with the address still beside it") {
    NearbySnapshot snap = listing(nullptr, 0, go::Units::Nautical);
    snap.own_callsign = "S-BLIP";

    Glass fb;
    draw_nearby(fb, snap);

    CHECK(reads_at(fb, kNearbyIdX, kNearbyTitleY, "S-BLIP", kNearbyTitleScale));
    CHECK(reads_at(fb, own_address_x(), kNearbyTitleY + 7, "4B1F07", 1));
}

// The count owns the right of that line, so a long name is cut where the row column cuts one.
TEST_CASE("nearby: own callsign is cut to the six characters a registration is known by") {
    NearbySnapshot snap = listing(nullptr, 0, go::Units::Nautical);
    snap.own_callsign = "ABCDEFGHI";

    Glass fb;
    draw_nearby(fb, snap);

    CHECK(reads_at(fb, kNearbyIdX, kNearbyTitleY, "ABCDEF", kNearbyTitleScale));
    CHECK(reads_at(fb, own_address_x(), kNearbyTitleY + 7, "4B1F07", 1));
}

// The letter in front of the address is the source the report arrived by, and nothing said so.
TEST_CASE("nearby: every head stands on the edge of the figures under it") {
    traffic::RangeRow rows[1] = {row_at(4300, 366)};

    Glass fb;
    draw_nearby(fb, listing(rows, 1, go::Units::Nautical));

    CHECK(reads_at(fb, kNearbyIdX, kNearbyHeadY, "SRC ID", 1));
    CHECK(reads_right_of(fb, kNearbyDistEnd, kNearbyHeadY, "DIST", 1));
    CHECK(reads_right_of(fb, kNearbyRelEnd, kNearbyHeadY, "ALT", 1));
}

// B4. A pilot who asked for miles on the instruments is not handed kilometres here.
TEST_CASE("nearby: the distance column reads in the unit a pilot set") {
    traffic::RangeRow rows[1] = {row_at(4300, 0)};  // 4.3 km, 2.3 NM

    Glass nautical, metric;
    draw_nearby(nautical, listing(rows, 1, go::Units::Nautical));
    draw_nearby(metric, listing(rows, 1, go::Units::Metric));

    CHECK(reads_right_of(nautical, kNearbyDistEnd, row_y(0), "2.3", kNearbyScale));
    CHECK(reads_right_of(metric, kNearbyDistEnd, row_y(0), "4.3", kNearbyScale));
}

// The vertical is the column next door, so a distance that carried it would say it twice.
TEST_CASE("nearby: distance is the ground track, whatever the traffic is above or below by") {
    traffic::RangeRow rows[1] = {row_at(0, 3000)};

    Glass fb;
    draw_nearby(fb, listing(rows, 1, go::Units::Nautical));

    CHECK(reads_right_of(fb, kNearbyDistEnd, row_y(0), "0.0", kNearbyScale));
    CHECK(reads_right_of(fb, kNearbyRelEnd, row_y(0), "+98", kNearbyScale));
}

// The column is scanned down, and a figure that changes width moves under the eye doing it.
TEST_CASE("nearby: relative altitude is two digits and a sign, the width the radar tags") {
    traffic::RangeRow rows[2] = {row_at(400, 120), row_at(900, -30)};  // 394 ft, -98 ft

    Glass fb;
    draw_nearby(fb, listing(rows, 2, go::Units::Nautical));

    CHECK(reads_right_of(fb, kNearbyRelEnd, row_y(0), "+04", kNearbyScale));
    CHECK(reads_right_of(fb, kNearbyRelEnd, row_y(1), "-01", kNearbyScale));
}

// A separation is cleared and flown in feet wherever the aeroplane is.
TEST_CASE("nearby: relative altitude reads in hundreds of feet under either unit setting") {
    traffic::RangeRow rows[1] = {row_at(4300, 366)};  // up 366 m, 1200 ft

    Glass nautical, metric;
    draw_nearby(nautical, listing(rows, 1, go::Units::Nautical));
    draw_nearby(metric, listing(rows, 1, go::Units::Metric));

    CHECK(reads_right_of(nautical, kNearbyRelEnd, row_y(0), "+12", kNearbyScale));
    CHECK(reads_right_of(metric, kNearbyRelEnd, row_y(0), "+12", kNearbyScale));
}

TEST_CASE("nearby: traffic below carries its sign, and traffic at this level carries none") {
    traffic::RangeRow rows[2] = {row_at(1200, -366), row_at(2000, 3)};

    Glass fb;
    draw_nearby(fb, listing(rows, 2, go::Units::Nautical));

    CHECK(reads_right_of(fb, kNearbyRelEnd, row_y(0), "-12", kNearbyScale));
    // +0 and -0 are the same number, and a sign an eye has to discard is one not to draw.
    CHECK(reads_right_of(fb, kNearbyRelEnd, row_y(1), "00", kNearbyScale));
}

// Three double-height columns fill the glass, so a figure with one more digit hits its neighbour.
TEST_CASE("nearby: a distance or a separation past what the column holds is said in words") {
    traffic::RangeRow rows[2] = {row_at(200000, 9000), row_at(1000, -9000)};

    Glass fb;
    draw_nearby(fb, listing(rows, 2, go::Units::Nautical));

    CHECK(reads_right_of(fb, kNearbyDistEnd, row_y(0), "FAR", kNearbyScale));
    CHECK(reads_right_of(fb, kNearbyRelEnd, row_y(0), "+99", kNearbyScale));
    CHECK(reads_right_of(fb, kNearbyRelEnd, row_y(1), "-99", kNearbyScale));
}

TEST_CASE("nearby: more emitters than rows are cut, never overdrawn") {
    traffic::RangeRow rows[kNearbyRows + 4];
    for (int i = 0; i < kNearbyRows + 4; i++) rows[i] = row_at(500 + 100 * i, 30 * i);

    Glass fb;
    draw_nearby(fb, listing(rows, kNearbyRows + 4, go::Units::Nautical));

    for (int i = 0; i < kNearbyRows; i++) CHECK(ink_in_row(fb, i) > 20);
    for (int y = row_y(kNearbyRows - 1) + kRowH; y < Glass::kH; y++)
        for (int x = 0; x < Glass::kW; x++) CHECK_FALSE(fb.get_pixel(x, y));
}
