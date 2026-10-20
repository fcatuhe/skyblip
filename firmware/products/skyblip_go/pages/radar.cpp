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
using ui::kCellWidth;
constexpr int kSecondsScale = 1;
constexpr int kSecondsGap = 2;
constexpr int kRangeScale = 2;
constexpr int kTrafficScale = 3;
constexpr int kRangePad = 4;
constexpr int kReadingCorner = 3;
constexpr int kFooterY = kFooterBottom - kCharacterHeight;
constexpr int kUnitGap = 3;
constexpr int kWedgeInnerRadius = 15;
constexpr int64_t kTangentScale = 10000;
constexpr int64_t kWedgeEdgeTangentE4 = 10000;
constexpr int kFormationHalfSide = 13;
constexpr int kFormationCorner = 4;
constexpr int kFormationInset = 3;
using ui::kCharacterWidth;
constexpr int kBannerScale = 2;
constexpr int kOwnshipTail = kNear - ui::kSkyshipRowsToNose + ui::kSkyshipRows;
constexpr int kNoteScale = 1;
constexpr int kNoteGap = 5;

struct Layout {
    int outer_radius;
    int range_y;
    int banner_y;
    int note_y;
};

constexpr Layout layout_with(int outer_radius, int range_y, int banner_y) {
    return {outer_radius, range_y, banner_y,
            banner_y + kCharacterHeight * kBannerScale + kLabelPad + kNoteGap};
}

constexpr Layout kRangeOnTheRing =
    layout_with(kToScaleRadius, kFooterBottom - kCharacterHeight * kRangeScale,
                (kOwnshipTail + kFooterTop - kCharacterHeight * kBannerScale) / 2);
constexpr int kRangeOnRimY =
    kFar + kAllRadius - kRingWidth / 2 - kCharacterHeight * kRangeScale / 2;
constexpr Layout kRangeOnTheRim =
    layout_with(kAllRadius, kRangeOnRimY,
                (kOwnshipTail + kRangeOnRimY - kRangePad - kCharacterHeight * kBannerScale) / 2);

constexpr const Layout& layout_of(const RadarSnapshot& snapshot) {
    return snapshot.plot == RadarPlot::All ? kRangeOnTheRim : kRangeOnTheRing;
}

int half_chord_in_half_pixels(int radius, int row) {
    const int32_t squared = 4 * radius * radius - (2 * row + 1) * (2 * row + 1);
    if (squared < 0) return -1;
    int root = square_root<int32_t>(squared);
    while (root * root > squared) root--;
    return (root - 1) / 2;
}

void ring(ui::Canvas& canvas, int radius) {
    for (int row = 0; row < radius; row++) {
        const int outer = half_chord_in_half_pixels(radius, row);
        if (outer < 0) continue;
        const int inner = half_chord_in_half_pixels(radius - kRingWidth, row) + 1;
        const int width = outer - inner + 1;
        canvas.horizontal_line(kFar + inner, kFar + row, width, true);
        canvas.horizontal_line(kFar + inner, kNear - row, width, true);
        canvas.horizontal_line(kNear - outer, kFar + row, width, true);
        canvas.horizontal_line(kNear - outer, kNear - row, width, true);
    }
}

int text_width(const char* text, int scale) {
    int count = 0;
    while (text[count]) count++;
    return count * kCellWidth * scale - scale;
}

struct Box {
    int x;
    int y;
    int width;
    int height;
};

Box padded(int x, int y, int width, int height, int pad = kLabelPad) {
    return {x - pad, y - pad, width + 2 * pad, height + 2 * pad};
}

void clear_behind(ui::Canvas& canvas, int x, int y, int width, int height) {
    const Box box = padded(x, y, width, height);
    canvas.rectangle(box.x, box.y, box.width, box.height, false, true);
}

bool flight_over(const RadarSnapshot& snapshot) {
    return snapshot.flight_time_valid && !snapshot.flight_running;
}

Box clock_box(const RadarSnapshot& snapshot) {
    char text[8];
    format_flight_clock(text, snapshot.flight_seconds, snapshot.flight_time_valid);
    int width = text_width(text, kClockScale);
    if (flight_over(snapshot)) width += kSecondsGap + text_width("00", kSecondsScale);
    return padded(kMargin, kClockY, width, kCharacterHeight * kClockScale);
}

void flight_clock(ui::Canvas& canvas, const RadarSnapshot& snapshot) {
    char text[8];
    format_flight_clock(text, snapshot.flight_seconds, snapshot.flight_time_valid);
    const int minutes_width = text_width(text, kClockScale);
    clear_behind(canvas, kMargin, kClockY, minutes_width, kCharacterHeight * kClockScale);
    canvas.draw_text(kMargin, kClockY, text, true, kClockScale);
    if (!flight_over(snapshot)) return;

    char seconds[4];
    seconds[format_unsigned(seconds, snapshot.flight_seconds % 60, 2)] = 0;
    const int x = kMargin + minutes_width + kSecondsGap;
    const int y = kClockY + kCharacterHeight * (kClockScale - kSecondsScale);
    clear_behind(canvas, x, y, text_width(seconds, kSecondsScale),
                 kCharacterHeight * kSecondsScale);
    canvas.draw_text(x, y, seconds, true, kSecondsScale);
}

struct RangeText {
    char number[8];
    const char* unit;
    int number_width;
    int width;
};

RangeText range_text(const RadarSnapshot& snapshot) {
    RangeText range;
    const int written = format_unsigned(
        range.number, static_cast<uint32_t>(range_value(snapshot.range_step, snapshot.units)));
    range.number[written] = 0;
    range.unit = range_unit(snapshot.units);
    range.number_width = text_width(range.number, kRangeScale);
    range.width = range.number_width + kUnitGap + text_width(range.unit, 1);
    return range;
}

Box range_box(const RadarSnapshot& snapshot) {
    const int width = range_text(snapshot).width;
    return padded(kCentreX - width / 2, layout_of(snapshot).range_y, width,
                  kCharacterHeight * kRangeScale, kRangePad);
}

void range_label(ui::Canvas& canvas, const RadarSnapshot& snapshot) {
    const RangeText range = range_text(snapshot);
    const Layout& at = layout_of(snapshot);
    const Box box = range_box(snapshot);
    const int x = box.x + kRangePad;
    canvas.rectangle(box.x, box.y, box.width, box.height, false, true);
    canvas.draw_text(x, at.range_y, range.number, true, kRangeScale);
    canvas.draw_text(x + range.number_width + kUnitGap,
                     at.range_y + kCharacterHeight * (kRangeScale - 1), range.unit, true, 1);
}

void flight_word(ui::Canvas& canvas, const RadarSnapshot& snapshot) {
    if (!snapshot.fix_valid || !snapshot.flight_running) return;
    const char* state = "FLIGHT";
    clear_behind(canvas, kMargin, kStateY, text_width(state, kStateScale),
                 kCharacterHeight * kStateScale);
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
    const int width = text_width(note, kNoteScale);
    return padded(kCentreX - width / 2, y, width, kCharacterHeight * kNoteScale);
}

void state_note(ui::Canvas& canvas, const char* note, int y) {
    const Box box = note_box(note, y);
    canvas.rectangle(box.x, box.y, box.width, box.height, false, true);
    canvas.draw_text(box.x + kLabelPad, box.y + kLabelPad, note, true, kNoteScale);
}

void traffic_count(ui::Canvas& canvas, int heard_count, bool counting) {
    char text[4];
    if (counting)
        text[format_unsigned(text, static_cast<uint32_t>(heard_count))] = 0;
    else
        text[format_string(text, "-")] = 0;
    const int x = kGlassWidth - kMargin - text_width(text, kTrafficScale);
    canvas.draw_text(x, kFooterBottom - kCharacterHeight * kTrafficScale, text, true,
                     kTrafficScale);
    canvas.draw_text(x - kUnitGap - text_width("TFC", 1), kFooterY, "TFC", true, 1);
}

Box banner_box(const char* word, int y) {
    const int width = text_width(word, kBannerScale);
    return padded(kCentreX - width / 2, y, width, kCharacterHeight * kBannerScale);
}

void state_banner(ui::Canvas& canvas, const char* word, int y) {
    const Box box = banner_box(word, y);
    canvas.rectangle(box.x, box.y, box.width, box.height, false, true);
    canvas.draw_text(box.x + kLabelPad, box.y + kLabelPad, word, true, kBannerScale);
}

Box formation_box() {
    return {kNear - kFormationHalfSide, kNear - kFormationHalfSide, 2 * kFormationHalfSide + 2,
            2 * kFormationHalfSide + 2};
}

void formation_square(ui::Canvas& canvas) {
    const int half_side = kFormationHalfSide, corner = kFormationCorner;
    const int x0 = kNear - half_side, x1 = kFar + half_side, y0 = kNear - half_side,
              y1 = kFar + half_side;
    canvas.horizontal_line(x0 + corner + 1, y0, x1 - x0 - 2 * corner - 1, true);
    canvas.horizontal_line(x0 + corner + 1, y1, x1 - x0 - 2 * corner - 1, true);
    canvas.vertical_line(x0, y0 + corner + 1, y1 - y0 - 2 * corner - 1, true);
    canvas.vertical_line(x1, y0 + corner + 1, y1 - y0 - 2 * corner - 1, true);
    for (int step = 0; step <= 90; step++) {
        const int16_t angle = angle16_of_degrees(step);
        const int delta_x = corner - (corner * cosine(angle)) / kTrigOne,
                  delta_y = corner - (corner * sine(angle)) / kTrigOne;
        canvas.set_pixel(x0 + delta_x, y0 + delta_y, true);
        canvas.set_pixel(x1 - delta_x, y0 + delta_y, true);
        canvas.set_pixel(x0 + delta_x, y1 - delta_y, true);
        canvas.set_pixel(x1 - delta_x, y1 - delta_y, true);
    }
}

// One digit per quadrant of own-ship's nose: how many of the formation are over there.
void formation_counts(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16) {
    int count[4] = {0, 0, 0, 0};
    for (int i = 0; i < snapshot.target_count; i++) {
        const RadarTarget& target = snapshot.targets[i];
        if (!target.in_formation) continue;
        const HeadingUp at = heading_up(target.north_m, target.east_m, track_angle16);
        const int quadrant = (at.ahead < 0 ? 2 : 0) + (at.right >= 0 ? 1 : 0);
        count[quadrant]++;
    }

    const int left = kNear - kFormationHalfSide + kFormationInset;
    const int right = kFar + kFormationHalfSide - kFormationInset - kCharacterWidth + 1;
    const int fore = kNear - kFormationHalfSide + kFormationInset;
    const int aft = kFar + kFormationHalfSide - kFormationInset - kCharacterHeight + 1;
    const int x[4] = {left, right, left, right};
    const int y[4] = {fore, fore, aft, aft};
    for (int quadrant = 0; quadrant < 4; quadrant++) {
        if (count[quadrant] <= 0) continue;
        char text[4];
        text[format_unsigned(text,
                             static_cast<uint32_t>(count[quadrant] > 9 ? 9 : count[quadrant]))] = 0;
        canvas.draw_text(x[quadrant], y[quadrant], text, true, 1);
    }
}

struct Wedge {
    int32_t x{0};
    int32_t y{0};
};

int alarm_wedges(const RadarSnapshot& snapshot, int16_t track_angle16, Wedge* out) {
    if (!snapshot.alarm_flash) return 0;
    int count = 0;
    for (int i = 0; i < snapshot.target_count && count < kMaxRadarTargets; i++) {
        const RadarTarget& target = snapshot.targets[i];
        if (target.alarm_level < traffic::Level::Advisory || target.alarm_dismissed) continue;
        const HeadingUp at = heading_up(target.north_m, target.east_m, track_angle16);
        if (at.ahead == 0 && at.right == 0) continue;
        out[count++] = {at.right, -at.ahead};
    }
    return count;
}

bool in_wedge(const Wedge& wedge, int64_t half_x, int64_t half_y) {
    const int64_t along = half_x * wedge.x + half_y * wedge.y;
    if (along <= 0) return false;
    const int64_t across = half_x * wedge.y - half_y * wedge.x;
    return (across < 0 ? -across : across) * kTangentScale <= kWedgeEdgeTangentE4 * along;
}

bool in_any_wedge(const Wedge* wedges, int wedge_count, int64_t half_x, int64_t half_y) {
    for (int i = 0; i < wedge_count; i++)
        if (in_wedge(wedges[i], half_x, half_y)) return true;
    return false;
}

bool inside_rounded(const Box& box, int corner, int x, int y) {
    const int past_left = box.x + corner - x;
    const int past_right = x - (box.x + box.width - 1 - corner);
    const int past_top = box.y + corner - y;
    const int past_bottom = y - (box.y + box.height - 1 - corner);
    if (x < box.x || y < box.y || past_right > corner || past_bottom > corner) return false;
    const int delta_x = past_left > 0 ? past_left : (past_right > 0 ? past_right : 0);
    const int delta_y = past_top > 0 ? past_top : (past_bottom > 0 ? past_bottom : 0);
    return delta_x * delta_x + delta_y * delta_y <= corner * corner;
}

bool spared(const Box* keep_out, int keep_out_count, int x, int y) {
    for (int i = 0; i < keep_out_count; i++)
        if (inside_rounded(keep_out[i], kReadingCorner, x, y)) return true;
    return false;
}

bool at_the_apex(const Box* formation, int64_t radius_squared, int x, int y) {
    if (formation) return inside_rounded(*formation, kFormationCorner, x, y);
    return radius_squared < 4 * static_cast<int64_t>(kWedgeInnerRadius) * kWedgeInnerRadius;
}

void invert_wedges(ui::Canvas& canvas, int outer_radius, const Wedge* wedges, int wedge_count,
                   const Box* keep_out, int keep_out_count, const Box* formation) {
    const int inside = outer_radius - kRingWidth;
    const int64_t inside_squared = 4 * static_cast<int64_t>(inside) * inside;
    const int bottom = kFar + outer_radius < kGlassHeight ? kFar + outer_radius : kGlassHeight;
    for (int y = kFar - outer_radius; y < bottom; y++) {
        const int64_t half_y = 2 * (y - kFar) + 1;
        for (int x = kFar - outer_radius; x < kFar + outer_radius; x++) {
            const int64_t half_x = 2 * (x - kFar) + 1;
            const int64_t radius_squared = half_x * half_x + half_y * half_y;
            if (radius_squared > inside_squared || at_the_apex(formation, radius_squared, x, y))
                continue;
            if (!in_any_wedge(wedges, wedge_count, half_x, half_y)) continue;
            if (spared(keep_out, keep_out_count, x, y)) continue;
            canvas.set_pixel(x, y, !canvas.get_pixel(x, y));
        }
    }
}

}  // namespace

void draw_radar(ui::Canvas& canvas, const RadarSnapshot& snapshot) {
    canvas.clear(true);

    const Layout& layout = layout_of(snapshot);
    ring(canvas, layout.outer_radius);
    ui::draw_skyship(canvas, kFar, kNear);
    flight_clock(canvas, snapshot);
    flight_word(canvas, snapshot);
    range_label(canvas, snapshot);
    traffic_count(canvas, snapshot.heard_count, snapshot.fix_valid && snapshot.receiver_listening);

    Box readings[4] = {range_box(snapshot), clock_box(snapshot)};
    int reading_count = 2;
    char word_buffer[12], note_buffer[12];
    if (const char* word = ring_word(snapshot, word_buffer)) {
        state_banner(canvas, word, layout.banner_y);
        readings[reading_count++] = banner_box(word, layout.banner_y);
    }
    if (const char* note = ring_note(snapshot, note_buffer)) {
        state_note(canvas, note, layout.note_y);
        readings[reading_count++] = note_box(note, layout.note_y);
    }

    const int16_t track_angle16 =
        angle16_of_degrees(to_degrees(CentiDegrees(snapshot.track_cdeg)).value);
    if (snapshot.fix_valid) {
        plot_leaders(canvas, snapshot, track_angle16);
        plot_blips(canvas, snapshot, track_angle16);
    }

    Wedge wedges[kMaxRadarTargets];
    const int wedge_count = alarm_wedges(snapshot, track_angle16, wedges);
    if (wedge_count > 0) {
        const Box square = formation_box();
        invert_wedges(canvas, layout.outer_radius, wedges, wedge_count, readings, reading_count,
                      snapshot.formation_members > 0 ? &square : nullptr);
    }

    if (snapshot.formation_members > 0) {
        formation_square(canvas);
        formation_counts(canvas, snapshot, track_angle16);
    }

    if (snapshot.fix_valid && snapshot.plot == RadarPlot::All) {
        const Box label = range_box(snapshot);
        plot_rim(canvas, snapshot, track_angle16,
                 {label.x, label.y, label.x + label.width - 1, label.y + label.height - 1});
    }
}

}  // namespace skyblip::go
