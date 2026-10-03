#include "products/skyblip_go/pages/sats.h"

#include "core/util/format.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
constexpr int kCellW = 6;
constexpr int kGlyphH = 7;
constexpr int kRight = kGlassW - kLeft;

constexpr int kTitleY = 2;
constexpr int kRuleY = 13;

constexpr int kBarTop = 22;
constexpr int kBaseY = 132;
constexpr int kBarSpan = kBaseY - kBarTop;
constexpr int kBarW = 4;
constexpr int kBarPitch = kBarW + 1;
constexpr int kGroupGap = 6;
constexpr int kGroupLabelY = kBaseY + 4;

// INFO: fc 18sep26 GSV reports C/N0 in dB-Hz, 0 to 99, and 40 is a satellite being received well
constexpr int kFullScaleDbHz = 50;
constexpr int kGoodDbHz = 40;

constexpr int kDopY = 158;
constexpr int kPortY = 170;
constexpr int kNoteY = 184;

constexpr gnss::System kSystemOrder[] = {gnss::System::Gps, gnss::System::Beidou,
                                         gnss::System::Glonass, gnss::System::Qzss,
                                         gnss::System::Unknown};

void right_aligned(ui::Canvas& fb, int x_end, int y, const char* text, int len) {
    fb.draw_text(x_end - len * kCellW, y, text, true, 1);
}

int bar_height(uint8_t cn0_dbhz) {
    if (cn0_dbhz == 0) return 0;
    const int clipped = cn0_dbhz > kFullScaleDbHz ? kFullScaleDbHz : cn0_dbhz;
    return clipped * kBarSpan / kFullScaleDbHz;
}

void bar(ui::Canvas& fb, int x, const gnss::SatelliteView& sat) {
    const int h = bar_height(sat.cn0_dbhz);
    if (h <= 0) {
        fb.hline(x, kBaseY - 1, kBarW, true);
        return;
    }
    if (sat.in_use) {
        fb.rect(x, kBaseY - h, kBarW, h, true, true);
        return;
    }
    fb.rect(x + 1, kBaseY - h + 1, kBarW - 2, h - 2, false, true);
    fb.rect(x, kBaseY - h, kBarW, h, true, false);
}

int draw_group(ui::Canvas& fb, const gnss::SkyView& sky, gnss::System system, int x) {
    if (sky.in_view_of(system) == 0) return x;
    int at = x;
    for (int i = 0; i < sky.count(); i++) {
        const gnss::SatelliteView& sat = sky.at(i);
        if (sat.system != system) continue;
        if (at + kBarW > kRight) break;
        bar(fb, at, sat);
        at += kBarPitch;
    }
    fb.draw_text(x, kGroupLabelY, gnss::system_name(system), true, 1);
    return at + kGroupGap;
}

void scale_mark(ui::Canvas& fb) {
    const int y = kBaseY - kGoodDbHz * kBarSpan / kFullScaleDbHz;
    for (int x = kLeft; x < kRight; x += 4) fb.hline(x, y, 1, true);
    char buf[4];
    const int n = fmt_uint(buf, kGoodDbHz, 2);
    buf[n] = 0;
    right_aligned(fb, kRight, y - kGlyphH - 1, buf, n);
}

uint32_t used_count(const SatsSnapshot& s) {
    if (s.sky != nullptr && s.sky->in_use() > 0) return static_cast<uint32_t>(s.sky->in_use());
    return s.sats;
}

void draw_counts(ui::Canvas& fb, const SatsSnapshot& s) {
    char buf[24];
    int n = fmt_string(buf, "USED ");
    n += fmt_uint(buf + n, used_count(s), 1);
    if (s.levels_live && s.sky != nullptr) {
        n += fmt_string(buf + n, " OF ");
        n += fmt_uint(buf + n, static_cast<uint32_t>(s.sky->count()), 1);
    }
    buf[n] = 0;
    right_aligned(fb, kRight, kTitleY, buf, n);
}

void draw_dops(ui::Canvas& fb, const SatsSnapshot& s) {
    char buf[32];
    int n = fmt_string(buf, "HDOP ");
    if (s.hdop_e2 > 0)
        n += fmt_uint(buf + n, s.hdop_e2, 3, 2);
    else
        n += fmt_string(buf + n, "---");
    n += fmt_string(buf + n, "  VDOP ");
    if (s.vdop_e2 > 0)
        n += fmt_uint(buf + n, s.vdop_e2, 3, 2);
    else
        n += fmt_string(buf + n, "---");
    buf[n] = 0;
    fb.draw_text(kLeft, kDopY, buf, true, 1);
}

void draw_nav_phase(ui::Canvas& fb, const SatsSnapshot& s) {
    if (!s.nav_valid) return;
    char buf[12];
    int n = fmt_string(buf, "NAV ");
    n += fmt_uint(buf + n, s.nav_ms, 3);
    n += fmt_string(buf + n, "MS");
    buf[n] = 0;
    right_aligned(fb, kRight, kDopY, buf, n);
}

void draw_port(ui::Canvas& fb, const SatsSnapshot& s) {
    if (s.health.baud == 0) return;
    char buf[32];
    int n = fmt_string(buf, "BAUD ");
    n += fmt_uint(buf + n, s.health.baud);
    n += fmt_string(buf + n, "  ");
    n += fmt_string(buf + n, ports::to_string(s.health.config));
    if (s.health.overruns > 0) {
        n += fmt_string(buf + n, "  OVR ");
        n += fmt_uint(buf + n, s.health.overruns);
    }
    buf[n] = 0;
    fb.draw_text(kLeft, kPortY, buf, true, 1);
}

void draw_stage(ui::Canvas& fb, const SatsSnapshot& s) {
    if (s.fix_valid) return;
    fb.draw_text(kLeft, kDopY - 12, gnss::stage_name(s.stage), true, 1);
}

void draw_used_by_system(ui::Canvas& fb, const SatsSnapshot& s) {
    if (s.sky == nullptr) return;
    char buf[32];
    int n = 0;
    for (gnss::System system : kSystemOrder) {
        const int used = s.sky->in_use_of(system);
        if (used == 0) continue;
        if (n > 0) n += fmt_string(buf + n, "  ");
        n += fmt_string(buf + n, gnss::system_name(system));
        buf[n++] = ' ';
        n += fmt_uint(buf + n, static_cast<uint32_t>(used), 1);
    }
    if (n == 0) return;
    buf[n] = 0;
    fb.draw_text(kLeft, kBarTop, buf, true, 1);
}

}  // namespace

void draw_sats(ui::Canvas& fb, const SatsSnapshot& s) {
    fb.draw_text(kLeft, kTitleY, "SATELLITES", true, 1);
    draw_counts(fb, s);
    fb.hline(kLeft, kRuleY, kRight - kLeft, true);

    draw_dops(fb, s);
    draw_nav_phase(fb, s);
    draw_port(fb, s);
    draw_stage(fb, s);

    if (!s.levels_live || s.sky == nullptr || s.sky->count() == 0) {
        draw_used_by_system(fb, s);
        fb.draw_text(kLeft, kNoteY, used_count(s) == 0 ? "NO SATELLITE HEARD" : "LEVELS COMING UP",
                     true, 1);
        return;
    }

    scale_mark(fb);
    int x = kLeft;
    for (gnss::System system : kSystemOrder) x = draw_group(fb, *s.sky, system, x);
    fb.draw_text(kLeft, kNoteY, "FILLED: IN THE SOLUTION", true, 1);
}

}  // namespace skyblip::go
