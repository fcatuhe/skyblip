#include "products/skyblip_go/pages/nearby.h"

#include "core/model/aircraft.h"
#include "core/units/units.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go {

namespace {
using ui::kCellW;
using ui::kCharH;
constexpr int kCountScale = 2;
constexpr int kRuleY = 32;
constexpr int kRelHundredsCeiling = 99;
constexpr int32_t kDistanceE1Ceiling = 999;

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int len, int scale) {
    canvas.draw_text(x_end - len * kCellW * scale, y, text, true, scale);
}

void right_aligned_text(ui::Canvas& canvas, int x_end, int y, const char* text, int scale) {
    int n = 0;
    while (text[n]) n++;
    right_aligned(canvas, x_end, y, text, n, scale);
}

void draw_count(ui::Canvas& canvas, int heard_count) {
    char text[8];
    const int written = format_uint(text, static_cast<uint32_t>(heard_count), 1);
    text[written] = 0;
    right_aligned(canvas, kNearbyRelativeAltitudeEnd, kNearbyTitleY, text, written, kCountScale);

    const int word_end =
        kNearbyRelativeAltitudeEnd - written * kCellW * kCountScale - kNearbyWordGap;
    right_aligned_text(canvas, word_end, kNearbyTitleY + kCharH * (kCountScale - 1), "TRAFFIC", 1);
}

int format_own_name(char* out, const char* callsign) {
    int written = 0;
    while (written < kNearbyOwnNameChars && callsign[written] != 0) {
        out[written] = callsign[written];
        written++;
    }
    return written;
}

void draw_own_address(ui::Canvas& canvas, int x, int y, settings::AirAddress address, int scale) {
    char buf[8];
    const int n = format_air_address(buf, address);
    buf[n] = 0;
    canvas.draw_text(x, y, buf, true, scale);
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

    const int address_x = kNearbyIdX + written * kCellW * kNearbyTitleScale + kNearbyWordGap;
    draw_own_address(canvas, address_x, kNearbyTitleY + kCharH * (kNearbyTitleScale - 1),
                     snapshot.own_address, 1);
}

void banner(ui::Canvas& canvas, const char* text) {
    int n = 0;
    while (text[n]) n++;
    canvas.draw_text((kGlassWidth - n * kCellW * kNearbyScale) / 2, kNearbyBannerY, text, true,
                     kNearbyScale);
}

void draw_header(ui::Canvas& canvas, const NearbySnapshot& snapshot) {
    draw_own_id(canvas, snapshot);
    draw_count(canvas, snapshot.heard_count);

    canvas.draw_text(kNearbyIdX, kNearbyHeadY, "SRC ID", true, 1);
    right_aligned_text(canvas, kNearbyDistanceEnd, kNearbyHeadY, "DIST", 1);
    right_aligned_text(canvas, kNearbyRelativeAltitudeEnd, kNearbyHeadY, "ALT", 1);
    canvas.hline(kNearbyIdX, kRuleY, kNearbyRelativeAltitudeEnd - kNearbyIdX, true);
}

int32_t rel_hundreds_of_feet(int32_t up_m) {
    const int32_t feet = to_feet(Metres(up_m)).v;
    const int32_t rounded = (feet >= 0 ? feet + 50 : feet - 50) / 100;
    if (rounded > kRelHundredsCeiling) return kRelHundredsCeiling;
    if (rounded < -kRelHundredsCeiling) return -kRelHundredsCeiling;
    return rounded;
}

int format_id(char* out, const traffic::RangeRow& row) {
    out[0] = model::source_letter(row.source);
    out[1] = ' ';
    if (row.callsign == nullptr || row.callsign[0] == 0)
        return 2 + format_hex(out + 2, row.address, 6);
    int written = 2;
    while (written < kNearbyIdChars && row.callsign[written - 2] != 0) {
        out[written] = row.callsign[written - 2];
        written++;
    }
    return written;
}

void draw_row(ui::Canvas& canvas, int y, const traffic::RangeRow& row, bool metric) {
    char text[16];

    const int n_id = format_id(text, row);
    text[n_id] = 0;
    canvas.draw_text(kNearbyIdX, y, text, true, kNearbyScale);

    const int32_t dist_e1 =
        metric ? div_round(row.ground_m, 100) : to_nm_e1(Metres(row.ground_m)).v;
    int written = dist_e1 > kDistanceE1Ceiling
                      ? format_string(text, "FAR")
                      : format_uint(text, static_cast<uint32_t>(dist_e1), 2, 1);
    text[written] = 0;
    right_aligned(canvas, kNearbyDistanceEnd, y, text, written, kNearbyScale);

    const int32_t rel = rel_hundreds_of_feet(row.up_m);
    written = format_int(text, rel, 2, 0, rel != 0);
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
        draw_row(canvas, kNearbyFirstRowY + i * kNearbyLineH, snapshot.rows[i], metric);
}

}  // namespace skyblip::go
