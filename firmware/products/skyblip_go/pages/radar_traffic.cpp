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
constexpr int kMinuteBallR = 3;
constexpr int kDashPx = 3;
constexpr int kDashGapPx = 3;
constexpr int kMinutesMarked = 2;
constexpr int kRimMarkPx = 15;
constexpr int kCountReachPx = 30;
using ui::kCharH;
using ui::kCharW;
constexpr int kCountClearPx = 4;

int px_of(int32_t right) { return right >= 0 ? kFar + right : kNear + right + 1; }
int py_of(int32_t ahead) { return ahead <= 0 ? kFar - ahead : kNear - ahead + 1; }

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

ui::Trend trend_of(const RadarTarget& t) {
    if (!t.climb_valid) return ui::Trend::Steady;
    if (t.climb_e8 >= kCaretClimbE8) return ui::Trend::Climbing;
    if (t.climb_e8 <= -kCaretClimbE8) return ui::Trend::Sinking;
    return ui::Trend::Steady;
}

int64_t ring_metres(const RadarSnapshot& snapshot) {
    return go::range_metres(snapshot.range_step, snapshot.units);
}

int32_t to_px(int32_t metres, int64_t range, int ring) {
    return static_cast<int32_t>((static_cast<int64_t>(metres) * ring) / range);
}

bool inside_ring(int32_t right, int32_t ahead, int ring) {
    return right * right + ahead * ahead <= ring * ring;
}

bool on_glass(int x, int y) { return x >= 0 && x < kGlassWidth && y >= 0 && y < kGlassHeight; }

bool pixel_within(int x, int y, int r) {
    const int64_t px = 2 * (x - kFar) + 1, py = 2 * (y - kFar) + 1;
    return px * px + py * py < 4 * static_cast<int64_t>(r) * r;
}

HeadingUp on_the_rim(int32_t right, int32_t ahead) {
    const int64_t r = right, a = ahead;
    const int64_t apart = square_root<int64_t>(r * r + a * a);
    return {static_cast<int32_t>(a * kRimR / apart), static_cast<int32_t>(r * kRimR / apart)};
}

Plotted plot_point(const RadarSnapshot& snapshot, const RadarTarget& t, int16_t track_angle16) {
    const int ring = ring_r(snapshot.plot);
    const int64_t range = ring_metres(snapshot);
    const HeadingUp at = heading_up(t.north_m, t.east_m, track_angle16);
    const int32_t dx = to_px(at.right, range, ring), dy = to_px(at.ahead, range, ring);
    const bool in_ring = inside_ring(dx, dy, ring);
    const bool pinned = !in_ring && snapshot.plot == RadarPlot::All;
    const HeadingUp drawn = pinned ? on_the_rim(dx, dy) : HeadingUp{dy, dx};
    Plotted plotted{dx,      dy,   px_of(drawn.right),  py_of(drawn.ahead),
                    in_ring, true, vertical_of(t.up_m), trend_of(t)};
    plotted.shown = pinned || on_glass(plotted.x, plotted.y);
    return plotted;
}

flight::Motion motion_of(int32_t speed_mm_s, int32_t track_cdeg, int16_t turn_cdps, bool turning) {
    flight::Motion m{};
    m.speed_mm_s = speed_mm_s;
    m.track_cdeg = track_cdeg;
    m.turn_cdps = turn_cdps;
    m.turning = turning;
    return m;
}

HeadingUp on_glass_at(const flight::Position& p, const RadarSnapshot& snapshot,
                      int16_t track_angle16) {
    const HeadingUp at = heading_up(p.north_m, p.east_m, track_angle16);
    const int64_t range = ring_metres(snapshot);
    const int ring = ring_r(snapshot.plot);
    return {to_px(at.ahead, range, ring), to_px(at.right, range, ring)};
}

void minute_ball(ui::Canvas& canvas, int x, int y) {
    const int r = kMinuteBallR;
    for (int dy = -r; dy < r; dy++)
        for (int dx = -r; dx < r; dx++) {
            const int px = 2 * dx + 1, py = 2 * dy + 1;
            if (px * px + py * py <= 4 * r * r) canvas.set_pixel(x + dx, y + dy, true);
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
        const int dx = x1 > at_x ? x1 - at_x : at_x - x1;
        const int dy = y1 > at_y ? y1 - at_y : at_y - y1;
        const int sx = at_x < x1 ? 1 : -1, sy = at_y < y1 ? 1 : -1;
        const bool steep = dy >= dx;
        int error = dx - dy;
        if (fresh) dash(at_x, at_y, steep);
        fresh = false;
        while (at_x != x1 || at_y != y1) {
            const int e2 = 2 * error;
            if (e2 > -dy) {
                error -= dy;
                at_x += sx;
            }
            if (e2 < dx) {
                error += dx;
                at_y += sy;
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
    const int ring = ring_r(snapshot.plot);
    const int ball_clear = ring - kRingW - kMinuteBallR;
    for (int step = 1; step <= kMinutesMarked * kStepsPerMinute; step++) {
        const HeadingUp at = on_glass_at(arc.advance(), snapshot, track_angle16);
        if (!inside_ring(at.right, at.ahead, ring)) return;
        const int32_t out = at.right * at.right + at.ahead * at.ahead;
        if (out < kMinuteClearPx * kMinuteClearPx) {
            pen.lift();
            continue;
        }
        const int x = px_of(at.right), y = py_of(at.ahead);
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

Leader leader_of(const RadarSnapshot& snapshot, const RadarTarget& t, int16_t track_angle16) {
    if (t.speed_mm_s <= 0) return {0, 0, false};
    flight::Arc arc(motion_of(t.speed_mm_s, t.track_cdeg, t.turn_cdps, t.turn_valid),
                    kLeaderStepMs);
    HeadingUp end{0, 0};
    for (int step = 0; step < kStepsPerMinute; step++)
        end = on_glass_at(arc.advance(), snapshot, track_angle16);
    if (end.right * end.right + end.ahead * end.ahead < kMinLeaderPx * kMinLeaderPx)
        return {0, 0, false};
    return {end.right, end.ahead, true};
}

void line_inside_ring(ui::Canvas& canvas, int x0, int y0, int x1, int y1, int ring) {
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx - dy;
    for (;;) {
        if (pixel_within(x0, y0, ring - kRingW)) canvas.set_pixel(x0, y0, true);
        if (x0 == x1 && y0 == y1) return;
        const int e2 = 2 * error;
        if (e2 > -dy) {
            error -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void draw_leader(ui::Canvas& canvas, const RadarSnapshot& snapshot, const RadarTarget& t,
                 int16_t track_angle16, const Plotted& plotted, const Leader& leader) {
    if (!leader.valid) return;
    flight::Arc arc(motion_of(t.speed_mm_s, t.track_cdeg, t.turn_cdps, t.turn_valid),
                    kLeaderStepMs);
    int from_x = px_of(plotted.right), from_y = py_of(plotted.ahead);
    for (int step = 0; step < kStepsPerMinute; step++) {
        const HeadingUp at = on_glass_at(arc.advance(), snapshot, track_angle16);
        const int to_x = px_of(plotted.right + at.right), to_y = py_of(plotted.ahead + at.ahead);
        if (snapshot.plot == RadarPlot::All)
            line_inside_ring(canvas, from_x, from_y, to_x, to_y, kAllR);
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

bool outranks(const RimMark& a, const RimMark& b) {
    if (a.level != b.level) return a.level > b.level;
    if (a.separation_m != b.separation_m) return a.separation_m < b.separation_m;
    return a.apart_squared < b.apart_squared;
}

RimMark* covered_by(RimMark* drawn, int n, const Plotted& plotted) {
    for (int i = 0; i < n; i++) {
        const int dx = drawn[i].plotted.x - plotted.x, dy = drawn[i].plotted.y - plotted.y;
        if ((dx < 0 ? -dx : dx) < kRimMarkPx && (dy < 0 ? -dy : dy) < kRimMarkPx) return &drawn[i];
    }
    return nullptr;
}

Rect mark_box(const Plotted& plotted) {
    const int beside = ui::blip_beside(plotted.vertical);
    return {plotted.x - beside, plotted.y - ui::blip_above(plotted.vertical, plotted.trend) - 1,
            plotted.x + beside, plotted.y + ui::blip_below(plotted.vertical, plotted.trend) + 1};
}

Rect grown(const Rect& r, int by) { return {r.left - by, r.top - by, r.right + by, r.bottom + by}; }

bool overlap(const Rect& a, const Rect& b) {
    return a.left <= b.right && b.left <= a.right && a.top <= b.bottom && b.top <= a.bottom;
}

Rect count_box_at(const RimMark& mark, int along_x, int along_y, int reach) {
    const int x = mark.plotted.x + along_x * reach / kRimR - kCharW / 2;
    const int y = mark.plotted.y + along_y * reach / kRimR - kCharH / 2;
    return {x - 1, y - 1, x + kCharW, y + kCharH};
}

Rect count_box_beside(const RimMark& mark, int along_x, int along_y) {
    const Rect own = mark_box(mark.plotted);
    Rect box = count_box_at(mark, along_x, along_y, 0);
    for (int reach = 1; reach <= kCountReachPx && overlap(box, own); reach++)
        box = count_box_at(mark, along_x, along_y, reach);
    return box;
}

bool count_fits(const Rect& box, const RimMark& own, const RimMark* marks, int n,
                const Rect& label) {
    if (box.left < 0 || box.top < 0 || box.right >= kGlassWidth || box.bottom >= kGlassHeight)
        return false;
    if (overlap(box, label)) return false;
    for (int i = 0; i < n; i++)
        if (&marks[i] != &own && overlap(box, grown(mark_box(marks[i].plotted), kCountClearPx)))
            return false;
    return true;
}

bool abeam(const Plotted& plotted) {
    const int dy = plotted.y - kFar;
    return (dy < 0 ? -dy : dy) < kRimMarkPx / 2;
}

void rim_count(ui::Canvas& canvas, const RimMark& mark, const RimMark* marks, int n,
               const Rect& label) {
    const int dx = mark.plotted.x - kFar, dy = mark.plotted.y - kFar;
    int along_x = -dy, along_y = dx;
    if ((along_x > 0) != (dx >= 0)) {
        along_x = -along_x;
        along_y = -along_y;
    }
    if (abeam(mark.plotted)) {
        along_x = 0;
        along_y = kRimR;
    }
    const Rect spots[] = {count_box_beside(mark, along_x, along_y),
                          count_box_beside(mark, -along_x, -along_y),
                          count_box_beside(mark, dx, dy)};
    const Rect* box = nullptr;
    for (const Rect& spot : spots)
        if (!box && count_fits(spot, mark, marks, n, label)) box = &spot;
    if (!box) return;
    char digit[2] = {static_cast<char>('0' + (mark.count > 9 ? 9 : mark.count)), 0};
    canvas.draw_text(box->left + 1, box->top + 1, digit, true, 1);
}

struct InView {
    Plotted shown[kMaxRadarTargets];
    const RadarTarget* target[kMaxRadarTargets];
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
        view.target[view.target_count] = &snapshot.targets[i];
        view.target_count++;
    }
    return view;
}

int slid_x(const Plotted& plotted, const Rect& label) {
    const int beside = ui::blip_beside(plotted.vertical);
    const bool rightwards = plotted.x > (label.left + label.right) / 2;
    return rightwards ? label.right + beside + 1 : label.left - beside - 1;
}

bool under_the_label(const Plotted& plotted, const Rect& label) {
    const int beside = ui::blip_beside(plotted.vertical);
    const int above = ui::blip_above(plotted.vertical, plotted.trend);
    return plotted.x + beside >= label.left && plotted.x - beside <= label.right &&
           plotted.y + ui::blip_below(plotted.vertical, plotted.trend) >= label.top &&
           plotted.y - above <= label.bottom;
}

Plotted clear_of(const Plotted& plotted, const Rect& label) {
    if (!under_the_label(plotted, label)) return plotted;
    Plotted moved = plotted;
    moved.x = slid_x(plotted, label);
    const int32_t dx = moved.x - kFar;
    moved.y = kFar + square_root<int32_t>(kRimR * kRimR - dx * dx);
    return moved;
}

}  // namespace

void plot_leaders(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16) {
    const InView view = in_view(snapshot, track_angle16);
    for (int i = 0; i < view.target_count; i++)
        draw_leader(canvas, snapshot, *view.target[i], track_angle16, view.shown[i],
                    leader_of(snapshot, *view.target[i], track_angle16));
    if (view.in_ring > 0) own_vector(canvas, snapshot, track_angle16);
}

void plot_blips(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16) {
    const InView view = in_view(snapshot, track_angle16);
    for (int i = 0; i < view.target_count; i++)
        if (view.shown[i].in_ring || snapshot.plot == RadarPlot::ToScale)
            plot_blip(canvas, view.shown[i], view.target[i]->alarm_level);
}

void plot_rim(ui::Canvas& canvas, const RadarSnapshot& snapshot, int16_t track_angle16,
              const Rect& label) {
    RimMark marks[kMaxRadarTargets];
    int n = 0;
    for (int i = 0; i < snapshot.target_count && n < kMaxRadarTargets; i++) {
        const RadarTarget& t = snapshot.targets[i];
        if (t.in_formation) continue;
        const Plotted plotted = plot_point(snapshot, t, track_angle16);
        if (plotted.in_ring) continue;
        const RimMark mark{clear_of(plotted, label), t.alarm_level, t.up_m < 0 ? -t.up_m : t.up_m,
                           apart_squared(plotted), 1};
        int at = n++;
        for (; at > 0 && outranks(mark, marks[at - 1]); at--) marks[at] = marks[at - 1];
        marks[at] = mark;
    }

    RimMark drawn[kMaxRadarTargets];
    int shown = 0;
    for (int i = 0; i < n; i++) {
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
