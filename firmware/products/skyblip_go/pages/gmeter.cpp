#include "products/skyblip_go/pages/gmeter.h"

#include <algorithm>

#include "core/util/format.h"
#include "core/util/intmath.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
using ui::kCellWidth;
using ui::kCharacterHeight;
constexpr int kTitleY = 3;
constexpr int kRuleY = 21;

constexpr int kFieldCentreX = 82;
constexpr int kFieldCentreY = 82;
constexpr int kFieldHalfWidth = 52;
constexpr int kFieldHalfHeight = 52;
constexpr int kLateralPerG = 52;
constexpr int kNormalPerG = 16;
constexpr int kMarkerRadius = 3;
constexpr int kTickLength = 3;

constexpr int kStripCentreX = 164;
constexpr int kStripHalfWidth = 10;
constexpr int kStripTop = kFieldCentreY - kFieldHalfHeight;
constexpr int kStripBottom = kFieldCentreY + kFieldHalfHeight;
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

int32_t tenths(int32_t load_mg) { return div_round(load_mg, kMilliPerTenth); }

int format_g(char* out, int32_t load_mg) {
    const int32_t value = tenths(load_mg);
    const int written = format_signed(out, value, 1, 1, value != 0);
    out[written] = 0;
    return written;
}

int format_named_g(char* out, int32_t load_mg, char positive, char negative) {
    const int32_t value = tenths(load_mg);
    int written = 0;
    if (value != 0) out[written++] = value < 0 ? negative : positive;
    written +=
        format_unsigned(out + written, static_cast<uint32_t>(value < 0 ? -value : value), 1, 1);
    out[written] = 0;
    return written;
}

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int length,
                   int scale = 1) {
    canvas.draw_text(x_end - length * kCellWidth * scale, y, text, true, scale);
}

void centred(ui::Canvas& canvas, int centre_x, int y, const char* text, int length) {
    canvas.draw_text(centre_x - (length * kCellWidth - 1) / 2, y, text, true, 1);
}

void value_at(ui::Canvas& canvas, int x_end, int y, int32_t load_mg, int scale = 1) {
    char buffer[8];
    const int written = format_g(buffer, load_mg);
    right_aligned(canvas, x_end, y, buffer, written, scale);
}

void named_at(ui::Canvas& canvas, int x_end, int y, int32_t load_mg, char positive, char negative,
              int scale = 1) {
    char buffer[12];
    const int written = format_named_g(buffer, load_mg, positive, negative);
    right_aligned(canvas, x_end, y, buffer, written, scale);
}

int thrown_down_y(int32_t load_mg) {
    const int32_t from_level = load_mg - kLevelFlightMg;
    const int offset = static_cast<int>((from_level * kNormalPerG) / kMilliPerG);
    return kFieldCentreY + std::clamp<int32_t>(offset, -kFieldHalfHeight, kFieldHalfHeight);
}

int lateral_x(int32_t load_mg) {
    const int offset = static_cast<int>((load_mg * kLateralPerG) / kMilliPerG);
    return kFieldCentreX + std::clamp<int32_t>(offset, -kFieldHalfWidth, kFieldHalfWidth);
}

int thrown_forward_y(int32_t accelerating_mg) {
    const int offset = static_cast<int>((accelerating_mg * kThrownPerG) / kMilliPerG);
    return kFieldCentreY + std::clamp<int32_t>(offset, -kThrownPerG, kThrownPerG);
}

void field_scale(ui::Canvas& canvas) {
    canvas.rectangle(kFieldCentreX - kFieldHalfWidth, kFieldCentreY - kFieldHalfHeight,
                     2 * kFieldHalfWidth + 1, 2 * kFieldHalfHeight + 1, true);
    canvas.horizontal_line(kFieldCentreX - kFieldHalfWidth, kFieldCentreY, 2 * kFieldHalfWidth + 1,
                           true);
    canvas.vertical_line(kFieldCentreX, kFieldCentreY - kFieldHalfHeight, 2 * kFieldHalfHeight + 1,
                         true);

    for (int32_t whole_g = -2; whole_g <= 4; whole_g++) {
        if (whole_g == 1) continue;
        canvas.horizontal_line(kFieldCentreX - kTickLength, thrown_down_y(whole_g * kMilliPerG),
                               2 * kTickLength + 1, true);
    }
    for (int32_t half_g = -2; half_g <= 2; half_g++) {
        if (half_g == 0) continue;
        canvas.vertical_line(lateral_x(half_g * kMilliPerG / 2), kFieldCentreY - kTickLength,
                             2 * kTickLength + 1, true);
    }
}

void field_reading(ui::Canvas& canvas, const GMeterSnapshot& snapshot) {
    const int left = lateral_x(snapshot.least.lateral_mg);
    const int right = lateral_x(snapshot.most.lateral_mg);
    const int top = thrown_down_y(snapshot.least.normal_mg);
    const int bottom = thrown_down_y(snapshot.most.normal_mg);
    canvas.rectangle(left, top, right - left + 1, bottom - top + 1, true);
    canvas.circle(lateral_x(snapshot.now.lateral_mg), thrown_down_y(snapshot.now.normal_mg),
                  kMarkerRadius, true, true);
}

void strip_scale(ui::Canvas& canvas) {
    canvas.rectangle(kStripCentreX - kStripHalfWidth, kStripTop, 2 * kStripHalfWidth + 1,
                     kStripBottom - kStripTop + 1, true);
    canvas.horizontal_line(kStripCentreX - kStripHalfWidth, kFieldCentreY, 2 * kStripHalfWidth + 1,
                           true);
    centred(canvas, kStripCentreX, kStripTop + kStripLabelInset, "DEC", 3);
    centred(canvas, kStripCentreX, kStripBottom - kStripLabelInset - kCharacterHeight, "ACC", 3);
}

void strip_reading(ui::Canvas& canvas, const GMeterSnapshot& snapshot) {
    const int inset = kStripHalfWidth - 3;
    canvas.horizontal_line(kStripCentreX - inset, thrown_forward_y(snapshot.most.longitudinal_mg),
                           2 * inset + 1, true);
    canvas.horizontal_line(kStripCentreX - inset, thrown_forward_y(snapshot.least.longitudinal_mg),
                           2 * inset + 1, true);
    canvas.rectangle(kStripCentreX - inset, thrown_forward_y(snapshot.now.longitudinal_mg) - 1,
                     2 * inset + 1, 3, true, true);
}

void row_label(ui::Canvas& canvas, int y, const char* label, bool valid) {
    canvas.draw_text(kLeft, y + kPeakDrop, label, true, 1);
    if (!valid) canvas.draw_text(kNowEnd - 4 * kCellWidth, y + kPeakDrop, "----", true, 1);
}

}  // namespace

void draw_gmeter(ui::Canvas& canvas, const GMeterSnapshot& snapshot) {
    canvas.clear(true);
    canvas.draw_text(kLeft, kTitleY, "G METER", true, 2);
    canvas.horizontal_line(kLeft, kRuleY, kGlassWidth - 2 * kLeft, true);

    if (!snapshot.inclinometer_fitted) {
        canvas.draw_text(kLeft, kFieldCentreY - kCharacterHeight / 2, "NO SENSOR", true, 1);
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
