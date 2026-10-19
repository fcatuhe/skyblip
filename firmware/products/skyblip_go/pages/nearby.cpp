#include "products/skyblip_go/pages/nearby.h"

#include "core/model/aircraft.h"
#include "core/units/units.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go {

namespace {
using ui::kCellWidth;
using ui::kCharacterHeight;
constexpr int kCountScale = 2;
constexpr int kRuleY = 32;
constexpr int kRelativeHundredsCeiling = 99;
constexpr int32_t kDistanceCeilingE1 = 999;

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int length, int scale) {
    canvas.draw_text(x_end - length * kCellWidth * scale, y, text, true, scale);
}

void right_aligned_text(ui::Canvas& canvas, int x_end, int y, const char* text, int scale) {
    int count = 0;
    while (text[count]) count++;
    right_aligned(canvas, x_end, y, text, count, scale);
}

void draw_count(ui::Canvas& canvas, int heard_count) {
    char text[8];
    const int written = format_unsigned(text, static_cast<uint32_t>(heard_count), 1);
    text[written] = 0;
    right_aligned(canvas, kNearbyRelativeAltitudeEnd, kNearbyTitleY, text, written, kCountScale);

    const int word_end =
        kNearbyRelativeAltitudeEnd - written * kCellWidth * kCountScale - kNearbyWordGap;
    right_aligned_text(canvas, word_end, kNearbyTitleY + kCharacterHeight * (kCountScale - 1),
                       "TRAFFIC", 1);
}

int format_own_name(char* out, const char* callsign) {
    int written = 0;
    while (written < kNearbyOwnNameCharacters && callsign[written] != 0) {
        out[written] = callsign[written];
        written++;
    }
    return written;
}

void draw_own_address(ui::Canvas& canvas, int x, int y, settings::AirAddress address, int scale) {
    char buffer[8];
    const int written = format_air_address(buffer, address);
    buffer[written] = 0;
    canvas.draw_text(x, y, buffer, true, scale);
}

void draw_own_id(ui::Canvas& canvas, const NearbySnapshot& snapshot) {
    if (snapshot.own_callsign == nullptr || snapshot.own_callsign[0] == 0) {
        draw_own_address(canvas, kNearbyIdX, kNearbyTitleY, snapshot.own_address,
                         kNearbyTitleScale);
        return;
    }

    char text[16];
    const int written = format_own_name(text, snapshot.own_callsign);
    text[written] = 0;
    canvas.draw_text(kNearbyIdX, kNearbyTitleY, text, true, kNearbyTitleScale);

    const int address_x = kNearbyIdX + written * kCellWidth * kNearbyTitleScale + kNearbyWordGap;
    draw_own_address(canvas, address_x, kNearbyTitleY + kCharacterHeight * (kNearbyTitleScale - 1),
                     snapshot.own_address, 1);
}

void banner(ui::Canvas& canvas, const char* text) {
    int count = 0;
    while (text[count]) count++;
    canvas.draw_text((kGlassWidth - count * kCellWidth * kNearbyScale) / 2, kNearbyBannerY, text,
                     true, kNearbyScale);
}

void draw_header(ui::Canvas& canvas, const NearbySnapshot& snapshot) {
    draw_own_id(canvas, snapshot);
    draw_count(canvas, snapshot.heard_count);

    canvas.draw_text(kNearbyIdX, kNearbyHeadY, "SRC ID", true, 1);
    right_aligned_text(canvas, kNearbyDistanceEnd, kNearbyHeadY, "DIST", 1);
    right_aligned_text(canvas, kNearbyRelativeAltitudeEnd, kNearbyHeadY, "ALT", 1);
    canvas.horizontal_line(kNearbyIdX, kRuleY, kNearbyRelativeAltitudeEnd - kNearbyIdX, true);
}

int32_t relative_hundreds_of_feet(int32_t up_m) {
    const int32_t feet = to_feet(Metres(up_m)).value;
    const int32_t rounded = (feet >= 0 ? feet + 50 : feet - 50) / 100;
    if (rounded > kRelativeHundredsCeiling) return kRelativeHundredsCeiling;
    if (rounded < -kRelativeHundredsCeiling) return -kRelativeHundredsCeiling;
    return rounded;
}

int format_id(char* out, const traffic::RangeRow& row) {
    out[0] = model::source_letter(row.source);
    out[1] = ' ';
    if (row.callsign == nullptr || row.callsign[0] == 0)
        return 2 + format_hex(out + 2, row.address, 6);
    int written = 2;
    while (written < kNearbyIdCharacters && row.callsign[written - 2] != 0) {
        out[written] = row.callsign[written - 2];
        written++;
    }
    return written;
}

void draw_row(ui::Canvas& canvas, int y, const traffic::RangeRow& row, bool metric) {
    char text[16];

    const int id_written = format_id(text, row);
    text[id_written] = 0;
    canvas.draw_text(kNearbyIdX, y, text, true, kNearbyScale);

    const int32_t distance_e1 =
        metric ? div_round(row.ground_m, 100) : to_nm_e1(Metres(row.ground_m)).value;
    int written = distance_e1 > kDistanceCeilingE1
                      ? format_string(text, "FAR")
                      : format_unsigned(text, static_cast<uint32_t>(distance_e1), 2, 1);
    text[written] = 0;
    right_aligned(canvas, kNearbyDistanceEnd, y, text, written, kNearbyScale);

    const int32_t relative = relative_hundreds_of_feet(row.up_m);
    written = format_signed(text, relative, 2, 0, relative != 0);
    text[written] = 0;
    right_aligned(canvas, kNearbyRelativeAltitudeEnd, y, text, written, kNearbyScale);
}

}  // namespace

void draw_nearby(ui::Canvas& canvas, const NearbySnapshot& snapshot) {
    draw_header(canvas, snapshot);

    if (!snapshot.fix_valid) {
        banner(canvas, "NO FIX");
        return;
    }
    if (snapshot.row_count == 0 || snapshot.rows == nullptr) {
        banner(canvas, "NO TRAFFIC");
        return;
    }

    const int rows = snapshot.row_count < kNearbyRows ? snapshot.row_count : kNearbyRows;
    const bool metric = snapshot.units == go::Units::Metric;
    for (int i = 0; i < rows; i++)
        draw_row(canvas, kNearbyFirstRowY + i * kNearbyLineHeight, snapshot.rows[i], metric);
}

}  // namespace skyblip::go
