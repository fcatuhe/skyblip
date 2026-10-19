#include "products/skyblip_go/pages/raw.h"

#include "core/util/format.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
using ui::kCellW;
constexpr int kRight = kGlassWidth - kLeft;

constexpr int kTitleY = 2;
constexpr int kTitleRuleY = 12;
constexpr int kGnssTopY = 16;
constexpr int kLineH = 11;
constexpr int kGnssRows = 6;
constexpr int kBlockRuleY = kGnssTopY + kGnssRows * kLineH + 1;
constexpr int kRadioTopY = kBlockRuleY + 5;

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int length) {
    canvas.draw_text(x_end - length * kCellW, y, text, true, 1);
}

void row(ui::Canvas& canvas, int y, char* buffer, int n) {
    buffer[n] = 0;
    canvas.draw_text(kLeft, y, buffer, true, 1);
}

int format_word(char* out, const char* word) {
    int n = format_string(out, word);
    out[n++] = ' ';
    return n;
}

int format_keyed(char* out, const char* key, uint32_t value) {
    int n = format_word(out, key);
    n += format_unsigned(out + n, value);
    out[n++] = ' ';
    return n;
}

int format_dop(char* out, const char* key, uint16_t dop_e2) {
    int n = format_word(out, key);
    n += dop_e2 == 0 ? format_string(out + n, "---") : format_unsigned(out + n, dop_e2, 3, 2);
    out[n++] = ' ';
    return n;
}

int format_micros(char* out, const char* key, int32_t us) {
    int n = format_word(out, key);
    n += format_signed(out + n, us, 1, 0, /*plus_sign=*/false);
    n += format_string(out + n, "US ");
    return n;
}

const char* yes_no(bool on) { return on ? "Y" : "N"; }

const char* pps_word(PpsState pps) {
    switch (pps) {
        case PpsState::Lock: return "LOCK";
        case PpsState::Holdover: return "HOLD";
        case PpsState::None:
        default: return "NONE";
    }
}

const char* slot_word(timing::SlotState slot) {
    switch (slot) {
        case timing::SlotState::UplinkRxO: return "O-UP";
        case timing::SlotState::SwitchOtoM: return "O>M";
        case timing::SlotState::Slot0: return "M0";
        case timing::SlotState::Hop: return "HOP";
        case timing::SlotState::Slot1: return "M1";
        case timing::SlotState::SwitchMtoO: return "M>O";
    }
    return "?";
}

void draw_title(ui::Canvas& canvas, const RawSnapshot& snapshot) {
    canvas.draw_text(kLeft, kTitleY, "RAW", true, 1);
    char text[16];
    int written = format_string(text, "T+");
    written += format_unsigned(text + written, snapshot.uptime_s % kUptimeClockWrapS);
    text[written] = 0;
    right_aligned(canvas, kRight, kTitleY, text, written);
    canvas.hline(kLeft, kTitleRuleY, kRight - kLeft, true);
}

void draw_port(ui::Canvas& canvas, int y, const RawGnss& g) {
    char text[40];
    int written = format_keyed(text, "PORT", g.health.baud);
    written += format_word(text + written, ports::to_string(g.health.config_stage));
    written += format_keyed(text + written, "OVR", g.health.overruns);
    written += format_word(text + written, "ID");
    written += format_string(text + written, yes_no(g.health.identified));
    row(canvas, y, text, written);
}

void draw_nav(ui::Canvas& canvas, int y, const RawGnss& g) {
    char text[40];
    int written = format_word(text, "NAV");
    if (g.nav_valid) {
        written += format_unsigned(text + written, g.nav_ms, 3);
        written += format_string(text + written, "MS ");
    } else {
        written += format_string(text + written, "--- ");
    }
    written += format_word(text + written, "WIRE");
    written += format_unsigned(text + written, g.health.pps_latency_ms);
    written += format_string(text + written, "MS ");
    written += format_keyed(text + written, "SENT", g.health.sentences);
    row(canvas, y, text, written);
}

void draw_fix(ui::Canvas& canvas, int y, const RawGnss& g) {
    char text[40];
    int written = format_word(text, "FIX");
    written += format_word(text + written, g.fix_valid ? "OK" : gnss::stage_name(g.stage));
    written += format_keyed(text + written, "SV", g.satellites);
    written += format_keyed(text + written, "USE", g.in_use);
    written += format_keyed(text + written, "Q", g.fix_mode);
    written += format_word(text + written, "SET");
    written += format_string(text + written, yes_no(g.settled));
    row(canvas, y, text, written);
}

void draw_quality(ui::Canvas& canvas, int y, const RawGnss& g) {
    char text[40];
    int written = format_dop(text, "HDOP", g.hdop_e2);
    written += format_dop(text + written, "VDOP", g.vdop_e2);
    written += format_word(text + written, "RES");
    if (g.resid_valid) {
        written += format_unsigned(text + written, g.resid_m);
        written += format_string(text + written, "M");
    } else {
        written += format_string(text + written, "---");
    }
    row(canvas, y, text, written);
}

void draw_refusals(ui::Canvas& canvas, int y, const RawGnss& g) {
    char text[40];
    int written = format_word(text, "REJ");
    written += format_word(text + written, gnss::reject_name(g.health.last_reject));
    written += format_keyed(text + written, "N", g.health.rejected);
    written += format_keyed(text + written, "SOL", g.solutions);
    written += format_word(text + written, "GSV");
    written += format_string(text + written, yes_no(g.levels_live));
    row(canvas, y, text, written);
}

void draw_clock(ui::Canvas& canvas, int y, const RawGnss& g) {
    char text[40];
    int written = format_word(text, "PPS");
    written += format_word(text + written, pps_word(g.pps));
    written += format_unsigned(text + written, g.pps_age_ms);
    written += format_string(text + written, "MS ");
    written += format_word(text + written, "UTC");
    written += g.utc_valid ? format_seconds_of_day(text + written, g.utc_s)
                           : format_string(text + written, "--:--:--");
    row(canvas, y, text, written);
}

void draw_dwell(ui::Canvas& canvas, int y, const RawRadio& r) {
    char text[40];
    int written = format_word(text, "DWELL");
    written += format_word(text + written, slot_word(r.slot));
    written += format_unsigned(text + written, r.frequency_hz / 100000 % 10000, 4, 1);
    written += format_string(text + written, " TX ");
    written += format_string(text + written, yes_no(r.tx_allowed));
    row(canvas, y, text, written);

    written = format_signed(text, r.noise_dbm, 1, 0, /*plus_sign=*/false);
    written += format_string(text + written, "DBM");
    text[written] = 0;
    right_aligned(canvas, kRight, y, text, written);
}

void draw_rx(ui::Canvas& canvas, int y, const RawRadio& r) {
    char text[40];
    int written = format_keyed(text, "RX", r.rx_ok);
    written += format_keyed(text + written, "CRC", r.rx_bad);
    written += format_keyed(text + written, "SYNC", r.rx_unframed);
    written += format_keyed(text + written, "KEY", r.rx_miskeyed);
    row(canvas, y, text, written);
}

void draw_rx_verdicts(ui::Canvas& canvas, int y, const RawRadio& r) {
    char text[40];
    int written = format_keyed(text, "DEC", r.rx_noise);
    written += format_keyed(text + written, "TYPE", r.rx_type);
    written += format_keyed(text + written, "WAIT", r.rx_wait);
    written += format_keyed(text + written, "CALL", r.rx_named);
    row(canvas, y, text, written);
}

void draw_uplink(ui::Canvas& canvas, int y, const RawRadio& r) {
    char text[40];
    int written = format_word(text, "UPLINK");
    written += format_unsigned(text + written, r.uplink_frames);
    text[written++] = '/';
    written += format_unsigned(text + written, r.uplink_bad);
    text[written++] = '/';
    written += format_unsigned(text + written, r.uplink_targets);
    text[written++] = ' ';
    written += format_keyed(text + written, "TRK", r.tracked);
    written += format_keyed(text + written, "ALM", r.alarm);
    row(canvas, y, text, written);
}

void draw_tx(ui::Canvas& canvas, int y, const RawRadio& r) {
    char text[40];
    int written = format_keyed(text, "TX", r.tx_ok);
    written += format_keyed(text + written, "LOST", r.tx_lost);
    written += format_keyed(text + written, "MISS", r.missed);
    written += format_keyed(text + written, "HELD", r.refused);
    written += format_keyed(text + written, "CALL", r.tx_named);
    row(canvas, y, text, written);
}

void draw_burst(ui::Canvas& canvas, int y, const RawRadio& r) {
    char text[40];
    int written = format_word(text, "DUTY");
    written += format_unsigned(text + written, r.duty_permille);
    written += format_string(text + written, "/1000 ");
    written += format_word(text + written, "KEYED");
    written += format_unsigned(text + written, r.tx_keyed_us);
    written += format_string(text + written, "US");
    row(canvas, y, text, written);
}

void draw_error(ui::Canvas& canvas, int y, const RawRadio& r) {
    char text[40];
    int written = format_micros(text, "SPAN", r.tx_span_us);
    written += format_micros(text + written, "SLOT", r.dwell_worst_us);
    row(canvas, y, text, written);

    written = format_micros(text, "PPS", r.pps_worst_us);
    written += format_keyed(text + written, "HOLDOVER", r.holdover);
    row(canvas, y + kLineH, text, written - 1);
}

}  // namespace

void draw_raw(ui::Canvas& canvas, const RawSnapshot& snapshot) {
    canvas.clear(true);
    draw_title(canvas, snapshot);

    draw_port(canvas, kGnssTopY, snapshot.gnss);
    draw_nav(canvas, kGnssTopY + kLineH, snapshot.gnss);
    draw_fix(canvas, kGnssTopY + 2 * kLineH, snapshot.gnss);
    draw_quality(canvas, kGnssTopY + 3 * kLineH, snapshot.gnss);
    draw_refusals(canvas, kGnssTopY + 4 * kLineH, snapshot.gnss);
    draw_clock(canvas, kGnssTopY + 5 * kLineH, snapshot.gnss);

    canvas.hline(kLeft, kBlockRuleY, kRight - kLeft, true);

    draw_dwell(canvas, kRadioTopY, snapshot.radio);
    draw_rx(canvas, kRadioTopY + kLineH, snapshot.radio);
    draw_rx_verdicts(canvas, kRadioTopY + 2 * kLineH, snapshot.radio);
    draw_uplink(canvas, kRadioTopY + 3 * kLineH, snapshot.radio);
    draw_tx(canvas, kRadioTopY + 4 * kLineH, snapshot.radio);
    draw_burst(canvas, kRadioTopY + 5 * kLineH, snapshot.radio);
    draw_error(canvas, kRadioTopY + 6 * kLineH, snapshot.radio);
}

}  // namespace skyblip::go
