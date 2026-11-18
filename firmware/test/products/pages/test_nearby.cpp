// The list a pilot reads when the plot says something is there: who, how far, how much above.
#include <cstring>
#include <optional>
#include <utility>

#include "core/model/aircraft.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/nearby.h"
#include "test/support/glass_text.h"
#include "test/support/nearby_glass.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

constexpr int kRowHeight = 7 * kNearbyScale;

int row_y(int index) { return kNearbyFirstRowY + index * kNearbyLineHeight; }

constexpr int kBravoY = kNearbyUnitY + kNearbyLineHeight;

// Ink inside one row band, so a drawn row is told from an empty one without asserting on glyphs.
int ink_in_row(const Glass& canvas, int index) {
    const int y0 = row_y(index);
    int count = 0;
    for (int y = y0; y < y0 + kRowHeight; y++)
        for (int x = 0; x < Glass::kWidth; x++)
            if (canvas.get_pixel(x, y)) count++;
    return count;
}

// A case claims a column reads "2.3", not that there is ink in it.
bool reads_right_of(const Glass& canvas, int x_end, int y, const char* text, int scale) {
    return reads_at(canvas, x_end - static_cast<int>(std::strlen(text)) * ui::kCellWidth * scale, y,
                    text, scale);
}

bool banner_reads(const Glass& canvas, const char* text) {
    int count = 0;
    while (text[count]) count++;
    const int width = count * 6 * kNearbyScale;
    return reads_at(canvas, (Glass::kWidth - width) / 2, kNearbyBannerY, text, kNearbyScale);
}

// A unit's figure ends where its column does, and the unit beside it stands on its baseline.
bool figure_reads(const Glass& canvas, int x_end, int y, const char* value, const char* unit) {
    const int unit_x = x_end - static_cast<int>(std::strlen(unit)) * ui::kCellWidth;
    return reads_at(canvas, unit_x, y + 7, unit, 1) &&
           reads_right_of(canvas, unit_x - 1, y, value, kNearbyScale);
}

NearbySnapshot listing(const traffic::RangeRow* rows, int count, go::Units units) {
    NearbySnapshot snapshot;
    snapshot.go.battery_percent = 87;
    snapshot.go.stage = gnss::Stage::Fixed;
    snapshot.go.satellites = 11;
    snapshot.go.transmitting = true;
    snapshot.go.traffic = count;
    snapshot.fix_valid = true;
    snapshot.units = units;
    snapshot.row_count = count;
    snapshot.rows = rows;
    return snapshot;
}

NearbySnapshot paired(const traffic::RangeRow* rows, int count) {
    NearbySnapshot snapshot = listing(rows, count, go::Units::Nautical);
    snapshot.bravo.state = comms::BravoState::Live;
    snapshot.bravo.heard = true;
    snapshot.bravo.battery_percent = 64;
    snapshot.bravo.stage = gnss::Stage::Fixed;
    snapshot.bravo.satellites = 9;
    snapshot.bravo.traffic = 3;
    return snapshot;
}

traffic::RangeRow row_at(int32_t ground_m, int32_t up_m) {
    traffic::RangeRow row;
    row.address = 0x3FA21C;
    row.source = model::Source::AdslDirect;
    row.ground_m = ground_m;
    row.up_m = up_m;
    return row;
}

}  // namespace

TEST_CASE("nearby page: one row per emitter heard, and none for the rest") {
    traffic::RangeRow rows[3] = {row_at(400, 120), row_at(4300, -366), row_at(9100, 0)};

    Glass canvas;
    draw_nearby(canvas, listing(rows, 3, go::Units::Nautical));

    for (int i = 0; i < 3; i++) CHECK(ink_in_row(canvas, i) > 20);
    CHECK(ink_in_row(canvas, 3) == 0);
}

// The small font says the aircraft gave no name, so a callsign of six hex digits is not an address.
TEST_CASE("nearby page: a row names the system at full height and the address in the small font") {
    traffic::RangeRow rows[3] = {row_at(400, 0), row_at(900, 0), row_at(1400, 0)};
    rows[1].source = model::Source::Alptas;
    rows[2].source = model::Source::AdslUplink;

    Glass canvas;
    draw_nearby(canvas, listing(rows, 3, go::Units::Nautical));

    CHECK(reads_at(canvas, kNearbyIdX, row_y(0), "L", kNearbyScale));
    CHECK(reads_at(canvas, kNearbyAddressX, row_y(0) + kNearbyAddressDrop, "3FA21C", 1));
    CHECK(nearby_lists(canvas, 'F', "3FA21C"));
    CHECK(nearby_lists(canvas, 'U', "3FA21C"));
    CHECK_FALSE(reads_in(canvas, "3FA21C", 0, row_y(0), kGlassWidth, kGlassHeight, kNearbyScale));
}

// ADS-L carries no name, so one only reaches this column from a Type 66 burst.
TEST_CASE("nearby page: a row wears the callsign in place of the address once one is heard") {
    traffic::RangeRow rows[2] = {row_at(400, 0), row_at(900, 0)};
    rows[0].callsign = "D-KXYZ";

    Glass canvas;
    draw_nearby(canvas, listing(rows, 2, go::Units::Nautical));

    CHECK(reads_at(canvas, kNearbyIdX, row_y(0), "L D-KXYZ", kNearbyScale));
    CHECK(reads_at(canvas, kNearbyAddressX, row_y(1) + kNearbyAddressDrop, "3FA21C", 1));
}

// The column ends where DIST begins, and a name that would run into it is cut, not wrapped.
TEST_CASE("nearby page: a callsign wider than the column is drawn as far as the column goes") {
    traffic::RangeRow rows[1] = {row_at(400, 0)};
    rows[0].callsign = "ABCDEFGHIJKLMN";

    Glass canvas;
    draw_nearby(canvas, listing(rows, 1, go::Units::Nautical));

    CHECK(reads_at(canvas, kNearbyIdX, row_y(0), "L ABCDEF", kNearbyScale));
    CHECK(reads_right_of(canvas, kNearbyDistanceEnd, row_y(0), "0.2", kNearbyScale));
}

TEST_CASE("nearby page: no fix means no range, and the page says so instead of listing") {
    traffic::RangeRow rows[1] = {row_at(4300, 120)};
    NearbySnapshot snapshot = listing(rows, 1, go::Units::Nautical);
    snapshot.fix_valid = false;
    snapshot.go.stage = gnss::Stage::Blind;

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

// 00 says level with us, which a target that sends no altitude has not said.
TEST_CASE("nearby page: a target that sends no altitude reads two dashes in the ALT column") {
    traffic::RangeRow rows[2] = {row_at(1200, 0), row_at(2000, 0)};
    rows[0].up_valid = false;

    Glass canvas;
    draw_nearby(canvas, listing(rows, 2, go::Units::Nautical));
    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, row_y(0), "--", kNearbyScale));
    CHECK(reads_right_of(canvas, kNearbyDistanceEnd, row_y(0), "0.6", kNearbyScale));
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
    for (int y = row_y(kNearbyRows - 1) + kRowHeight; y < Glass::kHeight; y++)
        for (int x = 0; x < Glass::kWidth; x++) CHECK_FALSE(canvas.get_pixel(x, y));
}

// What a pilot checks before taxiing: each unit's cell, its receiver, and what its radio hears.
TEST_CASE("nearby page: the Go's row reads its cell, its satellites and the traffic it heard") {
    Glass canvas;
    draw_nearby(canvas, listing(nullptr, 0, go::Units::Nautical));

    CHECK(reads_at(canvas, kNearbyIdX, kNearbyUnitY, "GO", kNearbyScale));
    CHECK(figure_reads(canvas, kNearbyBatteryEnd, kNearbyUnitY, "87", "%"));
    CHECK(figure_reads(canvas, kNearbySatellitesEnd, kNearbyUnitY, "11", "SAT"));
    CHECK(figure_reads(canvas, kNearbyRelativeAltitudeEnd, kNearbyUnitY, "0", "TFC"));
}

TEST_CASE(
    "nearby page: a Go with no fix says its receiver's rung in place of satellites and traffic") {
    for (const auto& [stage, word] :
         {std::pair{gnss::Stage::Silent, "GPS OFF"}, std::pair{gnss::Stage::Blind, "MOVE OUT"},
          std::pair{gnss::Stage::Solving, "FINDS FIX"}}) {
        CAPTURE(word);
        NearbySnapshot snapshot = listing(nullptr, 0, go::Units::Nautical);
        snapshot.fix_valid = false;
        snapshot.go.stage = stage;
        Glass canvas;
        draw_nearby(canvas, snapshot);
        CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, kNearbyUnitY, word, kNearbyScale));
        CHECK(figure_reads(canvas, kNearbyBatteryEnd, kNearbyUnitY, "87", "%"));
        CHECK_FALSE(reads_in(canvas, "SAT", 0, kNearbyUnitY, kGlassWidth, kNearbyHeadY));
    }
}

// FINDS FIX is the widest word, and a full cell is the widest figure in front of it.
TEST_CASE("nearby page: the widest word clears a full cell with air between them") {
    NearbySnapshot snapshot = listing(nullptr, 0, go::Units::Nautical);
    snapshot.fix_valid = false;
    snapshot.go.stage = gnss::Stage::Solving;
    snapshot.go.battery_percent = 100;
    Glass canvas;
    draw_nearby(canvas, snapshot);
    CHECK(figure_reads(canvas, kNearbyBatteryEnd, kNearbyUnitY, "100", "%"));
    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, kNearbyUnitY, "FINDS FIX",
                         kNearbyScale));
}

TEST_CASE("nearby page: a Go that does not transmit says TX OFF, and one on the cable CHG") {
    NearbySnapshot snapshot = listing(nullptr, 0, go::Units::Nautical);
    snapshot.go.transmitting = false;
    snapshot.go.charging = true;
    snapshot.go.battery_percent.reset();
    Glass canvas;
    draw_nearby(canvas, snapshot);
    CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, kNearbyUnitY, "TX OFF", kNearbyScale));
    CHECK(reads_right_of(canvas, kNearbyBatteryEnd, kNearbyUnitY, "CHG", kNearbyScale));
}

// The two TFC figures add up to every emitter heard, so neither unit's radio hides behind the
// other.
TEST_CASE("nearby page: a paired Go reads Bravo's row under its own, and the list a row lower") {
    traffic::RangeRow rows[1] = {row_at(400, 0)};
    Glass canvas;
    draw_nearby(canvas, paired(rows, 1));

    CHECK(reads_at(canvas, kNearbyIdX, kBravoY, "B", kNearbyScale));
    CHECK(figure_reads(canvas, kNearbyBatteryEnd, kBravoY, "64", "%"));
    CHECK(figure_reads(canvas, kNearbySatellitesEnd, kBravoY, "9", "SAT"));
    CHECK(figure_reads(canvas, kNearbyRelativeAltitudeEnd, kBravoY, "3", "TFC"));
    CHECK(reads_at(canvas, kNearbyIdX, kNearbyHeadY + kNearbyLineHeight, "SRC ID", 1));
    CHECK(nearby_lists(canvas, 'L', "3FA21C"));
    CHECK(reads_at(canvas, kNearbyIdX, row_y(1), "L", kNearbyScale));
}

TEST_CASE("nearby page: Bravo's row says why it is not live in place of satellites and traffic") {
    struct Case {
        comms::BravoState state;
        bool heard;
        std::optional<gnss::Stage> stage;
        const char* word;
    };
    for (const Case each :
         {Case{comms::BravoState::Waiting, false, std::nullopt, "NO LINK"},
          Case{comms::BravoState::LinkDown, true, gnss::Stage::Fixed, "NO LINK"},
          Case{comms::BravoState::ReceiverDown, true, gnss::Stage::Fixed, "RX DOWN"},
          Case{comms::BravoState::NoUtc, true, gnss::Stage::Blind, "MOVE OUT"},
          Case{comms::BravoState::Waiting, true, gnss::Stage::Silent, "GPS OFF"},
          Case{comms::BravoState::Live, true, gnss::Stage::Solving, "FINDS FIX"},
          Case{comms::BravoState::NoUtc, true, std::nullopt, "MOVE OUT"}}) {
        CAPTURE(each.word);
        NearbySnapshot snapshot = paired(nullptr, 0);
        snapshot.bravo.state = each.state;
        snapshot.bravo.heard = each.heard;
        snapshot.bravo.stage = each.stage;
        Glass canvas;
        draw_nearby(canvas, snapshot);
        CHECK(reads_right_of(canvas, kNearbyRelativeAltitudeEnd, kBravoY, each.word, kNearbyScale));
        CHECK_FALSE(reads_in(canvas, "TFC", 0, kBravoY, kGlassWidth, kBravoY + kNearbyLineHeight));
    }
}

// A cell read before the link went is a figure that stopped moving, so the row shows none.
TEST_CASE("nearby page: a Bravo with no link reads no cell, and one on its cable CHG") {
    NearbySnapshot lost = paired(nullptr, 0);
    lost.bravo.state = comms::BravoState::LinkDown;
    Glass gone;
    draw_nearby(gone, lost);
    CHECK(reads_right_of(gone, kNearbyBatteryEnd, kBravoY, "--", kNearbyScale));

    NearbySnapshot cabled = paired(nullptr, 0);
    cabled.bravo.charging = true;
    Glass charging;
    draw_nearby(charging, cabled);
    CHECK(reads_right_of(charging, kNearbyBatteryEnd, kBravoY, "CHG", kNearbyScale));
}

TEST_CASE("nearby page: a live Bravo from before gnss_stage reads no satellites, not a word") {
    NearbySnapshot snapshot = paired(nullptr, 0);
    snapshot.bravo.stage.reset();
    snapshot.bravo.satellites.reset();
    Glass canvas;
    draw_nearby(canvas, snapshot);
    CHECK(figure_reads(canvas, kNearbySatellitesEnd, kBravoY, "--", "SAT"));
    CHECK(figure_reads(canvas, kNearbyRelativeAltitudeEnd, kBravoY, "3", "TFC"));
}

TEST_CASE("nearby page: a paired Go lists one row fewer, and never draws past the glass") {
    traffic::RangeRow rows[kNearbyRows + 2];
    for (int i = 0; i < kNearbyRows + 2; i++) rows[i] = row_at(500 + 100 * i, 30 * i);

    Glass canvas;
    draw_nearby(canvas, paired(rows, kNearbyRows + 2));

    for (int i = 1; i < kNearbyRows; i++) CHECK(ink_in_row(canvas, i) > 20);
    for (int y = row_y(kNearbyRows - 1) + kRowHeight; y < Glass::kHeight; y++)
        for (int x = 0; x < Glass::kWidth; x++) CHECK_FALSE(canvas.get_pixel(x, y));
}
