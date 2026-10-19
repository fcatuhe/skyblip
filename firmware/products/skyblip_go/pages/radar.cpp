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
constexpr int kCentreX = kFar;  // only for centring text, which has no such nicety
using ui::kCellW;
constexpr int kSecondsScale = 1;
constexpr int kSecondsGap = 2;
constexpr int kRangeScale = 2;
constexpr int kTrafficScale = 3;
constexpr int kRangePad = 4;
constexpr int kReadingCorner = 3;
constexpr int kFooterY = kFooterBottom - kCharH;
constexpr int kUnitGap = 3;
constexpr int kWedgeInnerR = 15;
constexpr int64_t kTanScale = 10000;
constexpr int64_t kWedgeEdgeTanE4 = 10000;
constexpr int kFormationHalfSide = 13;
constexpr int kFormationCorner = 4;
constexpr int kFormationInset = 3;
using ui::kCharW;
constexpr int kBannerScale = 2;
constexpr int kOwnshipTail = kNear - ui::kSkyshipRowsToNose + ui::kSkyshipRows;
constexpr int kNoteScale = 1;
constexpr int kNoteGap = 5;

struct Layout {
    int outer_r;
    int range_y;
    int banner_y;
    int note_y;
};

constexpr Layout layout_with(int outer_r, int range_y, int banner_y) {
    return {outer_r, range_y, banner_y, banner_y + kCharH * kBannerScale + kLabelPad + kNoteGap};
}

constexpr Layout kRangeOnTheRing =
    layout_with(kToScaleR, kFooterBottom - kCharH * kRangeScale,
                (kOwnshipTail + kFooterTop - kCharH * kBannerScale) / 2);
constexpr int kRangeOnRimY = kFar + kAllR - kRingW / 2 - kCharH * kRangeScale / 2;
constexpr Layout kRangeOnTheRim = layout_with(
    kAllR, kRangeOnRimY, (kOwnshipTail + kRangeOnRimY - kRangePad - kCharH * kBannerScale) / 2);

constexpr const Layout& layout_of(const RadarSnapshot& snapshot) {
    return snapshot.plot == RadarPlot::All ? kRangeOnTheRim : kRangeOnTheRing;
}

int half_chord_in_half_pixels(int r, int b) {
    const int32_t v = 4 * r * r - (2 * b + 1) * (2 * b + 1);
    if (v < 0) return -1;
    int q = square_root<int32_t>(v);
    while (q * q > v) q--;
    return (q - 1) / 2;
}

void ring(ui::Canvas& canvas, int r) {
    for (int b = 0; b < r; b++) {
        const int outer = half_chord_in_half_pixels(r, b);
        if (outer < 0) continue;
        const int inner = half_chord_in_half_pixels(r - kRingW, b) + 1;
        const int w = outer - inner + 1;
        canvas.hline(kFar + inner, kFar + b, w, true);
        canvas.hline(kFar + inner, kNear - b, w, true);
        canvas.hline(kNear - outer, kFar + b, w, true);
        canvas.hline(kNear - outer, kNear - b, w, true);
    }
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

Box padded(int x, int y, int w, int h, int pad = kLabelPad) {
    return {x - pad, y - pad, w + 2 * pad, h + 2 * pad};
}

void clear_behind(ui::Canvas& canvas, int x, int y, int w, int h) {
    const Box b = padded(x, y, w, h);
    canvas.rect(b.x, b.y, b.w, b.h, false, true);
}

bool flight_over(const RadarSnapshot& snapshot) {
    return snapshot.flight_time_valid && !snapshot.flight_running;
}

Box clock_box(const RadarSnapshot& snapshot) {
    char text[8];
    format_flight_clock(text, snapshot.flight_seconds, snapshot.flight_time_valid);
    int w = text_width(text, kClockScale);
    if (flight_over(snapshot)) w += kSecondsGap + text_width("00", kSecondsScale);
    return padded(kMargin, kClockY, w, kCharH * kClockScale);
}

void flight_clock(ui::Canvas& canvas, const RadarSnapshot& snapshot) {
    char text[8];
    format_flight_clock(text, snapshot.flight_seconds, snapshot.flight_time_valid);
    const int minutes_w = text_width(text, kClockScale);
    clear_behind(canvas, kMargin, kClockY, minutes_w, kCharH * kClockScale);
    canvas.draw_text(kMargin, kClockY, text, true, kClockScale);
    if (!flight_over(snapshot)) return;

    char seconds[4];
    seconds[format_unsigned(seconds, snapshot.flight_seconds % 60, 2)] = 0;
    const int x = kMargin + minutes_w + kSecondsGap;
    const int y = kClockY + kCharH * (kClockScale - kSecondsScale);
    clear_behind(canvas, x, y, text_width(seconds, kSecondsScale), kCharH * kSecondsScale);
    canvas.draw_text(x, y, seconds, true, kSecondsScale);
}

struct RangeText {
    char number[8];
    const char* unit;
    int number_w;
    int w;
};

RangeText range_text(const RadarSnapshot& snapshot) {
    RangeText t;
    const int written = format_unsigned(
        t.number, static_cast<uint32_t>(range_value(snapshot.range_step, snapshot.units)));
    t.number[written] = 0;
    t.unit = range_unit(snapshot.units);
    t.number_w = text_width(t.number, kRangeScale);
    t.w = t.number_w + kUnitGap + text_width(t.unit, 1);
    return t;
}

Box range_box(const RadarSnapshot& snapshot) {
    const int w = range_text(snapshot).w;
    return padded(kCentreX - w / 2, layout_of(snapshot).range_y, w, kCharH * kRangeScale,
                  kRangePad);
}

void range_label(ui::Canvas& canvas, const RadarSnapshot& snapshot) {
    const RangeText t = range_text(snapshot);
    const Layout& at = layout_of(snapshot);
    const Box b = range_box(snapshot);
    const int x = b.x + kRangePad;
    canvas.rect(b.x, b.y, b.w, b.h, false, true);
    canvas.draw_text(x, at.range_y, t.number, true, kRangeScale);
    canvas.draw_text(x + t.number_w + kUnitGap, at.range_y + kCharH * (kRangeScale - 1), t.unit,
                     true, 1);
}

void flight_word(ui::Canvas& canvas, const RadarSnapshot& snapshot) {
    if (!snapshot.fix_valid || !snapshot.flight_running) return;
    const char* state = "FLIGHT";
    clear_behind(canvas, kMargin, kStateY, text_width(state, kStateScale), kCharH * kStateScale);
    canvas.draw_text(kMargin, kStateY, state, true, kStateScale);
}

const char* state_word(const RadarSnapshot& snapshot) {
    if (!snapshot.fix_valid) return "NO FIX";
    if (snapshot.flight_running) return nullptr;
    return snapshot.taxiing ? "TAXI" : "GROUND";
}

const char* cell_word(const RadarSnapshot& snapshot, char* out) {
    if (!snapshot.battery_low) return nullptr;
    int written = format_string(out, "BAT ");
    written += format_unsigned(out + written, snapshot.battery_percent);
    written += format_string(out + written, "%");
    out[written] = 0;
    return out;
}

const char* ring_word(const RadarSnapshot& snapshot, char* out) {
    if (const char* cell = cell_word(snapshot, out)) return cell;
    return state_word(snapshot);
}

// INFO: fc 18sep26 how far the receiver has got, under the word that says it has not got there
const char* ring_note(const RadarSnapshot& snapshot, char* out) {
    if (cell_word(snapshot, out) != nullptr) return state_word(snapshot);
    return snapshot.fix_valid ? nullptr : gnss::stage_name(snapshot.stage);
}

Box note_box(const char* note, int y) {
    const int w = text_width(note, kNoteScale);
    return padded(kCentreX - w / 2, y, w, kCharH * kNoteScale);
}

void state_note(ui::Canvas& canvas, const char* note, int y) {
    const Box b = note_box(note, y);
    canvas.rect(b.x, b.y, b.w, b.h, false, true);
    canvas.draw_text(b.x + kLabelPad, b.y + kLabelPad, note, true, kNoteScale);
}

void traffic_count(ui::Canvas& canvas, int heard_count, bool counting) {
    char text[4];
    if (counting)
        text[format_unsigned(text, static_cast<uint32_t>(heard_count))] = 0;
    else
        text[format_string(text, "-")] = 0;
    const int x = kGlassWidth - kMargin - text_width(text, kTrafficScale);
    canvas.draw_text(x, kFooterBottom - kCharH * kTrafficScale, text, true, kTrafficScale);
    canvas.draw_text(x - kUnitGap - text_width("TFC", 1), kFooterY, "TFC", true, 1);
}

Box banner_box(const char* word, int y) {
    const int w = text_width(word, kBannerScale);
    return padded(kCentreX - w / 2, y, w, kCharH * kBannerScale);
}

void state_banner(ui::Canvas& canvas, const char* word, int y) {
    const Box b = banner_box(word, y);
    canvas.rect(b.x, b.y, b.w, b.h, false, true);
    canvas.draw_text(b.x + kLabelPad, b.y + kLabelPad, word, true, kBannerScale);
}

Box formation_box() {
    return {kNear - kFormationHalfSide, kNear - kFormationHalfSide, 2 * kFormationHalfSide + 2,
            2 * kFormationHalfSide + 2};
}

void formation_square(ui::Canvas& canvas) {
    const int d = kFormationHalfSide, r = kFormationCorner;
    const int x0 = kNear - d, x1 = kFar + d, y0 = kNear - d, y1 = kFar + d;
    canvas.hline(x0 + r + 1, y0, x1 - x0 - 2 * r - 1, true);
    canvas.hline(x0 + r + 1, y1, x1 - x0 - 2 * r - 1, true);
    canvas.vline(x0, y0 + r + 1, y1 - y0 - 2 * r - 1, true);
    canvas.vline(x1, y0 + r + 1, y1 - y0 - 2 * r - 1, true);
    for (int step = 0; step <= 90; step++) {
        const int16_t a = angle16_of_degrees(step);
        const int dx = r - (r * cosine(a)) / kTrigOne, dy = r - (r * sine(a)) / kTrigOne;
        canvas.set_pixel(x0 + dx, y0 + dy, true);
        canvas.set_pixel(x1 - dx, y0 + dy, true);
        canvas.set_pixel(x0 + dx, y1 - dy, true);
        canvas.set_pixel(x1 - dx, y1 - dy, true);
    }
}

// One digit per quadrant of own-ship's nose: how many of the formation are over there.
void formation_counts(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16) {
    int count[4] = {0, 0, 0, 0};
    for (int i = 0; i < snapshot.target_count; i++) {
        const RadarTarget& t = snapshot.targets[i];
        if (!t.in_formation) continue;
        const HeadingUp at = heading_up(t.north_m, t.east_m, track_angle16);
        const int quadrant = (at.ahead < 0 ? 2 : 0) + (at.right >= 0 ? 1 : 0);
        count[quadrant]++;
    }

    const int left = kNear - kFormationHalfSide + kFormationInset;
    const int right = kFar + kFormationHalfSide - kFormationInset - kCharW + 1;
    const int fore = kNear - kFormationHalfSide + kFormationInset;
    const int aft = kFar + kFormationHalfSide - kFormationInset - kCharH + 1;
    const int x[4] = {left, right, left, right};
    const int y[4] = {fore, fore, aft, aft};
    for (int q = 0; q < 4; q++) {
        if (count[q] <= 0) continue;
        char text[4];
        text[format_unsigned(text, static_cast<uint32_t>(count[q] > 9 ? 9 : count[q]))] = 0;
        canvas.draw_text(x[q], y[q], text, true, 1);
    }
}

struct Wedge {
    int32_t x{0};
    int32_t y{0};
};

int alarm_wedges(const RadarSnapshot& snapshot, int16_t track_angle16, Wedge* out) {
    if (!snapshot.alarm_flash) return 0;
    int n = 0;
    for (int i = 0; i < snapshot.target_count && n < kMaxRadarTargets; i++) {
        const RadarTarget& t = snapshot.targets[i];
        if (t.alarm_level < traffic::Level::Advisory || t.alarm_dismissed) continue;
        const HeadingUp at = heading_up(t.north_m, t.east_m, track_angle16);
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

void invert_wedges(ui::Canvas& canvas, int outer_r, const Wedge* wedges, int n_wedges,
                   const Box* keep_out, int n_keep_out, const Box* formation) {
    const int inside = outer_r - kRingW;
    const int64_t outer2 = 4 * static_cast<int64_t>(inside) * inside;
    const int bottom = kFar + outer_r < kGlassHeight ? kFar + outer_r : kGlassHeight;
    for (int y = kFar - outer_r; y < bottom; y++) {
        const int64_t py = 2 * (y - kFar) + 1;
        for (int x = kFar - outer_r; x < kFar + outer_r; x++) {
            const int64_t px = 2 * (x - kFar) + 1;
            const int64_t r2 = px * px + py * py;
            if (r2 > outer2 || at_the_apex(formation, r2, x, y)) continue;
            if (!in_any_wedge(wedges, n_wedges, px, py)) continue;
            if (spared(keep_out, n_keep_out, x, y)) continue;
            canvas.set_pixel(x, y, !canvas.get_pixel(x, y));
        }
    }
}

}  // namespace

void draw_radar(ui::Canvas& canvas, const RadarSnapshot& snapshot) {
    canvas.clear(true);

    const Layout& layout = layout_of(snapshot);
    ring(canvas, layout.outer_r);
    ui::draw_skyship(canvas, kFar, kNear);
    flight_clock(canvas, snapshot);
    flight_word(canvas, snapshot);
    range_label(canvas, snapshot);
    traffic_count(canvas, snapshot.heard_count, snapshot.fix_valid && snapshot.receiver_listening);

    Box readings[4] = {range_box(snapshot), clock_box(snapshot)};
    int n_readings = 2;
    char word_buffer[12], note_buffer[12];
    if (const char* word = ring_word(snapshot, word_buffer)) {
        state_banner(canvas, word, layout.banner_y);
        readings[n_readings++] = banner_box(word, layout.banner_y);
    }
    if (const char* note = ring_note(snapshot, note_buffer)) {
        state_note(canvas, note, layout.note_y);
        readings[n_readings++] = note_box(note, layout.note_y);
    }

    const int16_t track_angle16 =
        angle16_of_degrees(to_degrees(CentiDegrees(snapshot.track_cdeg)).value);
    if (snapshot.fix_valid) {
        plot_leaders(canvas, snapshot, track_angle16);
        plot_blips(canvas, snapshot, track_angle16);
    }

    Wedge wedges[kMaxRadarTargets];
    const int n_wedges = alarm_wedges(snapshot, track_angle16, wedges);
    if (n_wedges > 0) {
        const Box square = formation_box();
        invert_wedges(canvas, layout.outer_r, wedges, n_wedges, readings, n_readings,
                      snapshot.formation_members > 0 ? &square : nullptr);
    }

    if (snapshot.formation_members > 0) {
        formation_square(canvas);
        formation_counts(canvas, snapshot, track_angle16);
    }

    if (snapshot.fix_valid && snapshot.plot == RadarPlot::All) {
        const Box label = range_box(snapshot);
        plot_rim(canvas, snapshot, track_angle16,
                 {label.x, label.y, label.x + label.w - 1, label.y + label.h - 1});
    }
}

}  // namespace skyblip::go
