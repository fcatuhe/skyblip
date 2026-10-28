#include "products/skyblip_go/pages/sixpack.h"

#include <algorithm>

#include "core/flight/slip.h"
#include "core/units/units.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/pages/page.h"
#include "ui/widgets/skyship.h"

namespace skyblip::go {

namespace {

constexpr int kDialRadius = 31;
constexpr int kCentreX[3] = {34, 100, 166};
constexpr int kCentreY[2] = {67, 133};
constexpr int kValueScale = 2;
constexpr int kValueGap = 7;
constexpr int kTitleGap = 4;
constexpr int kTickLength = 4;
constexpr int kTicks = 12;
constexpr int kAltitudeTicks = 10;  // one mark per 100 ft, so the hands line up with them
// Card letters stay upright whatever the card does: at 5x7 a turned glyph is a
// staircase of its own stroke width, and unreadable beats authentic.
constexpr int kCardRadius = kDialRadius - 7;
constexpr int kIndexTableLength = 5;
constexpr int kNeedleLength = kDialRadius - 6;
constexpr int kFaceRadius = kDialRadius - 1;
constexpr int kDeadInnerRadius = kDialRadius - 7;
constexpr int kShortNeedleLength = kDialRadius - 15;
constexpr int kHubRadius = 2;
using ui::kCellWidth;
using ui::kCharacterHeight;

constexpr int32_t kAsiFullScaleKt = 175;
constexpr int32_t kAsiFullScaleKmh = 315;
constexpr int32_t kAsiSpanDeg = 315;
constexpr int32_t kAsiZeroDeg = 180;
constexpr int kAsiTicks = 8;
constexpr int32_t kAltitudeHundredsPerTurn = 1000;
constexpr int32_t kAltitudeThousandsPerTurn = 10000;
constexpr int32_t kVsiFullScaleFpm = 2000;
constexpr int32_t kVsiKneeFpm = 1000;
constexpr int32_t kVsiKneeDeg = 90;
constexpr int32_t kVsiOuterDeg = 80;
constexpr int32_t kVsiMarkFpm = 500;
constexpr int32_t kVsiZeroDeg = -90;
constexpr int32_t kBankLimitDeg = 60;
constexpr int32_t kPitchFullScaleDeg = 20;
constexpr int32_t kStandardRateDps = 3;
constexpr int32_t kRateMarkDeg = 20;
constexpr int32_t kRateFullScaleDeg = 45;
constexpr int kWingHalfSpan = kDialRadius - 9;
constexpr int kFinLength = 6;
constexpr int kTailUp = 3;
constexpr int kTailHalfSpan = 8;
constexpr int kBallY = 18;
constexpr int kBallRadius = 4;
constexpr int kCageHalfWidth = kBallRadius + 2;
constexpr int kCageHalfHeight = 5;
constexpr int kBallTravel = 16;
constexpr int kFuselageRadius = 3;
constexpr int kReferenceInnerRadius = 7;
constexpr int kReferenceOuterRadius = 20;
constexpr int kReferenceDotRadius = 2;

// Rounded, sign-symmetric projection of a radius onto a cordic axis: plain
// integer division truncates toward zero and pulls both ends of a mark inward.
int radial_half(int32_t radius_in_halves, int32_t axis) {
    const int32_t product = radius_in_halves * axis;
    return (product + (product < 0 ? -kTrigOne : kTrigOne)) / (2 * kTrigOne);
}

int radial(int32_t radius, int32_t axis) { return radial_half(2 * radius, axis); }

int value_y(int row) {
    return row == 0 ? kCentreY[0] - kDialRadius - kValueGap - kCharacterHeight * kValueScale
                    : kCentreY[1] + kDialRadius + kValueGap;
}

int title_y(int row) {
    return row == 0 ? value_y(0) - kTitleGap - kCharacterHeight
                    : value_y(1) + kCharacterHeight * kValueScale + kTitleGap;
}

void centred_text(ui::Canvas& canvas, int centre_x, int y, const char* text, int scale = 1) {
    int count = 0;
    while (text[count]) count++;
    canvas.draw_text(centre_x - (count * kCellWidth * scale) / 2, y, text, true, scale);
}

void value_text(ui::Canvas& canvas, int centre_x, int row, const char* text) {
    centred_text(canvas, centre_x, value_y(row), text, kValueScale);
}

void centred_value(ui::Canvas& canvas, int centre_x, int row, bool valid, int32_t value,
                   bool plus_sign, uint8_t min_digits = 1) {
    if (!valid) {
        value_text(canvas, centre_x, row, "---");
        return;
    }
    char text[12];
    int written = format_signed(text, value, min_digits, 0, plus_sign && value != 0);
    text[written] = 0;
    value_text(canvas, centre_x, row, text);
}

// Marks are stepped along the true radius in half-pixels rather than drawn as a
// line between two rounded end points: over 4 px, rounding both ends tilts the
// mark by degrees, which is what made the scales look bent.
void radial_mark(ui::Canvas& canvas, int centre_x, int centre_y, int16_t angle, int from_radius,
                 int to_radius) {
    const int32_t unit_x = sine(angle), unit_y = cosine(angle);
    for (int half = 2 * from_radius; half <= 2 * to_radius; half++) {
        canvas.set_pixel(centre_x + radial_half(half, unit_x), centre_y - radial_half(half, unit_y),
                         true);
    }
}

void tick(ui::Canvas& canvas, int centre_x, int centre_y, int16_t angle, int length = kTickLength) {
    radial_mark(canvas, centre_x, centre_y, angle, kDialRadius - length, kFaceRadius);
}

void dial(ui::Canvas& canvas, int centre_x, int row, const char* title, int ticks = kTicks) {
    const int centre_y = kCentreY[row];
    canvas.circle(centre_x, centre_y, kDialRadius, true);
    for (int i = 0; i < ticks; i++)
        tick(canvas, centre_x, centre_y, angle16_of_degrees(i * (360 / ticks)));
    centred_text(canvas, centre_x, title_y(row), title);
}

void dead_sector(ui::Canvas& canvas, int centre_x, int centre_y, int32_t from_deg, int32_t to_deg) {
    const int16_t from = angle16_of_degrees(from_deg), to = angle16_of_degrees(to_deg);
    for (int delta_y = -kDialRadius; delta_y <= kDialRadius; delta_y++) {
        for (int delta_x = -kDialRadius; delta_x <= kDialRadius; delta_x++) {
            if (((delta_x + delta_y) & 1) != 0) continue;
            const int radius_squared = delta_x * delta_x + delta_y * delta_y;
            if (radius_squared > kDialRadius * kDialRadius ||
                radius_squared <= kDeadInnerRadius * kDeadInnerRadius)
                continue;
            const int16_t angle = arctangent(delta_x, -delta_y);
            if (from <= to ? (angle < from || angle > to) : (angle < from && angle > to)) continue;
            canvas.set_pixel(centre_x + delta_x, centre_y + delta_y, true);
        }
    }
}

int32_t vsi_deg(int32_t climb_fpm) {
    const int32_t clamped = std::clamp<int32_t>(climb_fpm, -kVsiFullScaleFpm, kVsiFullScaleFpm);
    const int32_t rate = clamped < 0 ? -clamped : clamped;
    const int32_t inner = (rate < kVsiKneeFpm ? rate : kVsiKneeFpm) * kVsiKneeDeg / kVsiKneeFpm;
    const int32_t outer = (rate > kVsiKneeFpm ? rate - kVsiKneeFpm : 0) * kVsiOuterDeg /
                          (kVsiFullScaleFpm - kVsiKneeFpm);
    return clamped < 0 ? kVsiZeroDeg - inner - outer : kVsiZeroDeg + inner + outer;
}

void vsi_face(ui::Canvas& canvas, int centre_x, int centre_y) {
    dead_sector(canvas, centre_x, centre_y, vsi_deg(kVsiFullScaleFpm), vsi_deg(-kVsiFullScaleFpm));
    for (int32_t climb_fpm = -kVsiFullScaleFpm; climb_fpm <= kVsiFullScaleFpm;
         climb_fpm += kVsiMarkFpm)
        tick(canvas, centre_x, centre_y, angle16_of_degrees(vsi_deg(climb_fpm)),
             climb_fpm % kVsiKneeFpm == 0 ? kTickLength + 3 : kTickLength);
}

void needle(ui::Canvas& canvas, int centre_x, int centre_y, int32_t angle_deg, int length,
            bool thick = false, bool cleared = false) {
    const int16_t angle = angle16_of_degrees(angle_deg);
    const int32_t unit_x = sine(angle), unit_y = cosine(angle);
    const int tip_x = centre_x + radial(length, unit_x);
    const int tip_y = centre_y - radial(length, unit_y);
    if (cleared)
        for (int offset_x = -1; offset_x <= 1; offset_x++)
            for (int offset_y = -1; offset_y <= 1; offset_y++)
                canvas.line(centre_x + offset_x, centre_y + offset_y, tip_x + offset_x,
                            tip_y + offset_y, false);
    canvas.line(centre_x, centre_y, tip_x, tip_y, true);
    if (thick) {  // the altimeter's thousands hand: short and broad, hundreds long and fine
        canvas.line(centre_x + 1, centre_y, tip_x + 1, tip_y, true);
        canvas.line(centre_x, centre_y + 1, tip_x, tip_y + 1, true);
    }
    canvas.circle(centre_x, centre_y, kHubRadius, true, true);
}

void horizon(ui::Canvas& canvas, int centre_x, int centre_y, int32_t pitch_deg, int32_t bank_deg) {
    const int32_t pitch_offset =
        (std::clamp<int32_t>(pitch_deg, -kPitchFullScaleDeg, kPitchFullScaleDeg) * kDialRadius) /
        kPitchFullScaleDeg;
    const int16_t angle =
        angle16_of_degrees(std::clamp<int32_t>(bank_deg, -kBankLimitDeg, kBankLimitDeg));
    const int32_t unit_x = sine(angle), unit_y = cosine(angle);
    for (int delta_x = -kDialRadius + 1; delta_x < kDialRadius; delta_x++) {
        const int chord = static_cast<int>(square_root<uint32_t>(
            static_cast<uint32_t>(kDialRadius * kDialRadius - delta_x * delta_x)));
        const int32_t horizon_y =
            std::clamp<int32_t>(pitch_offset - (delta_x * unit_x) / unit_y, -chord, chord);
        const int x = centre_x + delta_x;
        for (int y = centre_y + static_cast<int>(horizon_y); y <= centre_y + chord; y++) {
            if (((x + y) & 1) == 0) canvas.set_pixel(x, y, true);
        }
    }
    for (int i = kReferenceInnerRadius; i <= kReferenceOuterRadius;
         i++) {  // solid over sky or ground
        canvas.set_pixel(centre_x - i, centre_y, true);
        canvas.set_pixel(centre_x + i, centre_y, true);
    }
    canvas.circle(centre_x, centre_y, kReferenceDotRadius, true, true);
}

int32_t rate_deflection_deg(int32_t turn_cdps) {
    return std::clamp<int32_t>((turn_cdps * kRateMarkDeg) / (kStandardRateDps * 100),
                               -kRateFullScaleDeg, kRateFullScaleDeg);
}

void inclinometer(ui::Canvas& canvas, int centre_x, int centre_y, const SixPackSnapshot& snapshot) {
    if (!snapshot.inclinometer_fitted) return;
    const int y = centre_y + kBallY;
    for (int side = -1; side <= 1; side += 2)
        canvas.vertical_line(centre_x + side * kCageHalfWidth, y - kCageHalfHeight,
                             2 * kCageHalfHeight + 1, true);
    if (!snapshot.lateral_valid) return;
    const int32_t swing = std::clamp<int32_t>(snapshot.lateral_mg, -flight::kSlipFullScaleMg,
                                              flight::kSlipFullScaleMg);
    canvas.circle(centre_x + (swing * kBallTravel) / flight::kSlipFullScaleMg, y, kBallRadius, true,
                  true);
}

void turn_coordinator(ui::Canvas& canvas, int centre_x, int centre_y, int32_t turn_cdps) {
    const int16_t angle = angle16_of_degrees(rate_deflection_deg(turn_cdps));
    const int32_t unit_x = sine(angle), unit_y = cosine(angle);
    const int wing_x = radial(kWingHalfSpan, unit_y);
    const int wing_y = radial(kWingHalfSpan, unit_x);
    const int tail_up_x = radial(kTailUp, unit_x);
    const int tail_up_y = radial(kTailUp, unit_y);
    const int tail_x = radial(kTailHalfSpan, unit_y);
    const int tail_y = radial(kTailHalfSpan, unit_x);
    canvas.line(centre_x - wing_x, centre_y - wing_y, centre_x + wing_x, centre_y + wing_y, true);
    canvas.line(centre_x, centre_y, centre_x + radial(kFinLength, unit_x),
                centre_y - radial(kFinLength, unit_y), true);
    canvas.line(centre_x + tail_up_x - tail_x, centre_y - tail_up_y - tail_y,
                centre_x + tail_up_x + tail_x, centre_y - tail_up_y + tail_y, true);
    canvas.circle(centre_x, centre_y, kFuselageRadius, true, true);
    for (int side = -1; side <= 1; side += 2) {
        tick(canvas, centre_x, centre_y, angle16_of_degrees(90 * side));
        tick(canvas, centre_x, centre_y, angle16_of_degrees(side * (90 - kRateMarkDeg) + 180));
    }
}

// The card rotates so the flown track sits under the fixed index at the top,
// which is what makes N/E/S/W read as directions rather than as labels.
void heading_card(ui::Canvas& canvas, int centre_x, int centre_y, int32_t track_deg) {
    static const char kCardinal[4] = {'N', 'E', 'S', 'W'};
    for (int i = 0; i < kTicks; i++) {
        const int32_t bearing = i * (360 / kTicks);
        const int16_t angle = angle16_of_degrees(bearing - track_deg);
        if (bearing % 90 != 0) {
            tick(canvas, centre_x, centre_y, angle);
            continue;
        }
        canvas.draw_character(centre_x - kCellWidth / 2 + radial(kCardRadius, sine(angle)),
                              centre_y - kCharacterHeight / 2 - radial(kCardRadius, cosine(angle)),
                              kCardinal[bearing / 90], true);
    }
    canvas.rectangle(centre_x - 1, centre_y - kDialRadius, 3, kIndexTableLength, true,
                     true);  // the lubber index, on the case
    ui::draw_skyship(canvas, centre_x, centre_y);
}

int32_t flight_path_deg(int32_t climb_fpm, int32_t speed_kt) {
    if (speed_kt <= 0) return 0;
    return (static_cast<int32_t>(arctangent(climb_fpm * 10, speed_kt * 1013)) * 360) /
           kAngle16PerTurn;
}

// Coordinated turn: tan(bank) = omega * V / g, which in deg/s and knots is
// turn_dps * kt / 1093, and no sensor on this device measures a bank.
int32_t inferred_bank_deg(int32_t turn_dps, int32_t speed_kt) {
    if (speed_kt <= 0) return 0;
    return (static_cast<int32_t>(arctangent(turn_dps * speed_kt, 1093)) * 360) / kAngle16PerTurn;
}

// INFO: fc 11oct26 on the cable the divider reads the USB rail, so the dial reads it as that
void battery_dial(ui::Canvas& canvas, int centre_x, int row, const SixPackSnapshot& snapshot) {
    char text[8];
    int written = 0;
    if (snapshot.charging) {
        dial(canvas, centre_x, row, "CHARGE V", 0);
        written = format_unsigned(
            text, static_cast<uint32_t>(divide_rounded<int32_t>(snapshot.battery_mv, 10)), 1, 2);
    } else {
        dial(canvas, centre_x, row, "BATTERY", 0);
        written = snapshot.battery_valid ? format_unsigned(text, snapshot.battery_percent)
                                         : format_string(text, "--");
        written += format_string(text + written, "%");
    }
    text[written] = 0;
    value_text(canvas, centre_x, row, text);
}

}  // namespace

void draw_sixpack(ui::Canvas& canvas, const SixPackSnapshot& snapshot) {
    canvas.clear(true);

    const int32_t speed_kt =
        snapshot.fix_valid ? std::clamp<int32_t>(snapshot.speed_kt, 0, 999) : 0;
    const int32_t altitude_ft = snapshot.fix_valid ? snapshot.altitude_ft : 0;
    const int32_t climb_fpm = snapshot.climb_valid ? snapshot.climb_fpm : 0;
    const int32_t turn_cdps = snapshot.fix_valid ? snapshot.turn_cdps : 0;
    const int32_t turn_dps = (turn_cdps + (turn_cdps < 0 ? -50 : 50)) / 100;
    const int32_t track = snapshot.fix_valid ? snapshot.track_deg % 360 : 0;
    const int32_t bank = inferred_bank_deg(turn_dps, speed_kt);
    const int32_t pitch = flight_path_deg(climb_fpm, speed_kt);

    const bool metric = snapshot.units == go::Units::Metric;
    const int32_t speed = metric ? (speed_kt * 1852 + 500) / 1000 : speed_kt;
    const int32_t speed_full = metric ? kAsiFullScaleKmh : kAsiFullScaleKt;

    dial(canvas, kCentreX[0], 0, metric ? "GS KM/H" : "GS KT", kAsiTicks);
    dead_sector(canvas, kCentreX[0], kCentreY[0], kAsiZeroDeg + kAsiSpanDeg, kAsiZeroDeg);
    needle(canvas, kCentreX[0], kCentreY[0],
           kAsiZeroDeg + (std::clamp<int32_t>(speed, 0, speed_full) * kAsiSpanDeg) / speed_full,
           kNeedleLength,
           /*thick=*/false, /*cleared=*/true);
    centred_value(canvas, kCentreX[0], 0, snapshot.fix_valid, speed, false);

    battery_dial(canvas, kCentreX[1], 0, snapshot);
    horizon(canvas, kCentreX[1], kCentreY[0], pitch, bank);

    dial(canvas, kCentreX[2], 0, "ALT FT", kAltitudeTicks);
    const int32_t on_scale = altitude_ft < 0 ? 0 : altitude_ft;
    needle(canvas, kCentreX[2], kCentreY[0],
           ((on_scale % kAltitudeThousandsPerTurn) * 360) / kAltitudeThousandsPerTurn,
           kShortNeedleLength,
           /*thick=*/true);
    needle(canvas, kCentreX[2], kCentreY[0],
           ((on_scale % kAltitudeHundredsPerTurn) * 360) / kAltitudeHundredsPerTurn, kNeedleLength);
    centred_value(canvas, kCentreX[2], 0, snapshot.fix_valid, altitude_ft, false);

    dial(canvas, kCentreX[0], 1, "TURN D/S", 0);
    turn_coordinator(canvas, kCentreX[0], kCentreY[1], turn_cdps);
    inclinometer(canvas, kCentreX[0], kCentreY[1], snapshot);
    centred_value(canvas, kCentreX[0], 1, snapshot.fix_valid, turn_dps, true);

    dial(canvas, kCentreX[1], 1, "TRK", 0);
    heading_card(canvas, kCentreX[1], kCentreY[1], track);
    centred_value(canvas, kCentreX[1], 1, snapshot.fix_valid, track == 0 ? 360 : track, false, 3);

    dial(canvas, kCentreX[2], 1, "VS FPM", 0);
    vsi_face(canvas, kCentreX[2], kCentreY[1]);
    needle(canvas, kCentreX[2], kCentreY[1], vsi_deg(climb_fpm), kNeedleLength, /*thick=*/false,
           /*cleared=*/true);
    centred_value(canvas, kCentreX[2], 1, snapshot.climb_valid, climb_figure_fpm(climb_fpm), true);
}

}  // namespace skyblip::go
