#include "products/skyblip_go/pages/sixpack.h"

#include "core/flight/slip.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/pages/page.h"
#include "ui/widgets/skyship.h"

namespace skyblip::go {

namespace {

constexpr int kR = 31;
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
constexpr int kCardR = kR - 7;
constexpr int kIndexTabLen = 5;
constexpr int kNeedle = kR - 6;
constexpr int kFaceR = kR - 1;
constexpr int kDeadInnerR = kR - 7;
constexpr int kShortNeedle = kR - 15;
constexpr int kHubR = 2;
constexpr int kCharW = 6;
constexpr int kGlyphH = 7;

// isin/icos are Q14 (16384 = 1.0) over a 65536-unit circle.
constexpr int32_t kOne = 16384;
constexpr int32_t kTurn = 65536;

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
constexpr int32_t kRateFullDeg = 45;
constexpr int kWingHalf = kR - 9;
constexpr int kFinLen = 6;
constexpr int kTailUp = 3;
constexpr int kTailHalf = 8;
constexpr int kBallY = 18;
constexpr int kBallR = 4;
constexpr int kCageHalf = kBallR + 2;
constexpr int kCageHalfH = 5;
constexpr int kBallTravel = 16;
constexpr int32_t kSlipFullMg = flight::kSlipFullScaleMg;
constexpr int kFuselageR = 3;
constexpr int kRefInner = 7;
constexpr int kRefOuter = 20;
constexpr int kRefDotR = 2;

int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Rounded, sign-symmetric projection of a radius onto a cordic axis: plain
// integer division truncates toward zero and pulls both ends of a mark inward.
int radial_half(int32_t r_half, int32_t q14) {
    const int32_t v = r_half * q14;
    return static_cast<int>((v + (v < 0 ? -kOne : kOne)) / (2 * kOne));
}

int radial(int32_t r, int32_t q14) { return radial_half(2 * r, q14); }

int16_t c16(int32_t deg) {
    int32_t d = ((deg % 360) + 360) % 360;
    if (d >= 180) d -= 360;  // keep the cordic value inside int16_t
    return static_cast<int16_t>((d * kTurn) / 360);
}

int value_y(int row) {
    return row == 0 ? kCy[0] - kR - kValueGap - kGlyphH * kValueScale : kCy[1] + kR + kValueGap;
}

int title_y(int row) {
    return row == 0 ? value_y(0) - kTitleGap - kGlyphH
                    : value_y(1) + kGlyphH * kValueScale + kTitleGap;
}

void text_center(ui::Canvas& fb, int cx, int y, const char* s, int scale = 1) {
    int n = 0;
    while (s[n]) n++;
    fb.draw_text(cx - (n * kCharW * scale) / 2, y, s, true, scale);
}

void value_text(ui::Canvas& fb, int cx, int row, const char* s) {
    text_center(fb, cx, value_y(row), s, kValueScale);
}

void value_center(ui::Canvas& fb, int cx, int row, bool have, int32_t v, bool no_plus,
                  uint8_t min_digits = 1) {
    if (!have) {
        value_text(fb, cx, row, "---");
        return;
    }
    char buf[12];
    int n = fmt_int(buf, v, min_digits, 0, no_plus || v == 0);
    buf[n] = 0;
    value_text(fb, cx, row, buf);
}

// Marks are stepped along the true radius in half-pixels rather than drawn as a
// line between two rounded end points: over 4 px, rounding both ends tilts the
// mark by degrees, which is what made the scales look bent.
void radial_mark(ui::Canvas& fb, int cx, int cy, int16_t a, int from_r, int to_r) {
    const int32_t s = isin(a), c = icos(a);
    for (int half = 2 * from_r; half <= 2 * to_r; half++) {
        fb.set_pixel(cx + radial_half(half, s), cy - radial_half(half, c), true);
    }
}

void tick(ui::Canvas& fb, int cx, int cy, int16_t a, int len = kTickLen) {
    radial_mark(fb, cx, cy, a, kR - len, kFaceR);
}

void dial(ui::Canvas& fb, int cx, int row, const char* title, int ticks = kTicks) {
    const int cy = kCy[row];
    fb.circle(cx, cy, kR, true);
    for (int i = 0; i < ticks; i++) tick(fb, cx, cy, c16(i * (360 / ticks)));
    text_center(fb, cx, title_y(row), title);
}

void dead_sector(ui::Canvas& fb, int cx, int cy, int32_t from_deg, int32_t to_deg) {
    const int16_t from = c16(from_deg), to = c16(to_deg);
    for (int dy = -kR; dy <= kR; dy++) {
        for (int dx = -kR; dx <= kR; dx++) {
            if (((dx + dy) & 1) != 0) continue;
            const int r2 = dx * dx + dy * dy;
            if (r2 > kR * kR || r2 <= kDeadInnerR * kDeadInnerR) continue;
            const int16_t a = iatan2(dx, -dy);
            if (from <= to ? (a < from || a > to) : (a < from && a > to)) continue;
            fb.set_pixel(cx + dx, cy + dy, true);
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

void vsi_face(ui::Canvas& fb, int cx, int cy) {
    dead_sector(fb, cx, cy, vsi_deg(kVsiFullScaleFpm), vsi_deg(-kVsiFullScaleFpm));
    for (int32_t fpm = -kVsiFullScaleFpm; fpm <= kVsiFullScaleFpm; fpm += kVsiMarkFpm)
        tick(fb, cx, cy, c16(vsi_deg(fpm)), fpm % kVsiKneeFpm == 0 ? kTickLen + 3 : kTickLen);
}

void needle(ui::Canvas& fb, int cx, int cy, int32_t deg, int len, bool thick = false,
            bool cleared = false) {
    const int16_t a = c16(deg);
    const int32_t s = isin(a), c = icos(a);
    const int tx = cx + radial(len, s);
    const int ty = cy - radial(len, c);
    if (cleared)
        for (int ox = -1; ox <= 1; ox++)
            for (int oy = -1; oy <= 1; oy++) fb.line(cx + ox, cy + oy, tx + ox, ty + oy, false);
    fb.line(cx, cy, tx, ty, true);
    if (thick) {  // the altimeter's thousands hand: short and broad, hundreds long and fine
        fb.line(cx + 1, cy, tx + 1, ty, true);
        fb.line(cx, cy + 1, tx, ty + 1, true);
    }
    fb.circle(cx, cy, kHubR, true, true);
}

void horizon(ui::Canvas& fb, int cx, int cy, int32_t pitch_deg, int32_t bank_deg) {
    const int32_t off =
        (clampi(pitch_deg, -kPitchFullScaleDeg, kPitchFullScaleDeg) * kR) / kPitchFullScaleDeg;
    const int16_t a = c16(clampi(bank_deg, -kBankLimitDeg, kBankLimitDeg));
    const int32_t s = isin(a), c = icos(a);
    for (int dx = -kR + 1; dx < kR; dx++) {
        const int h = static_cast<int>(isqrt<uint32_t>(static_cast<uint32_t>(kR * kR - dx * dx)));
        const int32_t hy = clampi(off - (dx * s) / c, -h, h);
        const int x = cx + dx;
        for (int y = cy + static_cast<int>(hy); y <= cy + h; y++) {
            if (((x + y) & 1) == 0) fb.set_pixel(x, y, true);
        }
    }
    for (int i = kRefInner; i <= kRefOuter; i++) {  // solid over sky or ground
        fb.set_pixel(cx - i, cy, true);
        fb.set_pixel(cx + i, cy, true);
    }
    fb.circle(cx, cy, kRefDotR, true, true);
}

int32_t rate_deflection_deg(int32_t turn_cdps) {
    return clampi((turn_cdps * kRateMarkDeg) / (kStandardRateDps * 100), -kRateFullDeg,
                  kRateFullDeg);
}

void inclinometer(ui::Canvas& fb, int cx, int cy, const SixPackSnapshot& s) {
    if (!s.inclinometer_fitted) return;
    const int y = cy + kBallY;
    for (int side = -1; side <= 1; side += 2)
        fb.vline(cx + side * kCageHalf, y - kCageHalfH, 2 * kCageHalfH + 1, true);
    if (!s.lateral_valid) return;
    const int32_t swing = clampi(s.lateral_mg, -kSlipFullMg, kSlipFullMg);
    fb.circle(cx + static_cast<int>((swing * kBallTravel) / kSlipFullMg), y, kBallR, true, true);
}

void turn_coordinator(ui::Canvas& fb, int cx, int cy, int32_t turn_cdps) {
    const int16_t a = c16(rate_deflection_deg(turn_cdps));
    const int32_t s = isin(a), c = icos(a);
    const int wx = radial(kWingHalf, c);
    const int wy = radial(kWingHalf, s);
    const int ux = radial(kTailUp, s);
    const int uy = radial(kTailUp, c);
    const int tx = radial(kTailHalf, c);
    const int ty = radial(kTailHalf, s);
    fb.line(cx - wx, cy - wy, cx + wx, cy + wy, true);
    fb.line(cx, cy, cx + radial(kFinLen, s), cy - radial(kFinLen, c), true);
    fb.line(cx + ux - tx, cy - uy - ty, cx + ux + tx, cy - uy + ty, true);
    fb.circle(cx, cy, kFuselageR, true, true);
    for (int side = -1; side <= 1; side += 2) {
        tick(fb, cx, cy, c16(90 * side));
        tick(fb, cx, cy, c16(side * (90 - kRateMarkDeg) + 180));
    }
}

// The card rotates so the flown track sits under the fixed index at the top,
// which is what makes N/E/S/W read as directions rather than as labels.
void heading_card(ui::Canvas& fb, int cx, int cy, int32_t track_deg) {
    static const char kCardinal[4] = {'N', 'E', 'S', 'W'};
    for (int i = 0; i < kTicks; i++) {
        const int32_t bearing = i * (360 / kTicks);
        const int16_t a = c16(bearing - track_deg);
        if (bearing % 90 != 0) {
            tick(fb, cx, cy, a);
            continue;
        }
        fb.draw_char(cx - kCharW / 2 + radial(kCardR, isin(a)),
                     cy - kGlyphH / 2 - radial(kCardR, icos(a)), kCardinal[bearing / 90], true);
    }
    fb.rect(cx - 1, cy - kR, 3, kIndexTabLen, true, true);  // the lubber index, on the case
    ui::draw_skyship(fb, cx, cy);
}

int32_t flight_path_deg(int32_t vs_fpm, int32_t speed_kt) {
    if (speed_kt <= 0) return 0;
    return (static_cast<int32_t>(iatan2(vs_fpm * 10, speed_kt * 1013)) * 360) / kTurn;
}

// Coordinated turn: tan(bank) = omega * V / g, which in deg/s and knots is
// turn_dps * kt / 1093, and no sensor on this device measures a bank.
int32_t inferred_bank_deg(int32_t turn_dps, int32_t speed_kt) {
    if (speed_kt <= 0) return 0;
    return (static_cast<int32_t>(iatan2(turn_dps * speed_kt, 1093)) * 360) / kTurn;
}

void battery_value(ui::Canvas& fb, int cx, int row, const SixPackSnapshot& s) {
    char buf[8];
    int n = s.battery_valid ? fmt_uint(buf, s.battery_percent) : fmt_string(buf, "--");
    n += fmt_string(buf + n, "%");
    buf[n] = 0;
    value_text(fb, cx, row, buf);
}

}  // namespace

void draw_sixpack(ui::Canvas& fb, const SixPackSnapshot& s) {
    fb.clear(true);

    const int32_t kt = s.data_valid ? clampi(s.speed_kt, 0, 999) : 0;
    const int32_t alt_ft = s.data_valid ? s.alt_ft : 0;
    const int32_t vs_fpm = s.vs_valid ? s.vs_fpm : 0;
    const int32_t turn_cdps = s.data_valid ? s.turn_cdps : 0;
    const int32_t turn_dps = (turn_cdps + (turn_cdps < 0 ? -50 : 50)) / 100;
    const int32_t track = s.data_valid ? s.track_deg % 360 : 0;
    const int32_t bank = inferred_bank_deg(turn_dps, kt);
    const int32_t pitch = flight_path_deg(vs_fpm, kt);

    const bool metric = s.units == go::Units::Metric;
    const int32_t speed = metric ? (kt * 1852 + 500) / 1000 : kt;
    const int32_t speed_full = metric ? kAsiFullScaleKmh : kAsiFullScaleKt;

    dial(fb, kCx[0], 0, metric ? "GS KM/H" : "GS KT", kAsiTicks);
    dead_sector(fb, kCx[0], kCy[0], kAsiZeroDeg + kAsiSpanDeg, kAsiZeroDeg);
    needle(fb, kCx[0], kCy[0],
           kAsiZeroDeg + (clampi(speed, 0, speed_full) * kAsiSpanDeg) / speed_full, kNeedle,
           /*thick=*/false, /*cleared=*/true);
    value_center(fb, kCx[0], 0, s.data_valid, speed, true);

    dial(fb, kCx[1], 0, "BATTERY", 0);
    horizon(fb, kCx[1], kCy[0], pitch, bank);
    battery_value(fb, kCx[1], 0, s);

    dial(fb, kCx[2], 0, "ALT FT", kAltTicks);
    const int32_t on_scale = alt_ft < 0 ? 0 : alt_ft;
    needle(fb, kCx[2], kCy[0], ((on_scale % kAltThousandsPerTurn) * 360) / kAltThousandsPerTurn,
           kShortNeedle,
           /*thick=*/true);
    needle(fb, kCx[2], kCy[0], ((on_scale % kAltHundredsPerTurn) * 360) / kAltHundredsPerTurn,
           kNeedle);
    value_center(fb, kCx[2], 0, s.data_valid, alt_ft, true);

    dial(fb, kCx[0], 1, "TURN D/S", 0);
    turn_coordinator(fb, kCx[0], kCy[1], turn_cdps);
    inclinometer(fb, kCx[0], kCy[1], s);
    value_center(fb, kCx[0], 1, s.data_valid, turn_dps, false);

    dial(fb, kCx[1], 1, "TRK", 0);
    heading_card(fb, kCx[1], kCy[1], track);
    value_center(fb, kCx[1], 1, s.data_valid, track == 0 ? 360 : track, true, 3);

    dial(fb, kCx[2], 1, "VS FPM", 0);
    vsi_face(fb, kCx[2], kCy[1]);
    needle(fb, kCx[2], kCy[1], vsi_deg(vs_fpm), kNeedle, /*thick=*/false, /*cleared=*/true);
    value_center(fb, kCx[2], 1, s.vs_valid, climb_figure_fpm(vs_fpm), false);
}

}  // namespace skyblip::go
