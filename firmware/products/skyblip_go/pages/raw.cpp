#include "products/skyblip_go/pages/raw.h"

#include "core/util/format.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
constexpr int kCellW = 6;
constexpr int kRight = kGlassW - kLeft;

constexpr int kTitleY = 2;
constexpr int kTitleRuleY = 12;
constexpr int kGnssTopY = 16;
constexpr int kLineH = 11;
constexpr int kGnssRows = 6;
constexpr int kBlockRuleY = kGnssTopY + kGnssRows * kLineH + 1;
constexpr int kRadioTopY = kBlockRuleY + 5;

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int len) {
    canvas.draw_text(x_end - len * kCellW, y, text, true, 1);
}

void row(ui::Canvas& canvas, int y, char* buf, int n) {
    buf[n] = 0;
    canvas.draw_text(kLeft, y, buf, true, 1);
}

int format_word(char* out, const char* word) {
    int n = format_string(out, word);
    out[n++] = ' ';
    return n;
}

int format_keyed(char* out, const char* key, uint32_t value) {
    int n = format_word(out, key);
    n += format_uint(out + n, value);
    out[n++] = ' ';
    return n;
}

int format_dop(char* out, const char* key, uint16_t dop_e2) {
    int n = format_word(out, key);
    n += dop_e2 == 0 ? format_string(out + n, "---") : format_uint(out + n, dop_e2, 3, 2);
    out[n++] = ' ';
    return n;
}

int format_micros(char* out, const char* key, int32_t us) {
    int n = format_word(out, key);
    n += format_int(out + n, us, 1, 0, /*plus_sign=*/false);
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

void draw_title(ui::Canvas& canvas, const RawSnapshot& s) {
    canvas.draw_text(kLeft, kTitleY, "RAW", true, 1);
    char buf[16];
    int n = format_string(buf, "T+");
    n += format_uint(buf + n, s.uptime_s % kUptimeClockWrapS);
    buf[n] = 0;
    right_aligned(canvas, kRight, kTitleY, buf, n);
    canvas.hline(kLeft, kTitleRuleY, kRight - kLeft, true);
}

void draw_port(ui::Canvas& canvas, int y, const RawGnss& g) {
    char buf[40];
    int n = format_keyed(buf, "PORT", g.health.baud);
    n += format_word(buf + n, ports::to_string(g.health.config));
    n += format_keyed(buf + n, "OVR", g.health.overruns);
    n += format_word(buf + n, "ID");
    n += format_string(buf + n, yes_no(g.health.identified));
    row(canvas, y, buf, n);
}

void draw_nav(ui::Canvas& canvas, int y, const RawGnss& g) {
    char buf[40];
    int n = format_word(buf, "NAV");
    if (g.nav_valid) {
        n += format_uint(buf + n, g.nav_ms, 3);
        n += format_string(buf + n, "MS ");
    } else {
        n += format_string(buf + n, "--- ");
    }
    n += format_word(buf + n, "WIRE");
    n += format_uint(buf + n, g.health.pps_latency_ms);
    n += format_string(buf + n, "MS ");
    n += format_keyed(buf + n, "SENT", g.health.sentences);
    row(canvas, y, buf, n);
}

void draw_fix(ui::Canvas& canvas, int y, const RawGnss& g) {
    char buf[40];
    int n = format_word(buf, "FIX");
    n += format_word(buf + n, g.fix_valid ? "OK" : gnss::stage_name(g.stage));
    n += format_keyed(buf + n, "SV", g.sats);
    n += format_keyed(buf + n, "USE", g.in_use);
    n += format_keyed(buf + n, "Q", g.fix_mode);
    n += format_word(buf + n, "SET");
    n += format_string(buf + n, yes_no(g.settled));
    row(canvas, y, buf, n);
}

void draw_quality(ui::Canvas& canvas, int y, const RawGnss& g) {
    char buf[40];
    int n = format_dop(buf, "HDOP", g.hdop_e2);
    n += format_dop(buf + n, "VDOP", g.vdop_e2);
    n += format_word(buf + n, "RES");
    if (g.resid_valid) {
        n += format_uint(buf + n, g.resid_m);
        n += format_string(buf + n, "M");
    } else {
        n += format_string(buf + n, "---");
    }
    row(canvas, y, buf, n);
}

void draw_refusals(ui::Canvas& canvas, int y, const RawGnss& g) {
    char buf[40];
    int n = format_word(buf, "REJ");
    n += format_word(buf + n, gnss::reject_name(g.health.reject));
    n += format_keyed(buf + n, "N", g.health.rejected);
    n += format_keyed(buf + n, "SOL", g.solutions);
    n += format_word(buf + n, "GSV");
    n += format_string(buf + n, yes_no(g.levels_live));
    row(canvas, y, buf, n);
}

void draw_clock(ui::Canvas& canvas, int y, const RawGnss& g) {
    char buf[40];
    int n = format_word(buf, "PPS");
    n += format_word(buf + n, pps_word(g.pps));
    n += format_uint(buf + n, g.pps_age_ms);
    n += format_string(buf + n, "MS ");
    n += format_word(buf + n, "UTC");
    n += g.utc_valid ? format_seconds_of_day(buf + n, g.utc_s) : format_string(buf + n, "--:--:--");
    row(canvas, y, buf, n);
}

void draw_dwell(ui::Canvas& canvas, int y, const RawRadio& r) {
    char buf[40];
    int n = format_word(buf, "DWELL");
    n += format_word(buf + n, slot_word(r.slot));
    n += format_uint(buf + n, r.freq_hz / 100000 % 10000, 4, 1);
    n += format_string(buf + n, " TX ");
    n += format_string(buf + n, yes_no(r.tx_allowed));
    row(canvas, y, buf, n);

    n = format_int(buf, r.noise_dbm, 1, 0, /*plus_sign=*/false);
    n += format_string(buf + n, "DBM");
    buf[n] = 0;
    right_aligned(canvas, kRight, y, buf, n);
}

void draw_rx(ui::Canvas& canvas, int y, const RawRadio& r) {
    char buf[40];
    int n = format_keyed(buf, "RX", r.rx_ok);
    n += format_keyed(buf + n, "CRC", r.rx_bad);
    n += format_keyed(buf + n, "SYNC", r.rx_unframed);
    n += format_keyed(buf + n, "KEY", r.rx_miskeyed);
    row(canvas, y, buf, n);
}

void draw_rx_verdicts(ui::Canvas& canvas, int y, const RawRadio& r) {
    char buf[40];
    int n = format_keyed(buf, "DEC", r.rx_noise);
    n += format_keyed(buf + n, "TYPE", r.rx_type);
    n += format_keyed(buf + n, "WAIT", r.rx_wait);
    n += format_keyed(buf + n, "CALL", r.rx_named);
    row(canvas, y, buf, n);
}

void draw_uplink(ui::Canvas& canvas, int y, const RawRadio& r) {
    char buf[40];
    int n = format_word(buf, "UPLINK");
    n += format_uint(buf + n, r.uplink_frames);
    buf[n++] = '/';
    n += format_uint(buf + n, r.uplink_bad);
    buf[n++] = '/';
    n += format_uint(buf + n, r.uplink_targets);
    buf[n++] = ' ';
    n += format_keyed(buf + n, "TRK", r.tracked);
    n += format_keyed(buf + n, "ALM", r.alarm);
    row(canvas, y, buf, n);
}

void draw_tx(ui::Canvas& canvas, int y, const RawRadio& r) {
    char buf[40];
    int n = format_keyed(buf, "TX", r.tx_ok);
    n += format_keyed(buf + n, "LOST", r.tx_lost);
    n += format_keyed(buf + n, "MISS", r.missed);
    n += format_keyed(buf + n, "HELD", r.refused);
    n += format_keyed(buf + n, "CALL", r.tx_named);
    row(canvas, y, buf, n);
}

void draw_burst(ui::Canvas& canvas, int y, const RawRadio& r) {
    char buf[40];
    int n = format_word(buf, "DUTY");
    n += format_uint(buf + n, r.duty_permille);
    n += format_string(buf + n, "/1000 ");
    n += format_word(buf + n, "KEYED");
    n += format_uint(buf + n, r.tx_keyed_us);
    n += format_string(buf + n, "US");
    row(canvas, y, buf, n);
}

void draw_error(ui::Canvas& canvas, int y, const RawRadio& r) {
    char buf[40];
    int n = format_micros(buf, "SPAN", r.tx_span_us);
    n += format_micros(buf + n, "SLOT", r.dwell_worst_us);
    row(canvas, y, buf, n);

    n = format_micros(buf, "PPS", r.pps_worst_us);
    n += format_keyed(buf + n, "HOLDOVER", r.holdover);
    row(canvas, y + kLineH, buf, n - 1);
}

}  // namespace

void draw_raw(ui::Canvas& canvas, const RawSnapshot& s) {
    canvas.clear(true);
    draw_title(canvas, s);

    draw_port(canvas, kGnssTopY, s.gnss);
    draw_nav(canvas, kGnssTopY + kLineH, s.gnss);
    draw_fix(canvas, kGnssTopY + 2 * kLineH, s.gnss);
    draw_quality(canvas, kGnssTopY + 3 * kLineH, s.gnss);
    draw_refusals(canvas, kGnssTopY + 4 * kLineH, s.gnss);
    draw_clock(canvas, kGnssTopY + 5 * kLineH, s.gnss);

    canvas.hline(kLeft, kBlockRuleY, kRight - kLeft, true);

    draw_dwell(canvas, kRadioTopY, s.radio);
    draw_rx(canvas, kRadioTopY + kLineH, s.radio);
    draw_rx_verdicts(canvas, kRadioTopY + 2 * kLineH, s.radio);
    draw_uplink(canvas, kRadioTopY + 3 * kLineH, s.radio);
    draw_tx(canvas, kRadioTopY + 4 * kLineH, s.radio);
    draw_burst(canvas, kRadioTopY + 5 * kLineH, s.radio);
    draw_error(canvas, kRadioTopY + 6 * kLineH, s.radio);
}

}  // namespace skyblip::go
