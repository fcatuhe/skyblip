#include "products/skyblip_go/pages/sixpack.h"

#include "core/flight/slip.h"
#include "core/units/units.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/pages/page.h"
#include "ui/widgets/skyship.h"

namespace skyblip::go {

namespace {

constexpr int kDialR = 31;
constexpr int kCx[3] = {34, 100, 166};
constexpr int kCy[2] = {67, 133};
constexpr int kValueScale = 2;
constexpr int kValueGap = 7;
constexpr int kTitleGap = 4;
constexpr int kTickLen = 4;
constexpr int kTicks = 12;
constexpr int kAltTicks = 10;  // one mark per 100 ft, so the hands line up with them
// Card letters stay upright whatever the card does: at 5x7 a turned glyph is a
// staircase of its own stroke width, and unreadable beats authentic.
constexpr int kCardR = kDialR - 7;
constexpr int kIndexTabLen = 5;
constexpr int kNeedleR = kDialR - 6;
constexpr int kFaceR = kDialR - 1;
constexpr int kDeadInnerR = kDialR - 7;
constexpr int kShortNeedleR = kDialR - 15;
constexpr int kHubR = 2;
using ui::kCellW;
using ui::kCharH;

constexpr int32_t kAsiFullScaleKt = 175;
constexpr int32_t kAsiFullScaleKmh = 315;
constexpr int32_t kAsiSpanDeg = 315;
constexpr int32_t kAsiZeroDeg = 180;
constexpr int kAsiTicks = 8;
constexpr int32_t kAltHundredsPerTurn = 1000;
constexpr int32_t kAltThousandsPerTurn = 10000;
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
constexpr int kWingHalf = kDialR - 9;
constexpr int kFinLen = 6;
constexpr int kTailUp = 3;
constexpr int kTailHalf = 8;
constexpr int kBallY = 18;
constexpr int kBallR = 4;
constexpr int kCageHalf = kBallR + 2;
constexpr int kCageHalfH = 5;
constexpr int kBallTravel = 16;
constexpr int kFuselageR = 3;
constexpr int kReferenceInnerR = 7;
constexpr int kReferenceOuterR = 20;
constexpr int kReferenceDotR = 2;

int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Rounded, sign-symmetric projection of a radius onto a cordic axis: plain
// integer division truncates toward zero and pulls both ends of a mark inward.
int radial_half(int32_t r_half, int32_t q14) {
    const int32_t v = r_half * q14;
    return static_cast<int>((v + (v < 0 ? -kTrigOne : kTrigOne)) / (2 * kTrigOne));
}

int radial(int32_t r, int32_t q14) { return radial_half(2 * r, q14); }

int value_y(int row) {
    return row == 0 ? kCy[0] - kDialR - kValueGap - kCharH * kValueScale
                    : kCy[1] + kDialR + kValueGap;
}

int title_y(int row) {
    return row == 0 ? value_y(0) - kTitleGap - kCharH
                    : value_y(1) + kCharH * kValueScale + kTitleGap;
}

void centred_text(ui::Canvas& canvas, int cx, int y, const char* s, int scale = 1) {
    int n = 0;
    while (s[n]) n++;
    canvas.draw_text(cx - (n * kCellW * scale) / 2, y, s, true, scale);
}

void value_text(ui::Canvas& canvas, int cx, int row, const char* s) {
    centred_text(canvas, cx, value_y(row), s, kValueScale);
}

void centred_value(ui::Canvas& canvas, int cx, int row, bool have, int32_t v, bool plus_sign,
                   uint8_t min_digits = 1) {
    if (!have) {
        value_text(canvas, cx, row, "---");
        return;
    }
    char text[12];
    int written = format_int(text, v, min_digits, 0, plus_sign && v != 0);
    text[written] = 0;
    value_text(canvas, cx, row, text);
}

// Marks are stepped along the true radius in half-pixels rather than drawn as a
// line between two rounded end points: over 4 px, rounding both ends tilts the
// mark by degrees, which is what made the scales look bent.
void radial_mark(ui::Canvas& canvas, int cx, int cy, int16_t a, int from_r, int to_r) {
    const int32_t s = isin(a), c = icos(a);
    for (int half = 2 * from_r; half <= 2 * to_r; half++) {
        canvas.set_pixel(cx + radial_half(half, s), cy - radial_half(half, c), true);
    }
}

void tick(ui::Canvas& canvas, int cx, int cy, int16_t a, int len = kTickLen) {
    radial_mark(canvas, cx, cy, a, kDialR - len, kFaceR);
}

void dial(ui::Canvas& canvas, int cx, int row, const char* title, int ticks = kTicks) {
    const int cy = kCy[row];
    canvas.circle(cx, cy, kDialR, true);
    for (int i = 0; i < ticks; i++) tick(canvas, cx, cy, angle16_of_degrees(i * (360 / ticks)));
    centred_text(canvas, cx, title_y(row), title);
}

void dead_sector(ui::Canvas& canvas, int cx, int cy, int32_t from_deg, int32_t to_deg) {
    const int16_t from = angle16_of_degrees(from_deg), to = angle16_of_degrees(to_deg);
    for (int dy = -kDialR; dy <= kDialR; dy++) {
        for (int dx = -kDialR; dx <= kDialR; dx++) {
            if (((dx + dy) & 1) != 0) continue;
            const int r2 = dx * dx + dy * dy;
            if (r2 > kDialR * kDialR || r2 <= kDeadInnerR * kDeadInnerR) continue;
            const int16_t a = iatan2(dx, -dy);
            if (from <= to ? (a < from || a > to) : (a < from && a > to)) continue;
            canvas.set_pixel(cx + dx, cy + dy, true);
        }
    }
}

int32_t vsi_deg(int32_t fpm) {
    const int32_t v = clampi(fpm, -kVsiFullScaleFpm, kVsiFullScaleFpm);
    const int32_t rate = v < 0 ? -v : v;
    const int32_t inner = (rate < kVsiKneeFpm ? rate : kVsiKneeFpm) * kVsiKneeDeg / kVsiKneeFpm;
    const int32_t outer = (rate > kVsiKneeFpm ? rate - kVsiKneeFpm : 0) * kVsiOuterDeg /
                          (kVsiFullScaleFpm - kVsiKneeFpm);
    return v < 0 ? kVsiZeroDeg - inner - outer : kVsiZeroDeg + inner + outer;
}

void vsi_face(ui::Canvas& canvas, int cx, int cy) {
    dead_sector(canvas, cx, cy, vsi_deg(kVsiFullScaleFpm), vsi_deg(-kVsiFullScaleFpm));
    for (int32_t fpm = -kVsiFullScaleFpm; fpm <= kVsiFullScaleFpm; fpm += kVsiMarkFpm)
        tick(canvas, cx, cy, angle16_of_degrees(vsi_deg(fpm)),
             fpm % kVsiKneeFpm == 0 ? kTickLen + 3 : kTickLen);
}

void needle(ui::Canvas& canvas, int cx, int cy, int32_t deg, int len, bool thick = false,
            bool cleared = false) {
    const int16_t a = angle16_of_degrees(deg);
    const int32_t s = isin(a), c = icos(a);
    const int tx = cx + radial(len, s);
    const int ty = cy - radial(len, c);
    if (cleared)
        for (int ox = -1; ox <= 1; ox++)
            for (int oy = -1; oy <= 1; oy++) canvas.line(cx + ox, cy + oy, tx + ox, ty + oy, false);
    canvas.line(cx, cy, tx, ty, true);
    if (thick) {  // the altimeter's thousands hand: short and broad, hundreds long and fine
        canvas.line(cx + 1, cy, tx + 1, ty, true);
        canvas.line(cx, cy + 1, tx, ty + 1, true);
    }
    canvas.circle(cx, cy, kHubR, true, true);
}

void horizon(ui::Canvas& canvas, int cx, int cy, int32_t pitch_deg, int32_t bank_deg) {
    const int32_t off =
        (clampi(pitch_deg, -kPitchFullScaleDeg, kPitchFullScaleDeg) * kDialR) / kPitchFullScaleDeg;
    const int16_t a = angle16_of_degrees(clampi(bank_deg, -kBankLimitDeg, kBankLimitDeg));
    const int32_t s = isin(a), c = icos(a);
    for (int dx = -kDialR + 1; dx < kDialR; dx++) {
        const int h =
            static_cast<int>(isqrt<uint32_t>(static_cast<uint32_t>(kDialR * kDialR - dx * dx)));
        const int32_t hy = clampi(off - (dx * s) / c, -h, h);
        const int x = cx + dx;
        for (int y = cy + static_cast<int>(hy); y <= cy + h; y++) {
            if (((x + y) & 1) == 0) canvas.set_pixel(x, y, true);
        }
    }
    for (int i = kReferenceInnerR; i <= kReferenceOuterR; i++) {  // solid over sky or ground
        canvas.set_pixel(cx - i, cy, true);
        canvas.set_pixel(cx + i, cy, true);
    }
    canvas.circle(cx, cy, kReferenceDotR, true, true);
}

int32_t rate_deflection_deg(int32_t turn_cdps) {
    return clampi((turn_cdps * kRateMarkDeg) / (kStandardRateDps * 100), -kRateFullScaleDeg,
                  kRateFullScaleDeg);
}

void inclinometer(ui::Canvas& canvas, int cx, int cy, const SixPackSnapshot& snapshot) {
    if (!snapshot.inclinometer_fitted) return;
    const int y = cy + kBallY;
    for (int side = -1; side <= 1; side += 2)
        canvas.vline(cx + side * kCageHalf, y - kCageHalfH, 2 * kCageHalfH + 1, true);
    if (!snapshot.lateral_valid) return;
    const int32_t swing =
        clampi(snapshot.lateral_mg, -flight::kSlipFullScaleMg, flight::kSlipFullScaleMg);
    canvas.circle(cx + static_cast<int>((swing * kBallTravel) / flight::kSlipFullScaleMg), y,
                  kBallR, true, true);
}

void turn_coordinator(ui::Canvas& canvas, int cx, int cy, int32_t turn_cdps) {
    const int16_t a = angle16_of_degrees(rate_deflection_deg(turn_cdps));
    const int32_t s = isin(a), c = icos(a);
    const int wx = radial(kWingHalf, c);
    const int wy = radial(kWingHalf, s);
    const int ux = radial(kTailUp, s);
    const int uy = radial(kTailUp, c);
    const int tx = radial(kTailHalf, c);
    const int ty = radial(kTailHalf, s);
    canvas.line(cx - wx, cy - wy, cx + wx, cy + wy, true);
    canvas.line(cx, cy, cx + radial(kFinLen, s), cy - radial(kFinLen, c), true);
    canvas.line(cx + ux - tx, cy - uy - ty, cx + ux + tx, cy - uy + ty, true);
    canvas.circle(cx, cy, kFuselageR, true, true);
    for (int side = -1; side <= 1; side += 2) {
        tick(canvas, cx, cy, angle16_of_degrees(90 * side));
        tick(canvas, cx, cy, angle16_of_degrees(side * (90 - kRateMarkDeg) + 180));
    }
}

// The card rotates so the flown track sits under the fixed index at the top,
// which is what makes N/E/S/W read as directions rather than as labels.
void heading_card(ui::Canvas& canvas, int cx, int cy, int32_t track_deg) {
    static const char kCardinal[4] = {'N', 'E', 'S', 'W'};
    for (int i = 0; i < kTicks; i++) {
        const int32_t bearing = i * (360 / kTicks);
        const int16_t a = angle16_of_degrees(bearing - track_deg);
        if (bearing % 90 != 0) {
            tick(canvas, cx, cy, a);
            continue;
        }
        canvas.draw_char(cx - kCellW / 2 + radial(kCardR, isin(a)),
                         cy - kCharH / 2 - radial(kCardR, icos(a)), kCardinal[bearing / 90], true);
    }
    canvas.rect(cx - 1, cy - kDialR, 3, kIndexTabLen, true, true);  // the lubber index, on the case
    ui::draw_skyship(canvas, cx, cy);
}

int32_t flight_path_deg(int32_t climb_fpm, int32_t speed_kt) {
    if (speed_kt <= 0) return 0;
    return (static_cast<int32_t>(iatan2(climb_fpm * 10, speed_kt * 1013)) * 360) / kAngle16PerTurn;
}

// Coordinated turn: tan(bank) = omega * V / g, which in deg/s and knots is
// turn_dps * kt / 1093, and no sensor on this device measures a bank.
int32_t inferred_bank_deg(int32_t turn_dps, int32_t speed_kt) {
    if (speed_kt <= 0) return 0;
    return (static_cast<int32_t>(iatan2(turn_dps * speed_kt, 1093)) * 360) / kAngle16PerTurn;
}

// INFO: fc 11oct26 on the cable the divider reads the USB rail, so the dial reads it as that
void battery_dial(ui::Canvas& canvas, int cx, int row, const SixPackSnapshot& snapshot) {
    char text[8];
    int written = 0;
    if (snapshot.charging) {
        dial(canvas, cx, row, "CHARGE V", 0);
        written = format_uint(
            text, static_cast<uint32_t>(div_round<int32_t>(snapshot.battery_mv, 10)), 1, 2);
    } else {
        dial(canvas, cx, row, "BATTERY", 0);
        written = snapshot.battery_valid ? format_uint(text, snapshot.battery_percent)
                                         : format_string(text, "--");
        written += format_string(text + written, "%");
    }
    text[written] = 0;
    value_text(canvas, cx, row, text);
}

}  // namespace

void draw_sixpack(ui::Canvas& canvas, const SixPackSnapshot& snapshot) {
    canvas.clear(true);

    const int32_t kt = snapshot.fix_valid ? clampi(snapshot.speed_kt, 0, 999) : 0;
    const int32_t alt_ft = snapshot.fix_valid ? snapshot.alt_ft : 0;
    const int32_t climb_fpm = snapshot.climb_valid ? snapshot.climb_fpm : 0;
    const int32_t turn_cdps = snapshot.fix_valid ? snapshot.turn_cdps : 0;
    const int32_t turn_dps = (turn_cdps + (turn_cdps < 0 ? -50 : 50)) / 100;
    const int32_t track = snapshot.fix_valid ? snapshot.track_deg % 360 : 0;
    const int32_t bank = inferred_bank_deg(turn_dps, kt);
    const int32_t pitch = flight_path_deg(climb_fpm, kt);

    const bool metric = snapshot.units == go::Units::Metric;
    const int32_t speed = metric ? (kt * 1852 + 500) / 1000 : kt;
    const int32_t speed_full = metric ? kAsiFullScaleKmh : kAsiFullScaleKt;

    dial(canvas, kCx[0], 0, metric ? "GS KM/H" : "GS KT", kAsiTicks);
    dead_sector(canvas, kCx[0], kCy[0], kAsiZeroDeg + kAsiSpanDeg, kAsiZeroDeg);
    needle(canvas, kCx[0], kCy[0],
           kAsiZeroDeg + (clampi(speed, 0, speed_full) * kAsiSpanDeg) / speed_full, kNeedleR,
           /*thick=*/false, /*cleared=*/true);
    centred_value(canvas, kCx[0], 0, snapshot.fix_valid, speed, false);

    battery_dial(canvas, kCx[1], 0, snapshot);
    horizon(canvas, kCx[1], kCy[0], pitch, bank);

    dial(canvas, kCx[2], 0, "ALT FT", kAltTicks);
    const int32_t on_scale = alt_ft < 0 ? 0 : alt_ft;
    needle(canvas, kCx[2], kCy[0], ((on_scale % kAltThousandsPerTurn) * 360) / kAltThousandsPerTurn,
           kShortNeedleR,
           /*thick=*/true);
    needle(canvas, kCx[2], kCy[0], ((on_scale % kAltHundredsPerTurn) * 360) / kAltHundredsPerTurn,
           kNeedleR);
    centred_value(canvas, kCx[2], 0, snapshot.fix_valid, alt_ft, false);

    dial(canvas, kCx[0], 1, "TURN D/S", 0);
    turn_coordinator(canvas, kCx[0], kCy[1], turn_cdps);
    inclinometer(canvas, kCx[0], kCy[1], snapshot);
    centred_value(canvas, kCx[0], 1, snapshot.fix_valid, turn_dps, true);

    dial(canvas, kCx[1], 1, "TRK", 0);
    heading_card(canvas, kCx[1], kCy[1], track);
    centred_value(canvas, kCx[1], 1, snapshot.fix_valid, track == 0 ? 360 : track, false, 3);

    dial(canvas, kCx[2], 1, "VS FPM", 0);
    vsi_face(canvas, kCx[2], kCy[1]);
    needle(canvas, kCx[2], kCy[1], vsi_deg(climb_fpm), kNeedleR, /*thick=*/false, /*cleared=*/true);
    centred_value(canvas, kCx[2], 1, snapshot.climb_valid, climb_figure_fpm(climb_fpm), true);
}

}  // namespace skyblip::go
