#include "products/skyblip_go/pages/gmeter.h"

#include "core/util/format.h"
#include "core/util/intmath.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
using ui::kCellW;
using ui::kCharH;
constexpr int kTitleY = 3;
constexpr int kRuleY = 21;

constexpr int kFieldCx = 82;
constexpr int kFieldCy = 82;
constexpr int kFieldHalfW = 52;
constexpr int kFieldHalfH = 52;
constexpr int kLateralPerG = 52;
constexpr int kNormalPerG = 16;
constexpr int kMarkerR = 3;
constexpr int kTickLen = 3;

constexpr int kStripCx = 164;
constexpr int kStripHalfW = 10;
constexpr int kStripTop = kFieldCy - kFieldHalfH;
constexpr int kStripBottom = kFieldCy + kFieldHalfH;
constexpr int kThrownPerG = 36;
constexpr int kStripLabelInset = 2;

constexpr int kNormalRowY = 140;
constexpr int kLateralRowY = 160;
constexpr int kLongitudinalRowY = 180;
constexpr int kPeakDrop = 4;
constexpr int kNowScale = 2;

constexpr int kNowEnd = 116;
constexpr int kLeastEnd = 156;
constexpr int kMostEnd = 194;

constexpr int32_t kMilliPerG = 1000;
constexpr int32_t kMilliPerTenth = 100;
constexpr int16_t kLevelFlightMg = flight::kLevelFlightMg;

int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

int32_t tenths(int32_t mg) { return div_round(mg, kMilliPerTenth); }

int format_g(char* out, int32_t mg) {
    const int32_t value = tenths(mg);
    const int n = format_int(out, value, 1, 1, value != 0);
    out[n] = 0;
    return n;
}

int format_named_g(char* out, int32_t mg, char positive, char negative) {
    const int32_t value = tenths(mg);
    int written = 0;
    if (value != 0) out[written++] = value < 0 ? negative : positive;
    written += format_uint(out + written, static_cast<uint32_t>(value < 0 ? -value : value), 1, 1);
    out[written] = 0;
    return written;
}

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int len, int scale = 1) {
    canvas.draw_text(x_end - len * kCellW * scale, y, text, true, scale);
}

void centred(ui::Canvas& canvas, int cx, int y, const char* text, int len) {
    canvas.draw_text(cx - (len * kCellW - 1) / 2, y, text, true, 1);
}

void value_at(ui::Canvas& canvas, int x_end, int y, int32_t mg, int scale = 1) {
    char buf[8];
    const int n = format_g(buf, mg);
    right_aligned(canvas, x_end, y, buf, n, scale);
}

void named_at(ui::Canvas& canvas, int x_end, int y, int32_t mg, char positive, char negative,
              int scale = 1) {
    char buf[12];
    const int n = format_named_g(buf, mg, positive, negative);
    right_aligned(canvas, x_end, y, buf, n, scale);
}

int thrown_down_y(int32_t mg) {
    const int32_t from_level = mg - kLevelFlightMg;
    const int offset = static_cast<int>((from_level * kNormalPerG) / kMilliPerG);
    return kFieldCy + clampi(offset, -kFieldHalfH, kFieldHalfH);
}

int lateral_x(int32_t mg) {
    const int offset = static_cast<int>((mg * kLateralPerG) / kMilliPerG);
    return kFieldCx + clampi(offset, -kFieldHalfW, kFieldHalfW);
}

int thrown_forward_y(int32_t accelerating_mg) {
    const int offset = static_cast<int>((accelerating_mg * kThrownPerG) / kMilliPerG);
    return kFieldCy + clampi(offset, -kThrownPerG, kThrownPerG);
}

void field_scale(ui::Canvas& canvas) {
    canvas.rect(kFieldCx - kFieldHalfW, kFieldCy - kFieldHalfH, 2 * kFieldHalfW + 1,
                2 * kFieldHalfH + 1, true);
    canvas.hline(kFieldCx - kFieldHalfW, kFieldCy, 2 * kFieldHalfW + 1, true);
    canvas.vline(kFieldCx, kFieldCy - kFieldHalfH, 2 * kFieldHalfH + 1, true);

    for (int32_t g = -2; g <= 4; g++) {
        if (g == 1) continue;
        canvas.hline(kFieldCx - kTickLen, thrown_down_y(g * kMilliPerG), 2 * kTickLen + 1, true);
    }
    for (int32_t half_g = -2; half_g <= 2; half_g++) {
        if (half_g == 0) continue;
        canvas.vline(lateral_x(half_g * kMilliPerG / 2), kFieldCy - kTickLen, 2 * kTickLen + 1,
                     true);
    }
}

void field_reading(ui::Canvas& canvas, const GMeterSnapshot& snapshot) {
    const int left = lateral_x(snapshot.least.lateral_mg);
    const int right = lateral_x(snapshot.most.lateral_mg);
    const int top = thrown_down_y(snapshot.least.normal_mg);
    const int bottom = thrown_down_y(snapshot.most.normal_mg);
    canvas.rect(left, top, right - left + 1, bottom - top + 1, true);
    canvas.circle(lateral_x(snapshot.now.lateral_mg), thrown_down_y(snapshot.now.normal_mg),
                  kMarkerR, true, true);
}

void strip_scale(ui::Canvas& canvas) {
    canvas.rect(kStripCx - kStripHalfW, kStripTop, 2 * kStripHalfW + 1,
                kStripBottom - kStripTop + 1, true);
    canvas.hline(kStripCx - kStripHalfW, kFieldCy, 2 * kStripHalfW + 1, true);
    centred(canvas, kStripCx, kStripTop + kStripLabelInset, "DEC", 3);
    centred(canvas, kStripCx, kStripBottom - kStripLabelInset - kCharH, "ACC", 3);
}

void strip_reading(ui::Canvas& canvas, const GMeterSnapshot& snapshot) {
    const int inset = kStripHalfW - 3;
    canvas.hline(kStripCx - inset, thrown_forward_y(snapshot.most.longitudinal_mg), 2 * inset + 1,
                 true);
    canvas.hline(kStripCx - inset, thrown_forward_y(snapshot.least.longitudinal_mg), 2 * inset + 1,
                 true);
    canvas.rect(kStripCx - inset, thrown_forward_y(snapshot.now.longitudinal_mg) - 1, 2 * inset + 1,
                3, true, true);
}

void row_label(ui::Canvas& canvas, int y, const char* label, bool valid) {
    canvas.draw_text(kLeft, y + kPeakDrop, label, true, 1);
    if (!valid) canvas.draw_text(kNowEnd - 4 * kCellW, y + kPeakDrop, "----", true, 1);
}

}  // namespace

void draw_gmeter(ui::Canvas& canvas, const GMeterSnapshot& snapshot) {
    canvas.clear(true);
    canvas.draw_text(kLeft, kTitleY, "G METER", true, 2);
    canvas.hline(kLeft, kRuleY, kGlassWidth - 2 * kLeft, true);

    if (!snapshot.inclinometer_fitted) {
        canvas.draw_text(kLeft, kFieldCy - kCharH / 2, "NO SENSOR", true, 1);
        return;
    }

    field_scale(canvas);
    strip_scale(canvas);
    if (snapshot.gload_valid) {
        field_reading(canvas, snapshot);
        strip_reading(canvas, snapshot);
    }

    row_label(canvas, kNormalRowY, "G", snapshot.gload_valid);
    row_label(canvas, kLateralRowY, "SIDE", snapshot.gload_valid);
    row_label(canvas, kLongitudinalRowY, "ACCEL", snapshot.gload_valid);
    if (!snapshot.gload_valid) return;

    value_at(canvas, kNowEnd, kNormalRowY, snapshot.now.normal_mg, kNowScale);
    value_at(canvas, kLeastEnd, kNormalRowY + kPeakDrop, snapshot.least.normal_mg);
    value_at(canvas, kMostEnd, kNormalRowY + kPeakDrop, snapshot.most.normal_mg);

    named_at(canvas, kNowEnd, kLateralRowY, snapshot.now.lateral_mg, 'R', 'L', kNowScale);
    named_at(canvas, kLeastEnd, kLateralRowY + kPeakDrop, snapshot.least.lateral_mg, 'R', 'L');
    named_at(canvas, kMostEnd, kLateralRowY + kPeakDrop, snapshot.most.lateral_mg, 'R', 'L');

    named_at(canvas, kNowEnd, kLongitudinalRowY, snapshot.now.longitudinal_mg, 'A', 'D', kNowScale);
    named_at(canvas, kLeastEnd, kLongitudinalRowY + kPeakDrop, snapshot.least.longitudinal_mg, 'A',
             'D');
    named_at(canvas, kMostEnd, kLongitudinalRowY + kPeakDrop, snapshot.most.longitudinal_mg, 'A',
             'D');
}

}  // namespace skyblip::go
