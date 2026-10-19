// The list a pilot reads when the plot says something is there: who, how far, how much above.
#include <cstring>

#include "core/model/aircraft.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/nearby.h"
#include "test/support/glass_text.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

constexpr int kRowH = 7 * kNearbyScale;

int row_y(int index) { return kNearbyFirstRowY + index * kNearbyLineHeight; }

// Ink inside one row band, so a drawn row is told from an empty one without asserting on glyphs.
int ink_in_row(const Glass& canvas, int index) {
    const int y0 = row_y(index);
    int n = 0;
    for (int y = y0; y < y0 + kRowH; y++)
        for (int x = 0; x < Glass::kWidth; x++)
            if (canvas.get_pixel(x, y)) n++;
    return n;
}

// A case claims a column reads "2.3", not that there is ink in it.
bool reads_right_of(const Glass& canvas, int x_end, int y, const char* text, int scale) {
    return reads_at(canvas, x_end - static_cast<int>(std::strlen(text)) * ui::kCellWidth * scale, y,
                    text, scale);
}

bool banner_reads(const Glass& canvas, const char* text) {
    int n = 0;
    while (text[n]) n++;
    const int w = n * 6 * kNearbyScale;
    return reads_at(canvas, (Glass::kWidth - w) / 2, kNearbyBannerY, text, kNearbyScale);
}

// Where the small address lands: after the name it stands beside, on the same bottom.
int own_address_x() {
    return kNearbyIdX + kNearbyOwnNameCharacters * 6 * kNearbyTitleScale + kNearbyWordGap;
}

NearbySnapshot listing(const traffic::RangeRow* rows, int n, go::Units units) {
    NearbySnapshot snapshot;
    snapshot.own_address = 0x4B1F07;
    snapshot.fix_valid = true;
    snapshot.units = units;
    snapshot.heard_count = n;
    snapshot.row_count = n;
    snapshot.rows = rows;
    return snapshot;
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

TEST_CASE("nearby page: one row per emitter heard, and none for the rest") {
    traffic::RangeRow rows[3] = {row_at(400, 120), row_at(4300, -366), row_at(9100, 0)};

    Glass canvas;
    draw_nearby(canvas, listing(rows, 3, go::Units::Nautical));

    for (int i = 0; i < 3; i++) CHECK(ink_in_row(canvas, i) > 20);
    CHECK(ink_in_row(canvas, 3) == 0);
}

// The address is what a pilot reads back to a controller or matches against a club list.
TEST_CASE("nearby page: a row names the system that was heard and the whole address") {
    traffic::RangeRow rows[3] = {row_at(400, 0), row_at(900, 0), row_at(1400, 0)};
    rows[1].source = model::Source::Alptas;
    rows[2].source = model::Source::AdslUplink;

    Glass canvas;
    draw_nearby(canvas, listing(rows, 3, go::Units::Nautical));

    CHECK(reads_at(canvas, kNearbyIdX, row_y(0), "A 3FA21C", kNearbyScale));
    CHECK(reads_at(canvas, kNearbyIdX, row_y(1), "F 3FA21C", kNearbyScale));
    CHECK(reads_at(canvas, kNearbyIdX, row_y(2), "U 3FA21C", kNearbyScale));
}

// ADS-L carries no name, so one only reaches this column from a Type 66 burst.
TEST_CASE("nearby page: a row wears the callsign in place of the address once one is heard") {
    traffic::RangeRow rows[2] = {row_at(400, 0), row_at(900, 0)};
    rows[0].callsign = "D-KXYZ";

    Glass canvas;
    draw_nearby(canvas, listing(rows, 2, go::Units::Nautical));

    CHECK(reads_at(canvas, kNearbyIdX, row_y(0), "A D-KXYZ", kNearbyScale));
    CHECK(reads_at(canvas, kNearbyIdX, row_y(1), "A 3FA21C", kNearbyScale));
}

// The column ends where DIST begins, and a name that would run into it is cut, not wrapped.
TEST_CASE("nearby page: a callsign wider than the column is drawn as far as the column goes") {
    traffic::RangeRow rows[1] = {row_at(400, 0)};
    rows[0].callsign = "ABCDEFGHIJKLMN";

    Glass canvas;
    draw_nearby(canvas, listing(rows, 1, go::Units::Nautical));

    CHECK(reads_at(canvas, kNearbyIdX, row_y(0), "A ABCDEF", kNearbyScale));
    CHECK(reads_right_of(canvas, kNearbyDistanceEnd, row_y(0), "0.2", kNearbyScale));
}

TEST_CASE("nearby page: no fix means no range, and the page says so instead of listing") {
    traffic::RangeRow rows[1] = {row_at(4300, 120)};
    NearbySnapshot snapshot = listing(rows, 1, go::Units::Nautical);
    snapshot.fix_valid = false;
    snapshot.heard_count = 4;

    Glass canvas;
    draw_nearby(canvas, snapshot);
    CHECK(banner_reads(canvas, "NO FIX"));
    CHECK(ink_in_row(canvas, 0) == 0);
}

// An empty list and a page that is not listing look alike, so the empty one says which it is.
TEST_CASE("nearby page: an empty sky is a word on the second row, not an empty list") {
    Glass canvas;
    draw_nearby(canvas, listing(nullptr, 0, go::Units::Nautical));
    CHECK(banner_reads(canvas, "NO TRAFFIC"));
}

TEST_CASE("nearby page: the header counts what was heard, not what fits") {
    NearbySnapshot snapshot;
    snapshot.fix_valid = true;
    snapshot.heard_count = 26;
    snapshot.row_count = 0;
    snapshot.rows = nullptr;

    Glass canvas;
    draw_nearby(canvas, snapshot);
    // The figure is read, the word only says what was counted: the radar's own ACT arrangement.
    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, kNearbyTitleY, "26", 2));
    CHECK(
        reads_in(canvas, "TRAFFIC", 0, kNearbyTitleY, kNearbyRelativeAltitudeEnd, kNearbyHeadY, 1));
    CHECK(ink_in_row(canvas, 0) == 0);
}

// Own address is read back on the radio, and this is the page a pilot is already on to find it.
TEST_CASE("nearby page: the top line is own address, and nothing stands in front of it") {
    traffic::RangeRow rows[1] = {row_at(900, 0)};

    Glass canvas;
    draw_nearby(canvas, listing(rows, 1, go::Units::Nautical));

    CHECK(reads_at(canvas, kNearbyIdX, kNearbyTitleY, "4B1F07", kNearbyTitleScale));
    CHECK(reads_at(canvas, kNearbyIdX, row_y(0), "A 3FA21C", kNearbyScale));
}

// The name is what own-ship is called on the radio, the address is what it transmits under.
TEST_CASE("nearby page: a named own-ship reads its callsign, with the address still beside it") {
    NearbySnapshot snapshot = listing(nullptr, 0, go::Units::Nautical);
    snapshot.own_callsign = "S-BLIP";

    Glass canvas;
    draw_nearby(canvas, snapshot);

    CHECK(reads_at(canvas, kNearbyIdX, kNearbyTitleY, "S-BLIP", kNearbyTitleScale));
    CHECK(reads_at(canvas, own_address_x(), kNearbyTitleY + 7, "4B1F07", 1));
}

// The count owns the right of that line, so a long name is cut where the row column cuts one.
TEST_CASE("nearby page: own callsign is cut to the six characters a registration is known by") {
    NearbySnapshot snapshot = listing(nullptr, 0, go::Units::Nautical);
    snapshot.own_callsign = "ABCDEFGHI";

    Glass canvas;
    draw_nearby(canvas, snapshot);

    CHECK(reads_at(canvas, kNearbyIdX, kNearbyTitleY, "ABCDEF", kNearbyTitleScale));
    CHECK(reads_at(canvas, own_address_x(), kNearbyTitleY + 7, "4B1F07", 1));
}

// The letter in front of the address is the source the report arrived by, and nothing said so.
TEST_CASE("nearby page: every head stands on the edge of the figures under it") {
    traffic::RangeRow rows[1] = {row_at(4300, 366)};

    Glass canvas;
    draw_nearby(canvas, listing(rows, 1, go::Units::Nautical));

    CHECK(reads_at(canvas, kNearbyIdX, kNearbyHeadY, "SRC ID", 1));
    CHECK(reads_right_of(canvas, kNearbyDistanceEnd, kNearbyHeadY, "DIST", 1));
    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, kNearbyHeadY, "ALT", 1));
}

// B4. A pilot who asked for miles on the instruments is not handed kilometres here.
TEST_CASE("nearby page: the distance column reads in the unit a pilot set") {
    traffic::RangeRow rows[1] = {row_at(4300, 0)};  // 4.3 km, 2.3 NM

    Glass nautical, metric;
    draw_nearby(nautical, listing(rows, 1, go::Units::Nautical));
    draw_nearby(metric, listing(rows, 1, go::Units::Metric));

    CHECK(reads_right_of(nautical, kNearbyDistanceEnd, row_y(0), "2.3", kNearbyScale));
    CHECK(reads_right_of(metric, kNearbyDistanceEnd, row_y(0), "4.3", kNearbyScale));
}

// The vertical is the column next door, so a distance that carried it would say it twice.
TEST_CASE("nearby page: distance is the ground track, whatever the traffic is above or below by") {
    traffic::RangeRow rows[1] = {row_at(0, 3000)};

    Glass canvas;
    draw_nearby(canvas, listing(rows, 1, go::Units::Nautical));

    CHECK(reads_right_of(canvas, kNearbyDistanceEnd, row_y(0), "0.0", kNearbyScale));
    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, row_y(0), "+98", kNearbyScale));
}

// The column is scanned down, and a figure that changes width moves under the eye doing it.
TEST_CASE("nearby page: relative altitude is two digits and a sign, the width the radar tags") {
    traffic::RangeRow rows[2] = {row_at(400, 120), row_at(900, -30)};  // 394 ft, -98 ft

    Glass canvas;
    draw_nearby(canvas, listing(rows, 2, go::Units::Nautical));

    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, row_y(0), "+04", kNearbyScale));
    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, row_y(1), "-01", kNearbyScale));
}

// A separation is cleared and flown in feet wherever the aeroplane is.
TEST_CASE("nearby page: relative altitude reads in hundreds of feet under either unit setting") {
    traffic::RangeRow rows[1] = {row_at(4300, 366)};  // up 366 m, 1200 ft

    Glass nautical, metric;
    draw_nearby(nautical, listing(rows, 1, go::Units::Nautical));
    draw_nearby(metric, listing(rows, 1, go::Units::Metric));

    CHECK(reads_right_of(nautical, kNearbyRelativeAltitudeEnd, row_y(0), "+12", kNearbyScale));
    CHECK(reads_right_of(metric, kNearbyRelativeAltitudeEnd, row_y(0), "+12", kNearbyScale));
}

TEST_CASE("nearby page: traffic below carries its sign, and traffic at this level carries none") {
    traffic::RangeRow rows[2] = {row_at(1200, -366), row_at(2000, 3)};

    Glass canvas;
    draw_nearby(canvas, listing(rows, 2, go::Units::Nautical));

    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, row_y(0), "-12", kNearbyScale));
    // +0 and -0 are the same number, and a sign an eye has to discard is one not to draw.
    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, row_y(1), "00", kNearbyScale));
}

// Three double-height columns fill the glass, so a figure with one more digit hits its neighbour.
TEST_CASE("nearby page: a distance or a separation past what the column holds is said in words") {
    traffic::RangeRow rows[2] = {row_at(200000, 9000), row_at(1000, -9000)};

    Glass canvas;
    draw_nearby(canvas, listing(rows, 2, go::Units::Nautical));

    CHECK(reads_right_of(canvas, kNearbyDistanceEnd, row_y(0), "FAR", kNearbyScale));
    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, row_y(0), "+99", kNearbyScale));
    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, row_y(1), "-99", kNearbyScale));
}

TEST_CASE("nearby page: more emitters than rows are cut, never overdrawn") {
    traffic::RangeRow rows[kNearbyRows + 4];
    for (int i = 0; i < kNearbyRows + 4; i++) rows[i] = row_at(500 + 100 * i, 30 * i);

    Glass canvas;
    draw_nearby(canvas, listing(rows, kNearbyRows + 4, go::Units::Nautical));

    for (int i = 0; i < kNearbyRows; i++) CHECK(ink_in_row(canvas, i) > 20);
    for (int y = row_y(kNearbyRows - 1) + kRowH; y < Glass::kHeight; y++)
        for (int x = 0; x < Glass::kWidth; x++) CHECK_FALSE(canvas.get_pixel(x, y));
}
