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
constexpr int kBallClearR = kOuterR - kRingW - kMinuteBallR;
constexpr int kDashPx = 3;
constexpr int kDashGapPx = 3;
constexpr int kMinutesMarked = 2;

int px_of(int32_t right) { return right >= 0 ? kFar + right : kNear + right + 1; }
int py_of(int32_t ahead) { return ahead <= 0 ? kFar - ahead : kNear - ahead + 1; }

struct Plotted {
    int32_t right;
    int32_t ahead;
    int x;
    int y;
    bool in_ring;
    ui::Vertical vertical;
    ui::Trend trend;
};

ui::Vertical vertical_of(int32_t up_m) {
    const int32_t apart = up_m < 0 ? -up_m : up_m;
    if (apart <= kLevelM) return ui::Vertical::Level;
    const bool beyond = apart > traffic::kAdvisoryAltM;
    if (up_m > 0) return beyond ? ui::Vertical::FarAbove : ui::Vertical::Above;
    return beyond ? ui::Vertical::FarBelow : ui::Vertical::Below;
}

ui::Trend trend_of(const RadarTarget& t) {
    if (!t.climb_valid) return ui::Trend::Steady;
    if (t.climb_e8 >= kCaretClimbE8) return ui::Trend::Climbing;
    if (t.climb_e8 <= -kCaretClimbE8) return ui::Trend::Sinking;
    return ui::Trend::Steady;
}

int64_t ring_metres(const RadarSnapshot& snap) {
    return go::range_metres(snap.range_step, snap.units);
}

int32_t to_px(int32_t metres, int64_t range) {
    return static_cast<int32_t>((static_cast<int64_t>(metres) * kOuterR) / range);
}

bool inside_ring(int32_t right, int32_t ahead) {
    return right * right + ahead * ahead <= kOuterR * kOuterR;
}

bool on_glass(int x, int y) { return x >= 0 && x < kGlassW && y >= 0 && y < kGlassH; }

bool plot_point(const RadarSnapshot& snap, const RadarTarget& t, int16_t track, Plotted& out) {
    const int64_t range = ring_metres(snap);
    const HeadingUp at = heading_up(t.north_m, t.east_m, track);
    const int32_t dx = to_px(at.right, range), dy = to_px(at.ahead, range);
    const int x = px_of(dx), y = py_of(dy);
    if (!on_glass(x, y)) return false;
    const ui::Vertical vertical = vertical_of(t.up_m);
    const ui::Trend trend = trend_of(t);
    if (!inside_ring(dx, dy) && y + ui::blip_below(vertical, trend) >= kFooterTop) return false;
    out = {dx, dy, x, y, inside_ring(dx, dy), vertical, trend};
    return true;
}

flight::Motion motion_of(int32_t speed_mm_s, int32_t track_cdeg, int16_t turn_cdps, bool turning) {
    flight::Motion m{};
    m.speed_mm_s = speed_mm_s;
    m.track_cdeg = track_cdeg;
    m.turn_cdps = turn_cdps;
    m.turning = turning;
    return m;
}

HeadingUp on_glass_at(const flight::Position& p, const RadarSnapshot& snap, int16_t track) {
    const HeadingUp at = heading_up(p.north_m, p.east_m, track);
    const int64_t range = ring_metres(snap);
    return {to_px(at.ahead, range), to_px(at.right, range)};
}

void minute_ball(ui::Canvas& fb, int x, int y) {
    const int r = kMinuteBallR;
    for (int dy = -r; dy < r; dy++)
        for (int dx = -r; dx < r; dx++) {
            const int px = 2 * dx + 1, py = 2 * dy + 1;
            if (px * px + py * py <= 4 * r * r) fb.set_pixel(x + dx, y + dy, true);
        }
}

struct DashPen {
    ui::Canvas& fb;
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
        fb.set_pixel(at_x, at_y, true);
        fb.set_pixel(steep ? at_x - 1 : at_x, steep ? at_y : at_y - 1, true);
    }

    void stroke(int x1, int y1) {
        int at_x = x, at_y = y;
        const int dx = x1 > at_x ? x1 - at_x : at_x - x1;
        const int dy = y1 > at_y ? y1 - at_y : at_y - y1;
        const int sx = at_x < x1 ? 1 : -1, sy = at_y < y1 ? 1 : -1;
        const bool steep = dy >= dx;
        int err = dx - dy;
        if (fresh) dash(at_x, at_y, steep);
        fresh = false;
        while (at_x != x1 || at_y != y1) {
            const int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                at_x += sx;
            }
            if (e2 < dx) {
                err += dx;
                at_y += sy;
            }
            dash(at_x, at_y, steep);
        }
    }
};

void own_vector(ui::Canvas& fb, const RadarSnapshot& snap, int16_t track) {
    if (snap.speed_mm_s <= 0) return;
    flight::Arc arc(motion_of(snap.speed_mm_s, snap.track_cdeg, snap.turn_cdps, true),
                    kLeaderStepMs);
    DashPen pen{fb};
    for (int step = 1; step <= kMinutesMarked * kStepsPerMinute; step++) {
        const HeadingUp at = on_glass_at(arc.advance(), snap, track);
        if (!inside_ring(at.right, at.ahead)) return;
        const int32_t out = at.right * at.right + at.ahead * at.ahead;
        if (out < kMinuteClearPx * kMinuteClearPx) {
            pen.lift();
            continue;
        }
        const int x = px_of(at.right), y = py_of(at.ahead);
        pen.to(x, y);
        if (step % kStepsPerMinute != 0) continue;
        if (out <= kBallClearR * kBallClearR) minute_ball(fb, x, y);
    }
}

struct Leader {
    int32_t right;
    int32_t ahead;
    bool valid;
};

Leader leader_of(const RadarSnapshot& snap, const RadarTarget& t, int16_t track) {
    if (t.speed_mm_s <= 0) return {0, 0, false};
    flight::Arc arc(motion_of(t.speed_mm_s, t.track_cdeg, t.turn_cdps, t.turn_valid),
                    kLeaderStepMs);
    HeadingUp end{0, 0};
    for (int step = 0; step < kStepsPerMinute; step++)
        end = on_glass_at(arc.advance(), snap, track);
    if (end.right * end.right + end.ahead * end.ahead < kMinLeaderPx * kMinLeaderPx)
        return {0, 0, false};
    return {end.right, end.ahead, true};
}

void draw_leader(ui::Canvas& fb, const RadarSnapshot& snap, const RadarTarget& t, int16_t track,
                 const Plotted& p, const Leader& v) {
    if (!v.valid) return;
    flight::Arc arc(motion_of(t.speed_mm_s, t.track_cdeg, t.turn_cdps, t.turn_valid),
                    kLeaderStepMs);
    int from_x = p.x, from_y = p.y;
    for (int step = 0; step < kStepsPerMinute; step++) {
        const HeadingUp at = on_glass_at(arc.advance(), snap, track);
        const int to_x = px_of(p.right + at.right), to_y = py_of(p.ahead + at.ahead);
        fb.line(from_x, from_y, to_x, to_y, true);
        from_x = to_x;
        from_y = to_y;
    }
}

void plot_blip(ui::Canvas& fb, const Plotted& p, traffic::Level alarm_level) {
    ui::draw_blip(fb, p.x, p.y, p.vertical, p.trend, alarm_level >= traffic::Level::Advisory);
}

}  // namespace

int plot(ui::Canvas& fb, const RadarSnapshot& snap, int16_t track) {
    Plotted shown[kMaxRadarTargets];
    const RadarTarget* in_view[kMaxRadarTargets];
    int n = 0;
    int in_ring = 0;
    for (int i = 0; i < snap.n_targets && n < kMaxRadarTargets; i++) {
        Plotted p;
        if (!plot_point(snap, snap.targets[i], track, p)) continue;
        // A member of the formation is drawn once, as the square around own ship
        // and the count in its quadrant. Twice is two aircraft.
        if (snap.targets[i].in_formation) {
            if (p.in_ring) in_ring++;
            continue;
        }
        shown[n] = p;
        in_view[n] = &snap.targets[i];
        n++;
    }

    for (int i = 0; i < n; i++)
        if (shown[i].in_ring) in_ring++;

    for (int i = 0; i < n; i++)
        draw_leader(fb, snap, *in_view[i], track, shown[i], leader_of(snap, *in_view[i], track));
    if (in_ring > 0) own_vector(fb, snap, track);

    for (int i = 0; i < n; i++) plot_blip(fb, shown[i], in_view[i]->alarm_level);
    return in_ring;
}

}  // namespace skyblip::go::radar
