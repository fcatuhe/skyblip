#include "products/skyblip_go/pages/nearby.h"

#include "core/model/aircraft.h"
#include "core/units/units.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go {

namespace {
constexpr int kSmallCellW = 6;
constexpr int kGlyphH = 7;
constexpr int kCountScale = 2;
constexpr int kRuleY = 32;
constexpr int kRelHundredsCap = 99;
constexpr int32_t kDistE1Cap = 999;

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int len, int scale) {
    canvas.draw_text(x_end - len * kSmallCellW * scale, y, text, true, scale);
}

void right_aligned_text(ui::Canvas& canvas, int x_end, int y, const char* text, int scale) {
    int n = 0;
    while (text[n]) n++;
    right_aligned(canvas, x_end, y, text, n, scale);
}

void draw_count(ui::Canvas& canvas, int n_heard) {
    char buf[8];
    const int n = format_uint(buf, static_cast<uint32_t>(n_heard), 1);
    buf[n] = 0;
    right_aligned(canvas, kNearbyRelEnd, kNearbyTitleY, buf, n, kCountScale);

    const int word_end = kNearbyRelEnd - n * kSmallCellW * kCountScale - kNearbyWordGap;
    right_aligned_text(canvas, word_end, kNearbyTitleY + kGlyphH * (kCountScale - 1), "TRAFFIC", 1);
}

int format_own_name(char* out, const char* callsign) {
    int n = 0;
    while (n < kNearbyOwnNameChars && callsign[n] != 0) {
        out[n] = callsign[n];
        n++;
    }
    return n;
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

    char buf[16];
    const int n = format_own_name(buf, snapshot.own_callsign);
    buf[n] = 0;
    canvas.draw_text(kNearbyIdX, kNearbyTitleY, buf, true, kNearbyTitleScale);

    const int address_x = kNearbyIdX + n * kSmallCellW * kNearbyTitleScale + kNearbyWordGap;
    draw_own_address(canvas, address_x, kNearbyTitleY + kGlyphH * (kNearbyTitleScale - 1),
                     snapshot.own_address, 1);
}

void banner(ui::Canvas& canvas, const char* text) {
    int n = 0;
    while (text[n]) n++;
    canvas.draw_text((kGlassW - n * kSmallCellW * kNearbyScale) / 2, kNearbyBannerY, text, true,
                     kNearbyScale);
}

void draw_header(ui::Canvas& canvas, const NearbySnapshot& snapshot) {
    draw_own_id(canvas, snapshot);
    draw_count(canvas, snapshot.n_heard);

    canvas.draw_text(kNearbyIdX, kNearbyHeadY, "SRC ID", true, 1);
    right_aligned_text(canvas, kNearbyDistEnd, kNearbyHeadY, "DIST", 1);
    right_aligned_text(canvas, kNearbyRelEnd, kNearbyHeadY, "ALT", 1);
    canvas.hline(kNearbyIdX, kRuleY, kNearbyRelEnd - kNearbyIdX, true);
}

int32_t rel_hundreds_of_feet(int32_t up_m) {
    const int32_t feet = to_feet(Metres(up_m)).v;
    const int32_t rounded = (feet >= 0 ? feet + 50 : feet - 50) / 100;
    if (rounded > kRelHundredsCap) return kRelHundredsCap;
    if (rounded < -kRelHundredsCap) return -kRelHundredsCap;
    return rounded;
}

int format_id(char* out, const traffic::RangeRow& row) {
    out[0] = model::source_letter(row.source);
    out[1] = ' ';
    if (row.callsign == nullptr || row.callsign[0] == 0)
        return 2 + format_hex(out + 2, row.address, 6);
    int n = 2;
    while (n < kNearbyIdChars && row.callsign[n - 2] != 0) {
        out[n] = row.callsign[n - 2];
        n++;
    }
    return n;
}

void draw_row(ui::Canvas& canvas, int y, const traffic::RangeRow& row, bool metric) {
    char buf[16];

    const int n_id = format_id(buf, row);
    buf[n_id] = 0;
    canvas.draw_text(kNearbyIdX, y, buf, true, kNearbyScale);

    const int32_t dist_e1 =
        metric ? div_round(row.ground_m, 100) : to_nm_e1(Metres(row.ground_m)).v;
    int n = dist_e1 > kDistE1Cap ? format_string(buf, "FAR")
                                 : format_uint(buf, static_cast<uint32_t>(dist_e1), 2, 1);
    buf[n] = 0;
    right_aligned(canvas, kNearbyDistEnd, y, buf, n, kNearbyScale);

    const int32_t rel = rel_hundreds_of_feet(row.up_m);
    n = format_int(buf, rel, 2, 0, rel == 0);
    buf[n] = 0;
    right_aligned(canvas, kNearbyRelEnd, y, buf, n, kNearbyScale);
}

}  // namespace

void draw_nearby(ui::Canvas& canvas, const NearbySnapshot& snapshot) {
    draw_header(canvas, snapshot);

    if (!snapshot.fix_valid) {
        banner(canvas, "NO FIX");
        return;
    }
    if (snapshot.n_rows == 0 || snapshot.rows == nullptr) {
        banner(canvas, "NO TRAFFIC");
        return;
    }

    const int rows = snapshot.n_rows < kNearbyRows ? snapshot.n_rows : kNearbyRows;
    const bool metric = snapshot.units == go::Units::Metric;
    for (int i = 0; i < rows; i++)
        draw_row(canvas, kNearbyFirstRowY + i * kNearbyLineH, snapshot.rows[i], metric);
}

}  // namespace skyblip::go
