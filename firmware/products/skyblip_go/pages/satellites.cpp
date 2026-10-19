#include "products/skyblip_go/pages/satellites.h"

#include "core/util/format.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
using ui::kCellWidth;
using ui::kCharacterHeight;
constexpr int kRight = kGlassWidth - kLeft;

constexpr int kTitleY = 2;
constexpr int kRuleY = 13;

constexpr int kBarTop = 22;
constexpr int kBaseY = 132;
constexpr int kBarSpan = kBaseY - kBarTop;
constexpr int kBarWidth = 4;
constexpr int kBarPitch = kBarWidth + 1;
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

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int length) {
    canvas.draw_text(x_end - length * kCellWidth, y, text, true, 1);
}

int bar_height(uint8_t cn0_dbhz) {
    if (cn0_dbhz == 0) return 0;
    const int clipped = cn0_dbhz > kFullScaleDbHz ? kFullScaleDbHz : cn0_dbhz;
    return clipped * kBarSpan / kFullScaleDbHz;
}

void bar(ui::Canvas& canvas, int x, const gnss::SatelliteView& satellite) {
    const int height = bar_height(satellite.cn0_dbhz);
    if (height <= 0) {
        canvas.horizontal_line(x, kBaseY - 1, kBarWidth, true);
        return;
    }
    if (satellite.in_use) {
        canvas.rectangle(x, kBaseY - height, kBarWidth, height, true, true);
        return;
    }
    canvas.rectangle(x + 1, kBaseY - height + 1, kBarWidth - 2, height - 2, false, true);
    canvas.rectangle(x, kBaseY - height, kBarWidth, height, true, false);
}

int draw_group(ui::Canvas& canvas, const gnss::SkyView& sky, gnss::System system, int x) {
    if (sky.in_view_of(system) == 0) return x;
    int at = x;
    for (int i = 0; i < sky.count(); i++) {
        const gnss::SatelliteView& satellite = sky.at(i);
        if (satellite.system != system) continue;
        if (at + kBarWidth > kRight) break;
        bar(canvas, at, satellite);
        at += kBarPitch;
    }
    canvas.draw_text(x, kGroupLabelY, gnss::system_name(system), true, 1);
    return at + kGroupGap;
}

void scale_mark(ui::Canvas& canvas) {
    const int y = kBaseY - kGoodDbHz * kBarSpan / kFullScaleDbHz;
    for (int x = kLeft; x < kRight; x += 4) canvas.horizontal_line(x, y, 1, true);
    char text[4];
    const int written = format_unsigned(text, kGoodDbHz, 2);
    text[written] = 0;
    right_aligned(canvas, kRight, y - kCharacterHeight - 1, text, written);
}

uint32_t used_count(const SatellitesSnapshot& snapshot) {
    if (snapshot.sky != nullptr && snapshot.sky->in_use() > 0)
        return static_cast<uint32_t>(snapshot.sky->in_use());
    return snapshot.satellites;
}

void draw_counts(ui::Canvas& canvas, const SatellitesSnapshot& snapshot) {
    char text[24];
    int written = format_string(text, "USED ");
    written += format_unsigned(text + written, used_count(snapshot), 1);
    if (snapshot.levels_live && snapshot.sky != nullptr) {
        written += format_string(text + written, " OF ");
        written += format_unsigned(text + written, static_cast<uint32_t>(snapshot.sky->count()), 1);
    }
    text[written] = 0;
    right_aligned(canvas, kRight, kTitleY, text, written);
}

void draw_dops(ui::Canvas& canvas, const SatellitesSnapshot& snapshot) {
    char text[32];
    int written = format_string(text, "HDOP ");
    if (snapshot.hdop_e2 > 0)
        written += format_unsigned(text + written, snapshot.hdop_e2, 3, 2);
    else
        written += format_string(text + written, "---");
    written += format_string(text + written, "  VDOP ");
    if (snapshot.vdop_e2 > 0)
        written += format_unsigned(text + written, snapshot.vdop_e2, 3, 2);
    else
        written += format_string(text + written, "---");
    text[written] = 0;
    canvas.draw_text(kLeft, kDopY, text, true, 1);
}

void draw_solution_phase(ui::Canvas& canvas, const SatellitesSnapshot& snapshot) {
    if (!snapshot.solution_phase_valid) return;
    char text[12];
    int written = format_string(text, "NAV ");
    written += format_unsigned(text + written, snapshot.solution_phase_ms, 3);
    written += format_string(text + written, "MS");
    text[written] = 0;
    right_aligned(canvas, kRight, kDopY, text, written);
}

void draw_port(ui::Canvas& canvas, const SatellitesSnapshot& snapshot) {
    if (snapshot.health.baud == 0) return;
    char text[32];
    int written = format_string(text, "BAUD ");
    written += format_unsigned(text + written, snapshot.health.baud);
    written += format_string(text + written, "  ");
    written += format_string(text + written, ports::to_string(snapshot.health.config_stage));
    if (snapshot.health.overruns > 0) {
        written += format_string(text + written, "  OVR ");
        written += format_unsigned(text + written, snapshot.health.overruns);
    }
    text[written] = 0;
    canvas.draw_text(kLeft, kPortY, text, true, 1);
}

void draw_stage(ui::Canvas& canvas, const SatellitesSnapshot& snapshot) {
    if (snapshot.fix_valid) return;
    canvas.draw_text(kLeft, kDopY - 12, gnss::stage_name(snapshot.stage), true, 1);
}

void draw_used_by_system(ui::Canvas& canvas, const SatellitesSnapshot& snapshot) {
    if (snapshot.sky == nullptr) return;
    char text[32];
    int written = 0;
    for (gnss::System system : kSystemOrder) {
        const int used = snapshot.sky->in_use_of(system);
        if (used == 0) continue;
        if (written > 0) written += format_string(text + written, "  ");
        written += format_string(text + written, gnss::system_name(system));
        text[written++] = ' ';
        written += format_unsigned(text + written, static_cast<uint32_t>(used), 1);
    }
    if (written == 0) return;
    text[written] = 0;
    canvas.draw_text(kLeft, kBarTop, text, true, 1);
}

}  // namespace

void draw_satellites(ui::Canvas& canvas, const SatellitesSnapshot& snapshot) {
    canvas.draw_text(kLeft, kTitleY, "SATELLITES", true, 1);
    draw_counts(canvas, snapshot);
    canvas.horizontal_line(kLeft, kRuleY, kRight - kLeft, true);

    draw_dops(canvas, snapshot);
    draw_solution_phase(canvas, snapshot);
    draw_port(canvas, snapshot);
    draw_stage(canvas, snapshot);

    if (!snapshot.levels_live || snapshot.sky == nullptr || snapshot.sky->count() == 0) {
        draw_used_by_system(canvas, snapshot);
        canvas.draw_text(kLeft, kNoteY,
                         used_count(snapshot) == 0 ? "NO SATELLITE HEARD" : "LEVELS COMING UP",
                         true, 1);
        return;
    }

    scale_mark(canvas);
    int x = kLeft;
    for (gnss::System system : kSystemOrder) x = draw_group(canvas, *snapshot.sky, system, x);
    canvas.draw_text(kLeft, kNoteY, "FILLED: IN THE SOLUTION", true, 1);
}

}  // namespace skyblip::go
