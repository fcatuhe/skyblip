#include "products/skyblip_go/pages/radar.h"

#include "core/units/units.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/pages/radar_geometry.h"
#include "products/skyblip_go/pages/radar_traffic.h"
#include "ui/widgets/skyship.h"

namespace skyblip::go {

using namespace radar;

namespace {
constexpr int kCx = kFar;  // only for centring text, which has no such nicety
constexpr int kCellW = 6;
constexpr int kSecondsScale = 1;
constexpr int kSecondsGap = 2;
constexpr int kRangeScale = 2;
constexpr int kTrafficScale = 3;
constexpr int kRangePad = 6;
constexpr int kKeepOutPad = 3;
constexpr int kReadingCorner = 3;
constexpr int kFooterY = kFooterBottom - kGlyphH;
constexpr int kRangeY = kFooterBottom - kGlyphH * kRangeScale;
constexpr int kUnitGap = 3;
constexpr int32_t kTurn16 = 65536;
constexpr int kWedgeInnerR = 15;
constexpr int kWedgeOuterR = kOuterR - kRingW;
constexpr int64_t kTanScale = 10000;
constexpr int64_t kWedgeEdgeTanE4 = 10000;
constexpr int kFormationD = 13;
constexpr int kFormationCorner = 4;
constexpr int kFormationInset = 3;
constexpr int kDigitW = 5;
constexpr int kBannerScale = 2;
constexpr int kBannerPad = 4;
constexpr int kOwnShipTail = kNear - ui::kSkyshipRowsToNose + ui::kSkyshipRows;
constexpr int kBannerY = (kOwnShipTail + kFooterTop - kGlyphH * kBannerScale) / 2;
constexpr int kNoteScale = 1;
constexpr int kNotePad = 2;
constexpr int kNoteGap = 5;
constexpr int kNoteY = kBannerY + kGlyphH * kBannerScale + kBannerPad + kNoteGap;

int half_chord_in_half_pixels(int r, int b) {
    const int32_t v = 4 * r * r - (2 * b + 1) * (2 * b + 1);
    if (v < 0) return -1;
    int q = isqrt<int32_t>(v);
    while (q * q > v) q--;
    return (q - 1) / 2;
}

void ring(ui::Canvas& fb, int r) {
    for (int b = 0; b < r; b++) {
        const int outer = half_chord_in_half_pixels(r, b);
        if (outer < 0) continue;
        const int inner = half_chord_in_half_pixels(r - kRingW, b) + 1;
        const int w = outer - inner + 1;
        fb.hline(kFar + inner, kFar + b, w, true);
        fb.hline(kFar + inner, kNear - b, w, true);
        fb.hline(kNear - outer, kFar + b, w, true);
        fb.hline(kNear - outer, kNear - b, w, true);
    }
}

int16_t c16(int32_t deg) {
    int32_t d = ((deg % 360) + 360) % 360;
    if (d >= 180) d -= 360;  // keep the cordic value inside int16_t
    return static_cast<int16_t>((d * kTurn16) / 360);
}

int text_width(const char* s, int scale) {
    int n = 0;
    while (s[n]) n++;
    return n * kCellW * scale - scale;
}

struct Box {
    int x;
    int y;
    int w;
    int h;
};

Box padded(int x, int y, int w, int h, int pad) {
    return {x - pad, y - pad, w + 2 * pad, h + 2 * pad};
}

void clear_behind(ui::Canvas& fb, int x, int y, int w, int h, int pad) {
    const Box b = padded(x, y, w, h, pad);
    fb.rect(b.x, b.y, b.w, b.h, false, true);
}

bool flight_over(const RadarSnapshot& snap) { return snap.flight_time_valid && !snap.in_flight; }

Box clock_box(const RadarSnapshot& snap, int pad) {
    char buf[8];
    fmt_flight_clock(buf, snap.flight_seconds, snap.flight_time_valid);
    int w = text_width(buf, kClockScale);
    if (flight_over(snap)) w += kSecondsGap + text_width("00", kSecondsScale);
    return padded(kMargin, kClockY, w, kGlyphH * kClockScale, pad);
}

void flight_clock(ui::Canvas& fb, const RadarSnapshot& snap) {
    char buf[8];
    fmt_flight_clock(buf, snap.flight_seconds, snap.flight_time_valid);
    const int minutes_w = text_width(buf, kClockScale);
    clear_behind(fb, kMargin, kClockY, minutes_w, kGlyphH * kClockScale, kLabelPad);
    fb.draw_text(kMargin, kClockY, buf, true, kClockScale);
    if (!flight_over(snap)) return;

    char seconds[4];
    seconds[fmt_uint(seconds, snap.flight_seconds % 60, 2)] = 0;
    const int x = kMargin + minutes_w + kSecondsGap;
    const int y = kClockY + kGlyphH * (kClockScale - kSecondsScale);
    clear_behind(fb, x, y, text_width(seconds, kSecondsScale), kGlyphH * kSecondsScale, kLabelPad);
    fb.draw_text(x, y, seconds, true, kSecondsScale);
}

struct RangeText {
    char number[8];
    const char* unit;
    int number_w;
    int w;
};

RangeText range_text(const RadarSnapshot& snap) {
    RangeText t;
    const int n =
        fmt_uint(t.number, static_cast<uint32_t>(range_value(snap.range_step, snap.units)));
    t.number[n] = 0;
    t.unit = range_unit(snap.units);
    t.number_w = text_width(t.number, kRangeScale);
    t.w = t.number_w + kUnitGap + text_width(t.unit, 1);
    return t;
}

Box range_box(const RadarSnapshot& snap, int pad) {
    const int w = range_text(snap).w;
    return padded(kCx - w / 2, kRangeY, w, kGlyphH * kRangeScale, pad);
}

void range_label(ui::Canvas& fb, const RadarSnapshot& snap) {
    const RangeText t = range_text(snap);
    const Box b = range_box(snap, kRangePad);
    const int x = b.x + kRangePad;
    fb.rect(b.x, b.y, b.w, b.h, false, true);
    fb.draw_text(x, kRangeY, t.number, true, kRangeScale);
    fb.draw_text(x + t.number_w + kUnitGap, kRangeY + kGlyphH * (kRangeScale - 1), t.unit, true, 1);
}

void flight_word(ui::Canvas& fb, const RadarSnapshot& snap) {
    if (!snap.fix_valid || !snap.in_flight) return;
    const char* state = "FLIGHT";
    clear_behind(fb, kMargin, kStateY, text_width(state, kStateScale), kGlyphH * kStateScale,
                 kLabelPad);
    fb.draw_text(kMargin, kStateY, state, true, kStateScale);
}

const char* state_word(const RadarSnapshot& snap) {
    if (!snap.fix_valid) return "NO FIX";
    if (snap.in_flight) return nullptr;
    return snap.taxiing ? "TAXI" : "GROUND";
}

const char* cell_word(const RadarSnapshot& snap, char* buf) {
    if (!snap.battery_low) return nullptr;
    int n = fmt_string(buf, "BAT ");
    n += fmt_uint(buf + n, snap.battery_percent);
    n += fmt_string(buf + n, "%");
    buf[n] = 0;
    return buf;
}

const char* ring_word(const RadarSnapshot& snap, char* buf) {
    if (const char* cell = cell_word(snap, buf)) return cell;
    return state_word(snap);
}

// INFO: fc 18sep26 how far the receiver has got, under the word that says it has not got there
const char* ring_note(const RadarSnapshot& snap, char* buf) {
    if (cell_word(snap, buf) != nullptr) return state_word(snap);
    return snap.fix_valid ? nullptr : gnss::stage_name(snap.stage);
}

Box note_box(const char* note) {
    const int w = text_width(note, kNoteScale);
    return {kCx - w / 2 - kNotePad, kNoteY - kNotePad, w + 2 * kNotePad,
            kGlyphH * kNoteScale + 2 * kNotePad};
}

void state_note(ui::Canvas& fb, const char* note) {
    const Box b = note_box(note);
    fb.rect(b.x, b.y, b.w, b.h, false, true);
    fb.draw_text(b.x + kNotePad, b.y + kNotePad, note, true, kNoteScale);
}

void aircraft(ui::Canvas& fb, int in_view, bool counting) {
    char buf[4];
    if (counting)
        buf[fmt_uint(buf, static_cast<uint32_t>(in_view))] = 0;
    else
        buf[fmt_string(buf, "-")] = 0;
    const int x = kGlassW - kMargin - text_width(buf, kTrafficScale);
    fb.draw_text(x, kFooterBottom - kGlyphH * kTrafficScale, buf, true, kTrafficScale);
    fb.draw_text(x - kUnitGap - text_width("ACT", 1), kFooterY, "ACT", true, 1);
}

Box banner_box(const char* word) {
    const int w = text_width(word, kBannerScale);
    return {kCx - w / 2 - kBannerPad, kBannerY - kBannerPad, w + 2 * kBannerPad,
            kGlyphH * kBannerScale + 2 * kBannerPad};
}

void state_banner(ui::Canvas& fb, const char* word) {
    const Box b = banner_box(word);
    fb.rect(b.x, b.y, b.w, b.h, false, true);
    fb.draw_text(b.x + kBannerPad, b.y + kBannerPad, word, true, kBannerScale);
}

Box formation_box() {
    return {kNear - kFormationD, kNear - kFormationD, 2 * kFormationD + 2, 2 * kFormationD + 2};
}

void formation_square(ui::Canvas& fb) {
    const int d = kFormationD, r = kFormationCorner;
    const int x0 = kNear - d, x1 = kFar + d, y0 = kNear - d, y1 = kFar + d;
    fb.hline(x0 + r + 1, y0, x1 - x0 - 2 * r - 1, true);
    fb.hline(x0 + r + 1, y1, x1 - x0 - 2 * r - 1, true);
    fb.vline(x0, y0 + r + 1, y1 - y0 - 2 * r - 1, true);
    fb.vline(x1, y0 + r + 1, y1 - y0 - 2 * r - 1, true);
    for (int step = 0; step <= 90; step++) {
        const int16_t a = c16(step);
        const int dx = r - (r * icos(a)) / kQ14One, dy = r - (r * isin(a)) / kQ14One;
        fb.set_pixel(x0 + dx, y0 + dy, true);
        fb.set_pixel(x1 - dx, y0 + dy, true);
        fb.set_pixel(x0 + dx, y1 - dy, true);
        fb.set_pixel(x1 - dx, y1 - dy, true);
    }
}

// One digit per quadrant of own-ship's nose: how many of the formation are over there.
void formation_counts(ui::Canvas& fb, const RadarSnapshot& snap, int16_t track) {
    int count[4] = {0, 0, 0, 0};
    for (int i = 0; i < snap.n_targets; i++) {
        const RadarTarget& t = snap.targets[i];
        if (!t.in_formation) continue;
        const HeadingUp at = heading_up(t.north_m, t.east_m, track);
        const int quadrant = (at.ahead < 0 ? 2 : 0) + (at.right >= 0 ? 1 : 0);
        count[quadrant]++;
    }

    const int left = kNear - kFormationD + kFormationInset;
    const int right = kFar + kFormationD - kFormationInset - kDigitW + 1;
    const int fore = kNear - kFormationD + kFormationInset;
    const int aft = kFar + kFormationD - kFormationInset - kGlyphH + 1;
    const int x[4] = {left, right, left, right};
    const int y[4] = {fore, fore, aft, aft};
    for (int q = 0; q < 4; q++) {
        if (count[q] <= 0) continue;
        char buf[4];
        buf[fmt_uint(buf, static_cast<uint32_t>(count[q] > 9 ? 9 : count[q]))] = 0;
        fb.draw_text(x[q], y[q], buf, true, 1);
    }
}

struct Wedge {
    int32_t x{0};
    int32_t y{0};
};

int alarm_wedges(const RadarSnapshot& snap, int16_t track, Wedge* out) {
    if (!snap.alarm_flash) return 0;
    int n = 0;
    for (int i = 0; i < snap.n_targets && n < kMaxRadarTargets; i++) {
        const RadarTarget& t = snap.targets[i];
        if (t.alarm_level < traffic::Level::Advisory || t.alarm_dismissed) continue;
        const HeadingUp at = heading_up(t.north_m, t.east_m, track);
        if (at.ahead == 0 && at.right == 0) continue;
        out[n++] = {at.right, -at.ahead};
    }
    return n;
}

bool in_wedge(const Wedge& w, int64_t px, int64_t py) {
    const int64_t along = px * w.x + py * w.y;
    if (along <= 0) return false;
    const int64_t across = px * w.y - py * w.x;
    return (across < 0 ? -across : across) * kTanScale <= kWedgeEdgeTanE4 * along;
}

bool in_any_wedge(const Wedge* wedges, int n, int64_t px, int64_t py) {
    for (int i = 0; i < n; i++)
        if (in_wedge(wedges[i], px, py)) return true;
    return false;
}

bool inside_rounded(const Box& b, int corner, int x, int y) {
    const int past_left = b.x + corner - x;
    const int past_right = x - (b.x + b.w - 1 - corner);
    const int past_top = b.y + corner - y;
    const int past_bottom = y - (b.y + b.h - 1 - corner);
    if (x < b.x || y < b.y || past_right > corner || past_bottom > corner) return false;
    const int dx = past_left > 0 ? past_left : (past_right > 0 ? past_right : 0);
    const int dy = past_top > 0 ? past_top : (past_bottom > 0 ? past_bottom : 0);
    return dx * dx + dy * dy <= corner * corner;
}

bool spared(const Box* keep_out, int n, int x, int y) {
    for (int i = 0; i < n; i++)
        if (inside_rounded(keep_out[i], kReadingCorner, x, y)) return true;
    return false;
}

bool at_the_apex(const Box* formation, int64_t r2, int x, int y) {
    if (formation) return inside_rounded(*formation, kFormationCorner, x, y);
    return r2 < 4 * static_cast<int64_t>(kWedgeInnerR) * kWedgeInnerR;
}

void invert_wedges(ui::Canvas& fb, const Wedge* wedges, int n_wedges, const Box* keep_out,
                   int n_keep_out, const Box* formation) {
    const int64_t outer2 = 4 * static_cast<int64_t>(kWedgeOuterR) * kWedgeOuterR;
    const int bottom = kFar + kOuterR < kGlassH ? kFar + kOuterR : kGlassH;
    for (int y = kFar - kOuterR; y < bottom; y++) {
        const int64_t py = 2 * (y - kFar) + 1;
        for (int x = kFar - kOuterR; x < kFar + kOuterR; x++) {
            const int64_t px = 2 * (x - kFar) + 1;
            const int64_t r2 = px * px + py * py;
            if (r2 > outer2 || at_the_apex(formation, r2, x, y)) continue;
            if (!in_any_wedge(wedges, n_wedges, px, py)) continue;
            if (spared(keep_out, n_keep_out, x, y)) continue;
            fb.set_pixel(x, y, !fb.get_pixel(x, y));
        }
    }
}

}  // namespace

void draw_radar(ui::Canvas& fb, const RadarSnapshot& snap) {
    fb.clear(true);

    ring(fb, kOuterR);

    const int16_t track = c16(to_degrees(CentiDegrees(snap.track_cdeg)).v);

    ui::draw_skyship(fb, kFar, kNear);

    const int in_ring = snap.fix_valid ? plot(fb, snap, track) : 0;

    flight_clock(fb, snap);
    flight_word(fb, snap);
    range_label(fb, snap);
    aircraft(fb, in_ring, snap.fix_valid && snap.receiver_listening);

    Wedge wedges[kMaxRadarTargets];
    const int n_wedges = alarm_wedges(snap, track, wedges);
    if (n_wedges > 0) {
        const Box readings[] = {range_box(snap, kKeepOutPad), clock_box(snap, kKeepOutPad)};
        const Box square = formation_box();
        invert_wedges(fb, wedges, n_wedges, readings, 2,
                      snap.formation_members > 0 ? &square : nullptr);
    }

    if (snap.formation_members > 0) {
        formation_square(fb);
        formation_counts(fb, snap, track);
    }

    char ring[12], under[12];
    if (const char* word = ring_word(snap, ring)) state_banner(fb, word);
    if (const char* note = ring_note(snap, under)) state_note(fb, note);
}

}  // namespace skyblip::go
