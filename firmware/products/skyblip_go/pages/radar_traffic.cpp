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
constexpr int kCountW = 5;
constexpr int kCountH = 7;
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

int32_t to_px(int32_t metres, int64_t range, int ring) {
    return static_cast<int32_t>((static_cast<int64_t>(metres) * ring) / range);
}

bool inside_ring(int32_t right, int32_t ahead, int ring) {
    return right * right + ahead * ahead <= ring * ring;
}

bool on_glass(int x, int y) { return x >= 0 && x < kGlassW && y >= 0 && y < kGlassH; }

bool pixel_within(int x, int y, int r) {
    const int64_t px = 2 * (x - kFar) + 1, py = 2 * (y - kFar) + 1;
    return px * px + py * py < 4 * static_cast<int64_t>(r) * r;
}

HeadingUp on_the_rim(int32_t right, int32_t ahead) {
    const int64_t r = right, a = ahead;
    const int64_t apart = isqrt<int64_t>(r * r + a * a);
    return {static_cast<int32_t>(a * kRimR / apart), static_cast<int32_t>(r * kRimR / apart)};
}

bool clear_of_the_footer(const Plotted& p) {
    return p.in_ring || p.y + ui::blip_below(p.vertical, p.trend) < kFooterTop;
}

Plotted plot_point(const RadarSnapshot& snap, const RadarTarget& t, int16_t track) {
    const int ring = ring_r(snap.plot);
    const int64_t range = ring_metres(snap);
    const HeadingUp at = heading_up(t.north_m, t.east_m, track);
    const int32_t dx = to_px(at.right, range, ring), dy = to_px(at.ahead, range, ring);
    const bool in_ring = inside_ring(dx, dy, ring);
    const bool pinned = !in_ring && snap.plot == RadarPlot::All;
    const HeadingUp drawn = pinned ? on_the_rim(dx, dy) : HeadingUp{dy, dx};
    Plotted p{dx,      dy,   px_of(drawn.right),  py_of(drawn.ahead),
              in_ring, true, vertical_of(t.up_m), trend_of(t)};
    p.shown = pinned || (on_glass(p.x, p.y) && clear_of_the_footer(p));
    return p;
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
    const int ring = ring_r(snap.plot);
    return {to_px(at.ahead, range, ring), to_px(at.right, range, ring)};
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
    const int ring = ring_r(snap.plot);
    const int ball_clear = ring - kRingW - kMinuteBallR;
    for (int step = 1; step <= kMinutesMarked * kStepsPerMinute; step++) {
        const HeadingUp at = on_glass_at(arc.advance(), snap, track);
        if (!inside_ring(at.right, at.ahead, ring)) return;
        const int32_t out = at.right * at.right + at.ahead * at.ahead;
        if (out < kMinuteClearPx * kMinuteClearPx) {
            pen.lift();
            continue;
        }
        const int x = px_of(at.right), y = py_of(at.ahead);
        pen.to(x, y);
        if (step % kStepsPerMinute != 0) continue;
        if (out <= ball_clear * ball_clear) minute_ball(fb, x, y);
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

void line_inside_ring(ui::Canvas& fb, int x0, int y0, int x1, int y1, int ring) {
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        if (pixel_within(x0, y0, ring - kRingW)) fb.set_pixel(x0, y0, true);
        if (x0 == x1 && y0 == y1) return;
        const int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void draw_leader(ui::Canvas& fb, const RadarSnapshot& snap, const RadarTarget& t, int16_t track,
                 const Plotted& p, const Leader& v) {
    if (!v.valid) return;
    flight::Arc arc(motion_of(t.speed_mm_s, t.track_cdeg, t.turn_cdps, t.turn_valid),
                    kLeaderStepMs);
    int from_x = px_of(p.right), from_y = py_of(p.ahead);
    for (int step = 0; step < kStepsPerMinute; step++) {
        const HeadingUp at = on_glass_at(arc.advance(), snap, track);
        const int to_x = px_of(p.right + at.right), to_y = py_of(p.ahead + at.ahead);
        if (snap.plot == RadarPlot::All)
            line_inside_ring(fb, from_x, from_y, to_x, to_y, kAllR);
        else
            fb.line(from_x, from_y, to_x, to_y, true);
        from_x = to_x;
        from_y = to_y;
    }
}

void plot_blip(ui::Canvas& fb, const Plotted& p, traffic::Level alarm_level) {
    ui::draw_blip(fb, p.x, p.y, p.vertical, p.trend, alarm_level >= traffic::Level::Advisory);
}

void clear_under(ui::Canvas& fb, const Plotted& p) {
    const int beside = ui::blip_beside(p.vertical);
    const int above = ui::blip_above(p.vertical, p.trend) + 1;
    const int below = ui::blip_below(p.vertical, p.trend) + 1;
    for (int y = p.y - above; y <= p.y + below; y++)
        for (int x = p.x - beside; x <= p.x + beside; x++)
            if (!pixel_within(x, y, kAllR)) fb.set_pixel(x, y, false);
}

int64_t apart2(const Plotted& p) {
    return static_cast<int64_t>(p.right) * p.right + static_cast<int64_t>(p.ahead) * p.ahead;
}

struct RimMark {
    Plotted p;
    traffic::Level level;
    int32_t separation_m;
    int64_t apart2;
    int count;
};

bool outranks(const RimMark& a, const RimMark& b) {
    if (a.level != b.level) return a.level > b.level;
    if (a.separation_m != b.separation_m) return a.separation_m < b.separation_m;
    return a.apart2 < b.apart2;
}

RimMark* covered_by(RimMark* drawn, int n, const Plotted& p) {
    for (int i = 0; i < n; i++) {
        const int dx = drawn[i].p.x - p.x, dy = drawn[i].p.y - p.y;
        if ((dx < 0 ? -dx : dx) < kRimMarkPx && (dy < 0 ? -dy : dy) < kRimMarkPx) return &drawn[i];
    }
    return nullptr;
}

Rect mark_box(const Plotted& p) {
    const int beside = ui::blip_beside(p.vertical);
    return {p.x - beside, p.y - ui::blip_above(p.vertical, p.trend) - 1, p.x + beside,
            p.y + ui::blip_below(p.vertical, p.trend) + 1};
}

Rect grown(const Rect& r, int by) { return {r.left - by, r.top - by, r.right + by, r.bottom + by}; }

bool overlap(const Rect& a, const Rect& b) {
    return a.left <= b.right && b.left <= a.right && a.top <= b.bottom && b.top <= a.bottom;
}

Rect count_box_at(const RimMark& m, int along_x, int along_y, int reach) {
    const int x = m.p.x + along_x * reach / kRimR - kCountW / 2;
    const int y = m.p.y + along_y * reach / kRimR - kCountH / 2;
    return {x - 1, y - 1, x + kCountW, y + kCountH};
}

Rect count_box_beside(const RimMark& m, int along_x, int along_y) {
    const Rect own = mark_box(m.p);
    Rect box = count_box_at(m, along_x, along_y, 0);
    for (int reach = 1; reach <= kCountReachPx && overlap(box, own); reach++)
        box = count_box_at(m, along_x, along_y, reach);
    return box;
}

bool count_fits(const Rect& box, const RimMark& own, const RimMark* marks, int n,
                const Rect& label) {
    if (box.left < 0 || box.top < 0 || box.right >= kGlassW || box.bottom >= kGlassH) return false;
    if (overlap(box, label)) return false;
    for (int i = 0; i < n; i++)
        if (&marks[i] != &own && overlap(box, grown(mark_box(marks[i].p), kCountClearPx)))
            return false;
    return true;
}

bool abeam(const Plotted& p) {
    const int dy = p.y - kFar;
    return (dy < 0 ? -dy : dy) < kRimMarkPx / 2;
}

void rim_count(ui::Canvas& fb, const RimMark& m, const RimMark* marks, int n, const Rect& label) {
    const int dx = m.p.x - kFar, dy = m.p.y - kFar;
    int along_x = -dy, along_y = dx;
    if ((along_x > 0) != (dx >= 0)) {
        along_x = -along_x;
        along_y = -along_y;
    }
    if (abeam(m.p)) {
        along_x = 0;
        along_y = kRimR;
    }
    const Rect spots[] = {count_box_beside(m, along_x, along_y),
                          count_box_beside(m, -along_x, -along_y), count_box_beside(m, dx, dy)};
    const Rect* box = nullptr;
    for (const Rect& spot : spots)
        if (!box && count_fits(spot, m, marks, n, label)) box = &spot;
    if (!box) return;
    char digit[2] = {static_cast<char>('0' + (m.count > 9 ? 9 : m.count)), 0};
    fb.rect(box->left, box->top, kCountW + 2, kCountH + 2, false, true);
    fb.draw_text(box->left + 1, box->top + 1, digit, true, 1);
}

struct InView {
    Plotted shown[kMaxRadarTargets];
    const RadarTarget* target[kMaxRadarTargets];
    int n{0};
    int in_ring{0};
};

InView in_view(const RadarSnapshot& snap, int16_t track) {
    InView v;
    for (int i = 0; i < snap.n_targets && v.n < kMaxRadarTargets; i++) {
        const Plotted p = plot_point(snap, snap.targets[i], track);
        if (!p.shown) continue;
        if (p.in_ring) v.in_ring++;
        // A member of the formation is drawn once, as the square around own ship
        // and the count in its quadrant. Twice is two aircraft.
        if (snap.targets[i].in_formation) continue;
        v.shown[v.n] = p;
        v.target[v.n] = &snap.targets[i];
        v.n++;
    }
    return v;
}

int slid_x(const Plotted& p, const Rect& c) {
    const int beside = ui::blip_beside(p.vertical);
    const bool rightwards = p.x > (c.left + c.right) / 2;
    return rightwards ? c.right + beside + 1 : c.left - beside - 1;
}

bool under_the_label(const Plotted& p, const Rect& c) {
    const int beside = ui::blip_beside(p.vertical);
    const int above = ui::blip_above(p.vertical, p.trend);
    return p.x + beside >= c.left && p.x - beside <= c.right &&
           p.y + ui::blip_below(p.vertical, p.trend) >= c.top && p.y - above <= c.bottom;
}

Plotted clear_of(const Plotted& p, const Rect& c) {
    if (!under_the_label(p, c)) return p;
    Plotted moved = p;
    moved.x = slid_x(p, c);
    const int32_t dx = moved.x - kFar;
    moved.y = kFar + isqrt<int32_t>(kRimR * kRimR - dx * dx);
    return moved;
}

}  // namespace

void plot_leaders(ui::Canvas& fb, const RadarSnapshot& snap, int16_t track) {
    const InView v = in_view(snap, track);
    for (int i = 0; i < v.n; i++)
        draw_leader(fb, snap, *v.target[i], track, v.shown[i],
                    leader_of(snap, *v.target[i], track));
    if (v.in_ring > 0) own_vector(fb, snap, track);
}

void plot_blips(ui::Canvas& fb, const RadarSnapshot& snap, int16_t track) {
    const InView v = in_view(snap, track);
    for (int i = 0; i < v.n; i++)
        if (v.shown[i].in_ring || snap.plot == RadarPlot::ToScale)
            plot_blip(fb, v.shown[i], v.target[i]->alarm_level);
}

void plot_rim(ui::Canvas& fb, const RadarSnapshot& snap, int16_t track, const Rect& label) {
    RimMark marks[kMaxRadarTargets];
    int n = 0;
    for (int i = 0; i < snap.n_targets && n < kMaxRadarTargets; i++) {
        const RadarTarget& t = snap.targets[i];
        if (t.in_formation) continue;
        const Plotted p = plot_point(snap, t, track);
        if (p.in_ring) continue;
        const RimMark mark{clear_of(p, label), t.alarm_level, t.up_m < 0 ? -t.up_m : t.up_m,
                           apart2(p), 1};
        int at = n++;
        for (; at > 0 && outranks(mark, marks[at - 1]); at--) marks[at] = marks[at - 1];
        marks[at] = mark;
    }

    RimMark drawn[kMaxRadarTargets];
    int shown = 0;
    for (int i = 0; i < n; i++) {
        RimMark* covering = covered_by(drawn, shown, marks[i].p);
        if (covering)
            covering->count++;
        else
            drawn[shown++] = marks[i];
    }
    for (int i = 0; i < shown; i++)
        if (drawn[i].count > 1) rim_count(fb, drawn[i], drawn, shown, label);
    for (int i = shown - 1; i >= 0; i--) {
        clear_under(fb, drawn[i].p);
        plot_blip(fb, drawn[i].p, drawn[i].level);
    }
}

}  // namespace skyblip::go::radar
