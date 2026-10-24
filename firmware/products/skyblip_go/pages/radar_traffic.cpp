#include "products/skyblip_go/pages/radar_traffic.h"

#include "core/flight/arc.h"
#include "products/skyblip_go/pages/radar_geometry.h"
#include "ui/widgets/blip.h"
#include "ui/widgets/skyship.h"

namespace skyblip::go::radar {

namespace {
constexpr int16_t kCaretClimbE8 = 20;
constexpr int32_t kLevelM = 60;
constexpr int32_t kLeaderSeconds = 60;
constexpr uint32_t kLeaderStepMs = 5000;
constexpr int kStepsPerMinute = static_cast<int>((kLeaderSeconds * 1000) / kLeaderStepMs);
constexpr int kMinLeaderPx = 3;
constexpr int kOwnNoseAhead = ui::kSkyshipRowsToNose + 1;
constexpr int kMinuteClearPx = kOwnNoseAhead + kMinLeaderPx;
constexpr int kMinuteBallRadius = 3;
constexpr int kDashPx = 3;
constexpr int kDashGapPx = 3;
constexpr int kMinutesMarked = 2;
constexpr int kRimMarkPx = 15;
constexpr int kCountReachPx = 30;
using ui::kCharacterHeight;
using ui::kCharacterWidth;
constexpr int kCountClearPx = 4;

int glass_x_of(int32_t right) { return right >= 0 ? kFar + right : kNear + right + 1; }
int glass_y_of(int32_t ahead) { return ahead <= 0 ? kFar - ahead : kNear - ahead + 1; }

struct Plotted {
    int32_t right;
    int32_t ahead;
    int x;
    int y;
    bool in_ring;
    bool shown;
    ui::Vertical vertical;
    ui::Trend trend;
};

ui::Vertical vertical_of(int32_t up_m) {
    const int32_t apart = up_m < 0 ? -up_m : up_m;
    if (apart <= kLevelM) return ui::Vertical::Level;
    const bool beyond = apart > traffic::kAdvisoryAltitudeM;
    if (up_m > 0) return beyond ? ui::Vertical::FarAbove : ui::Vertical::Above;
    return beyond ? ui::Vertical::FarBelow : ui::Vertical::Below;
}

ui::Trend trend_of(const RadarTarget& target) {
    if (!target.climb_valid) return ui::Trend::Steady;
    if (target.climb_e8 >= kCaretClimbE8) return ui::Trend::Climbing;
    if (target.climb_e8 <= -kCaretClimbE8) return ui::Trend::Sinking;
    return ui::Trend::Steady;
}

int64_t ring_metres(const RadarSnapshot& snapshot) {
    return go::range_metres(snapshot.range_step, snapshot.units);
}

int32_t to_pixels(int32_t metres, int64_t range, int ring) {
    return static_cast<int32_t>((static_cast<int64_t>(metres) * ring) / range);
}

bool inside_ring(int32_t right, int32_t ahead, int ring) {
    return right * right + ahead * ahead <= ring * ring;
}

bool on_glass(int x, int y) { return x >= 0 && x < kGlassWidth && y >= 0 && y < kGlassHeight; }

bool pixel_within(int x, int y, int radius) {
    const int64_t half_x = 2 * (x - kFar) + 1, half_y = 2 * (y - kFar) + 1;
    return half_x * half_x + half_y * half_y < 4 * static_cast<int64_t>(radius) * radius;
}

HeadingUp on_the_rim(int32_t right, int32_t ahead) {
    const int64_t wide_right = right, wide_ahead = ahead;
    const int64_t apart = square_root<int64_t>(wide_right * wide_right + wide_ahead * wide_ahead);
    return {static_cast<int32_t>(wide_ahead * kRimRadius / apart),
            static_cast<int32_t>(wide_right * kRimRadius / apart)};
}

Plotted plot_point(const RadarSnapshot& snapshot, const RadarTarget& target,
                   int16_t track_angle16) {
    const int ring = ring_radius(snapshot.plot);
    const int64_t range = ring_metres(snapshot);
    const HeadingUp at = heading_up(target.north_m, target.east_m, track_angle16);
    const int32_t delta_x = to_pixels(at.right, range, ring),
                  delta_y = to_pixels(at.ahead, range, ring);
    const bool in_ring = inside_ring(delta_x, delta_y, ring);
    const bool pinned = !in_ring && snapshot.plot == RadarPlot::All;
    const HeadingUp drawn = pinned ? on_the_rim(delta_x, delta_y) : HeadingUp{delta_y, delta_x};
    Plotted plotted{delta_x, delta_y, glass_x_of(drawn.right),  glass_y_of(drawn.ahead),
                    in_ring, true,    vertical_of(target.up_m), trend_of(target)};
    plotted.shown = pinned || on_glass(plotted.x, plotted.y);
    return plotted;
}

flight::Motion motion_of(int32_t speed_mm_s, int32_t track_cdeg, int16_t turn_cdps, bool turning) {
    flight::Motion motion{};
    motion.speed_mm_s = speed_mm_s;
    motion.track_cdeg = track_cdeg;
    motion.turn_cdps = turn_cdps;
    motion.turning = turning;
    return motion;
}

HeadingUp on_glass_at(const flight::Position& position, const RadarSnapshot& snapshot,
                      int16_t track_angle16) {
    const HeadingUp at = heading_up(position.north_m, position.east_m, track_angle16);
    const int64_t range = ring_metres(snapshot);
    const int ring = ring_radius(snapshot.plot);
    return {to_pixels(at.ahead, range, ring), to_pixels(at.right, range, ring)};
}

void minute_ball(ui::Canvas& canvas, int x, int y) {
    const int radius = kMinuteBallRadius;
    for (int delta_y = -radius; delta_y < radius; delta_y++)
        for (int delta_x = -radius; delta_x < radius; delta_x++) {
            const int half_x = 2 * delta_x + 1, half_y = 2 * delta_y + 1;
            if (half_x * half_x + half_y * half_y <= 4 * radius * radius)
                canvas.set_pixel(x + delta_x, y + delta_y, true);
        }
}

struct DashPen {
    ui::Canvas& canvas;
    int phase{0};
    int x{0};
    int y{0};
    bool down{false};
    bool fresh{false};

    void lift() { down = false; }

    void to(int x1, int y1) {
        if (down)
            stroke(x1, y1);
        else
            fresh = down = true;
        x = x1;
        y = y1;
    }

   private:
    void dash(int at_x, int at_y, bool steep) {
        if (phase++ % (kDashPx + kDashGapPx) >= kDashPx) return;
        canvas.set_pixel(at_x, at_y, true);
        canvas.set_pixel(steep ? at_x - 1 : at_x, steep ? at_y : at_y - 1, true);
    }

    void stroke(int x1, int y1) {
        int at_x = x, at_y = y;
        const int delta_x = x1 > at_x ? x1 - at_x : at_x - x1;
        const int delta_y = y1 > at_y ? y1 - at_y : at_y - y1;
        const int step_x = at_x < x1 ? 1 : -1, step_y = at_y < y1 ? 1 : -1;
        const bool steep = delta_y >= delta_x;
        int error = delta_x - delta_y;
        if (fresh) dash(at_x, at_y, steep);
        fresh = false;
        while (at_x != x1 || at_y != y1) {
            const int doubled_error = 2 * error;
            if (doubled_error > -delta_y) {
                error -= delta_y;
                at_x += step_x;
            }
            if (doubled_error < delta_x) {
                error += delta_x;
                at_y += step_y;
            }
            dash(at_x, at_y, steep);
        }
    }
};

void own_vector(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16) {
    if (snapshot.speed_mm_s <= 0) return;
    flight::Arc arc(motion_of(snapshot.speed_mm_s, snapshot.track_cdeg, snapshot.turn_cdps, true),
                    kLeaderStepMs);
    DashPen pen{canvas};
    const int ring = ring_radius(snapshot.plot);
    const int ball_clear = ring - kRingWidth - kMinuteBallRadius;
    for (int step = 1; step <= kMinutesMarked * kStepsPerMinute; step++) {
        const HeadingUp at = on_glass_at(arc.advance(), snapshot, track_angle16);
        if (!inside_ring(at.right, at.ahead, ring)) return;
        const int32_t out = at.right * at.right + at.ahead * at.ahead;
        if (out < kMinuteClearPx * kMinuteClearPx) {
            pen.lift();
            continue;
        }
        const int x = glass_x_of(at.right), y = glass_y_of(at.ahead);
        pen.to(x, y);
        if (step % kStepsPerMinute != 0) continue;
        if (out <= ball_clear * ball_clear) minute_ball(canvas, x, y);
    }
}

struct Leader {
    int32_t right;
    int32_t ahead;
    bool valid;
};

Leader leader_of(const RadarSnapshot& snapshot, const RadarTarget& target, int16_t track_angle16) {
    if (target.speed_mm_s <= 0) return {0, 0, false};
    flight::Arc arc(
        motion_of(target.speed_mm_s, target.track_cdeg, target.turn_cdps, target.turn_valid),
        kLeaderStepMs);
    HeadingUp end{0, 0};
    for (int step = 0; step < kStepsPerMinute; step++)
        end = on_glass_at(arc.advance(), snapshot, track_angle16);
    if (end.right * end.right + end.ahead * end.ahead < kMinLeaderPx * kMinLeaderPx)
        return {0, 0, false};
    return {end.right, end.ahead, true};
}

void line_inside_ring(ui::Canvas& canvas, int x0, int y0, int x1, int y1, int ring) {
    const int delta_x = x1 > x0 ? x1 - x0 : x0 - x1, delta_y = y1 > y0 ? y1 - y0 : y0 - y1;
    const int step_x = x0 < x1 ? 1 : -1, step_y = y0 < y1 ? 1 : -1;
    int error = delta_x - delta_y;
    for (;;) {
        if (pixel_within(x0, y0, ring - kRingWidth)) canvas.set_pixel(x0, y0, true);
        if (x0 == x1 && y0 == y1) return;
        const int doubled_error = 2 * error;
        if (doubled_error > -delta_y) {
            error -= delta_y;
            x0 += step_x;
        }
        if (doubled_error < delta_x) {
            error += delta_x;
            y0 += step_y;
        }
    }
}

void draw_leader(ui::Canvas& canvas, const RadarSnapshot& snapshot, const RadarTarget& target,
                 int16_t track_angle16, const Plotted& plotted, const Leader& leader) {
    if (!leader.valid) return;
    flight::Arc arc(
        motion_of(target.speed_mm_s, target.track_cdeg, target.turn_cdps, target.turn_valid),
        kLeaderStepMs);
    int from_x = glass_x_of(plotted.right), from_y = glass_y_of(plotted.ahead);
    for (int step = 0; step < kStepsPerMinute; step++) {
        const HeadingUp at = on_glass_at(arc.advance(), snapshot, track_angle16);
        const int to_x = glass_x_of(plotted.right + at.right),
                  to_y = glass_y_of(plotted.ahead + at.ahead);
        if (snapshot.plot == RadarPlot::All)
            line_inside_ring(canvas, from_x, from_y, to_x, to_y, kAllRadius);
        else
            canvas.line(from_x, from_y, to_x, to_y, true);
        from_x = to_x;
        from_y = to_y;
    }
}

void plot_blip(ui::Canvas& canvas, const Plotted& plotted, traffic::Level alarm_level) {
    ui::draw_blip(canvas, plotted.x, plotted.y, plotted.vertical, plotted.trend,
                  alarm_level >= traffic::Level::Advisory);
}

int64_t apart_squared(const Plotted& plotted) {
    return static_cast<int64_t>(plotted.right) * plotted.right +
           static_cast<int64_t>(plotted.ahead) * plotted.ahead;
}

struct RimMark {
    Plotted plotted;
    traffic::Level level;
    int32_t separation_m;
    int64_t apart_squared;
    int count;
};

bool outranks(const RimMark& mark, const RimMark& other) {
    if (mark.level != other.level) return mark.level > other.level;
    if (mark.separation_m != other.separation_m) return mark.separation_m < other.separation_m;
    return mark.apart_squared < other.apart_squared;
}

RimMark* covered_by(RimMark* drawn, int count, const Plotted& plotted) {
    for (int i = 0; i < count; i++) {
        const int delta_x = drawn[i].plotted.x - plotted.x,
                  delta_y = drawn[i].plotted.y - plotted.y;
        if ((delta_x < 0 ? -delta_x : delta_x) < kRimMarkPx &&
            (delta_y < 0 ? -delta_y : delta_y) < kRimMarkPx)
            return &drawn[i];
    }
    return nullptr;
}

Bounds mark_box(const Plotted& plotted) {
    const int beside = ui::blip_beside(plotted.vertical);
    return {plotted.x - beside, plotted.y - ui::blip_above(plotted.vertical, plotted.trend) - 1,
            plotted.x + beside, plotted.y + ui::blip_below(plotted.vertical, plotted.trend) + 1};
}

Bounds grown(const Bounds& bounds, int by) {
    return {bounds.left - by, bounds.top - by, bounds.right + by, bounds.bottom + by};
}

bool overlap(const Bounds& first, const Bounds& second) {
    return first.left <= second.right && second.left <= first.right && first.top <= second.bottom &&
           second.top <= first.bottom;
}

Bounds count_box_at(const RimMark& mark, int along_x, int along_y, int reach) {
    const int x = mark.plotted.x + along_x * reach / kRimRadius - kCharacterWidth / 2;
    const int y = mark.plotted.y + along_y * reach / kRimRadius - kCharacterHeight / 2;
    return {x - 1, y - 1, x + kCharacterWidth, y + kCharacterHeight};
}

Bounds count_box_beside(const RimMark& mark, int along_x, int along_y) {
    const Bounds own = mark_box(mark.plotted);
    Bounds box = count_box_at(mark, along_x, along_y, 0);
    for (int reach = 1; reach <= kCountReachPx && overlap(box, own); reach++)
        box = count_box_at(mark, along_x, along_y, reach);
    return box;
}

bool count_fits(const Bounds& box, const RimMark& own, const RimMark* marks, int mark_count,
                const Bounds& label) {
    if (box.left < 0 || box.top < 0 || box.right >= kGlassWidth || box.bottom >= kGlassHeight)
        return false;
    if (overlap(box, label)) return false;
    for (int i = 0; i < mark_count; i++)
        if (&marks[i] != &own && overlap(box, grown(mark_box(marks[i].plotted), kCountClearPx)))
            return false;
    return true;
}

bool abeam(const Plotted& plotted) {
    const int delta_y = plotted.y - kFar;
    return (delta_y < 0 ? -delta_y : delta_y) < kRimMarkPx / 2;
}

void rim_count(ui::Canvas& canvas, const RimMark& mark, const RimMark* marks, int mark_count,
               const Bounds& label) {
    const int delta_x = mark.plotted.x - kFar, delta_y = mark.plotted.y - kFar;
    int along_x = -delta_y, along_y = delta_x;
    if ((along_x > 0) != (delta_x >= 0)) {
        along_x = -along_x;
        along_y = -along_y;
    }
    if (abeam(mark.plotted)) {
        along_x = 0;
        along_y = kRimRadius;
    }
    const Bounds spots[] = {count_box_beside(mark, along_x, along_y),
                            count_box_beside(mark, -along_x, -along_y),
                            count_box_beside(mark, delta_x, delta_y)};
    const Bounds* box = nullptr;
    for (const Bounds& spot : spots)
        if (!box && count_fits(spot, mark, marks, mark_count, label)) box = &spot;
    if (!box) return;
    char digit[2] = {static_cast<char>('0' + (mark.count > 9 ? 9 : mark.count)), 0};
    canvas.draw_text(box->left + 1, box->top + 1, digit, true, 1);
}

struct InView {
    Plotted shown[kMaxRadarTargets];
    const RadarTarget* targets[kMaxRadarTargets];
    int target_count{0};
    int in_ring{0};
};

InView in_view(const RadarSnapshot& snapshot, int16_t track_angle16) {
    InView view;
    for (int i = 0; i < snapshot.target_count && view.target_count < kMaxRadarTargets; i++) {
        const Plotted plotted = plot_point(snapshot, snapshot.targets[i], track_angle16);
        if (!plotted.shown) continue;
        // A member of the formation is drawn once, as the square around own ship
        // and the count in its quadrant. Twice is two aircraft.
        if (snapshot.targets[i].in_formation) continue;
        if (plotted.in_ring) view.in_ring++;
        view.shown[view.target_count] = plotted;
        view.targets[view.target_count] = &snapshot.targets[i];
        view.target_count++;
    }
    return view;
}

int slid_x(const Plotted& plotted, const Bounds& label) {
    const int beside = ui::blip_beside(plotted.vertical);
    const bool rightwards = plotted.x > (label.left + label.right) / 2;
    return rightwards ? label.right + beside + 1 : label.left - beside - 1;
}

bool under_the_label(const Plotted& plotted, const Bounds& label) {
    const int beside = ui::blip_beside(plotted.vertical);
    const int above = ui::blip_above(plotted.vertical, plotted.trend);
    return plotted.x + beside >= label.left && plotted.x - beside <= label.right &&
           plotted.y + ui::blip_below(plotted.vertical, plotted.trend) >= label.top &&
           plotted.y - above <= label.bottom;
}

Plotted clear_of(const Plotted& plotted, const Bounds& label) {
    if (!under_the_label(plotted, label)) return plotted;
    Plotted moved = plotted;
    moved.x = slid_x(plotted, label);
    const int32_t delta_x = moved.x - kFar;
    moved.y = kFar + square_root<int32_t>(kRimRadius * kRimRadius - delta_x * delta_x);
    return moved;
}

}  // namespace

void plot_leaders(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16) {
    const InView view = in_view(snapshot, track_angle16);
    for (int i = 0; i < view.target_count; i++)
        draw_leader(canvas, snapshot, *view.targets[i], track_angle16, view.shown[i],
                    leader_of(snapshot, *view.targets[i], track_angle16));
    if (view.in_ring > 0) own_vector(canvas, snapshot, track_angle16);
}

void plot_blips(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16) {
    const InView view = in_view(snapshot, track_angle16);
    for (int i = 0; i < view.target_count; i++)
        if (view.shown[i].in_ring || snapshot.plot == RadarPlot::ToScale)
            plot_blip(canvas, view.shown[i], view.targets[i]->alarm_level);
}

void plot_rim(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16,
              const Bounds& label) {
    RimMark marks[kMaxRadarTargets];
    int ranked = 0;
    for (int i = 0; i < snapshot.target_count && ranked < kMaxRadarTargets; i++) {
        const RadarTarget& target = snapshot.targets[i];
        if (target.in_formation) continue;
        const Plotted plotted = plot_point(snapshot, target, track_angle16);
        if (plotted.in_ring) continue;
        const RimMark mark{clear_of(plotted, label), target.alarm_level,
                           target.up_m < 0 ? -target.up_m : target.up_m, apart_squared(plotted), 1};
        int at = ranked++;
        for (; at > 0 && outranks(mark, marks[at - 1]); at--) marks[at] = marks[at - 1];
        marks[at] = mark;
    }

    RimMark drawn[kMaxRadarTargets];
    int shown = 0;
    for (int i = 0; i < ranked; i++) {
        RimMark* covering = covered_by(drawn, shown, marks[i].plotted);
        if (covering)
            covering->count++;
        else
            drawn[shown++] = marks[i];
    }
    for (int i = 0; i < shown; i++)
        if (drawn[i].count > 1) rim_count(canvas, drawn[i], drawn, shown, label);
    for (int i = 0; i < shown; i++) plot_blip(canvas, drawn[i].plotted, drawn[i].level);
}

}  // namespace skyblip::go::radar
