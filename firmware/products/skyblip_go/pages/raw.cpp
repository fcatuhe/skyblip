#include "products/skyblip_go/pages/raw.h"

#include "core/util/format.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
using ui::kCellWidth;
constexpr int kRight = kGlassWidth - kLeft;

constexpr int kTitleY = 2;
constexpr int kTitleRuleY = 12;
constexpr int kGnssTopY = 16;
constexpr int kLineHeight = 11;
constexpr int kGnssRows = 6;
constexpr int kBlockRuleY = kGnssTopY + kGnssRows * kLineHeight + 1;
constexpr int kRadioTopY = kBlockRuleY + 5;

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int length) {
    canvas.draw_text(x_end - length * kCellWidth, y, text, true, 1);
}

void row(ui::Canvas& canvas, int y, char* text, int written) {
    text[written] = 0;
    canvas.draw_text(kLeft, y, text, true, 1);
}

int format_word(char* out, const char* word) {
    int written = format_string(out, word);
    out[written++] = ' ';
    return written;
}

int format_keyed(char* out, const char* key, uint32_t value) {
    int written = format_word(out, key);
    written += format_unsigned(out + written, value);
    out[written++] = ' ';
    return written;
}

int format_dop(char* out, const char* key, uint16_t dop_e2) {
    int written = format_word(out, key);
    written += dop_e2 == 0 ? format_string(out + written, "---")
                           : format_unsigned(out + written, dop_e2, 3, 2);
    out[written++] = ' ';
    return written;
}

int format_microseconds(char* out, const char* key, int32_t value_us) {
    int written = format_word(out, key);
    written += format_signed(out + written, value_us, 1, 0, /*plus_sign=*/false);
    written += format_string(out + written, "US ");
    return written;
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
        case timing::SlotState::UplinkRxOBand: return "O-UP";
        case timing::SlotState::SwitchOBandToMBand: return "O>M";
        case timing::SlotState::Slot0: return "M0";
        case timing::SlotState::Hop: return "HOP";
        case timing::SlotState::Slot1: return "M1";
        case timing::SlotState::SwitchMBandToOBand: return "M>O";
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
    canvas.horizontal_line(kLeft, kTitleRuleY, kRight - kLeft, true);
}

void draw_port(ui::Canvas& canvas, int y, const RawGnss& receiver) {
    char text[40];
    int written = format_keyed(text, "PORT", receiver.health.baud);
    written += format_word(text + written, ports::to_string(receiver.health.config_stage));
    written += format_keyed(text + written, "OVR", receiver.health.overruns);
    written += format_word(text + written, "ID");
    written += format_string(text + written, yes_no(receiver.health.identified));
    row(canvas, y, text, written);
}

void draw_solution_phase(ui::Canvas& canvas, int y, const RawGnss& receiver) {
    char text[40];
    int written = format_word(text, "NAV");
    if (receiver.solution_phase_valid) {
        written += format_unsigned(text + written, receiver.solution_phase_ms, 3);
        written += format_string(text + written, "MS ");
    } else {
        written += format_string(text + written, "--- ");
    }
    written += format_word(text + written, "WIRE");
    written += format_unsigned(text + written, receiver.health.pps_latency_ms);
    written += format_string(text + written, "MS ");
    written += format_keyed(text + written, "SENT", receiver.health.sentences);
    row(canvas, y, text, written);
}

void draw_fix(ui::Canvas& canvas, int y, const RawGnss& receiver) {
    char text[40];
    int written = format_word(text, "FIX");
    written +=
        format_word(text + written, receiver.fix_valid ? "OK" : gnss::stage_name(receiver.stage));
    written += format_keyed(text + written, "SV", receiver.satellites);
    written += format_keyed(text + written, "USE", receiver.in_use);
    written += format_keyed(text + written, "Q", receiver.fix_mode);
    written += format_word(text + written, "SET");
    written += format_string(text + written, yes_no(receiver.settled));
    row(canvas, y, text, written);
}

void draw_quality(ui::Canvas& canvas, int y, const RawGnss& receiver) {
    char text[40];
    int written = format_dop(text, "HDOP", receiver.hdop_e2);
    written += format_dop(text + written, "VDOP", receiver.vdop_e2);
    written += format_word(text + written, "RES");
    if (receiver.residual_valid) {
        written += format_unsigned(text + written, receiver.residual_m);
        written += format_string(text + written, "M");
    } else {
        written += format_string(text + written, "---");
    }
    row(canvas, y, text, written);
}

void draw_refusals(ui::Canvas& canvas, int y, const RawGnss& receiver) {
    char text[40];
    int written = format_word(text, "REJ");
    written += format_word(text + written, gnss::reject_name(receiver.health.last_reject));
    written += format_keyed(text + written, "N", receiver.health.rejected);
    written += format_keyed(text + written, "SOL", receiver.solutions);
    written += format_word(text + written, "GSV");
    written += format_string(text + written, yes_no(receiver.levels_live));
    row(canvas, y, text, written);
}

void draw_clock(ui::Canvas& canvas, int y, const RawGnss& receiver) {
    char text[40];
    int written = format_word(text, "PPS");
    written += format_word(text + written, pps_word(receiver.pps));
    written += format_unsigned(text + written, receiver.pps_age_ms);
    written += format_string(text + written, "MS ");
    written += format_word(text + written, "UTC");
    written += receiver.utc_valid ? format_seconds_of_day(text + written, receiver.utc_s)
                                  : format_string(text + written, "--:--:--");
    row(canvas, y, text, written);
}

void draw_dwell(ui::Canvas& canvas, int y, const RawRadio& radio) {
    char text[40];
    int written = format_word(text, "DWELL");
    written += format_word(text + written, slot_word(radio.slot));
    written += format_unsigned(text + written, radio.frequency_hz / 100000 % 10000, 4, 1);
    written += format_string(text + written, " TX ");
    written += format_string(text + written, yes_no(radio.tx_allowed));
    row(canvas, y, text, written);

    written = format_signed(text, radio.noise_dbm, 1, 0, /*plus_sign=*/false);
    written += format_string(text + written, "DBM");
    text[written] = 0;
    right_aligned(canvas, kRight, y, text, written);
}

void draw_rx(ui::Canvas& canvas, int y, const RawRadio& radio) {
    char text[40];
    int written = format_keyed(text, "RX", radio.rx_ok);
    written += format_keyed(text + written, "CRC", radio.rx_bad);
    written += format_keyed(text + written, "SYNC", radio.rx_unframed);
    written += format_keyed(text + written, "KEY", radio.rx_miskeyed);
    row(canvas, y, text, written);
}

void draw_rx_verdicts(ui::Canvas& canvas, int y, const RawRadio& radio) {
    char text[40];
    int written = format_keyed(text, "DEC", radio.rx_noise);
    written += format_keyed(text + written, "TYPE", radio.rx_type);
    written += format_keyed(text + written, "WAIT", radio.rx_wait);
    written += format_keyed(text + written, "CALL", radio.rx_named);
    row(canvas, y, text, written);
}

void draw_uplink(ui::Canvas& canvas, int y, const RawRadio& radio) {
    char text[40];
    int written = format_word(text, "UPLINK");
    written += format_unsigned(text + written, radio.uplink_frames);
    text[written++] = '/';
    written += format_unsigned(text + written, radio.uplink_bad);
    text[written++] = '/';
    written += format_unsigned(text + written, radio.uplink_targets);
    text[written++] = ' ';
    written += format_keyed(text + written, "TRK", radio.tracked);
    written += format_keyed(text + written, "ALM", radio.alarm);
    row(canvas, y, text, written);
}

void draw_tx(ui::Canvas& canvas, int y, const RawRadio& radio) {
    char text[40];
    int written = format_keyed(text, "TX", radio.tx_ok);
    written += format_keyed(text + written, "LOST", radio.tx_lost);
    written += format_keyed(text + written, "MISS", radio.missed);
    written += format_keyed(text + written, "HELD", radio.refused);
    written += format_keyed(text + written, "CALL", radio.tx_named);
    row(canvas, y, text, written);
}

void draw_burst(ui::Canvas& canvas, int y, const RawRadio& radio) {
    char text[40];
    int written = format_word(text, "DUTY");
    written += format_unsigned(text + written, radio.duty_permille);
    written += format_string(text + written, "/1000 ");
    written += format_word(text + written, "KEYED");
    written += format_unsigned(text + written, radio.tx_keyed_us);
    written += format_string(text + written, "US");
    row(canvas, y, text, written);
}

void draw_error(ui::Canvas& canvas, int y, const RawRadio& radio) {
    char text[40];
    int written = format_microseconds(text, "SPAN", radio.tx_span_us);
    written += format_microseconds(text + written, "SLOT", radio.dwell_worst_us);
    row(canvas, y, text, written);

    written = format_microseconds(text, "PPS", radio.pps_worst_us);
    written += format_keyed(text + written, "HOLDOVER", radio.holdover);
    row(canvas, y + kLineHeight, text, written - 1);
}

}  // namespace

void draw_raw(ui::Canvas& canvas, const RawSnapshot& snapshot) {
    canvas.clear(true);
    draw_title(canvas, snapshot);

    draw_port(canvas, kGnssTopY, snapshot.gnss);
    draw_solution_phase(canvas, kGnssTopY + kLineHeight, snapshot.gnss);
    draw_fix(canvas, kGnssTopY + 2 * kLineHeight, snapshot.gnss);
    draw_quality(canvas, kGnssTopY + 3 * kLineHeight, snapshot.gnss);
    draw_refusals(canvas, kGnssTopY + 4 * kLineHeight, snapshot.gnss);
    draw_clock(canvas, kGnssTopY + 5 * kLineHeight, snapshot.gnss);

    canvas.horizontal_line(kLeft, kBlockRuleY, kRight - kLeft, true);

    draw_dwell(canvas, kRadioTopY, snapshot.radio);
    draw_rx(canvas, kRadioTopY + kLineHeight, snapshot.radio);
    draw_rx_verdicts(canvas, kRadioTopY + 2 * kLineHeight, snapshot.radio);
    draw_uplink(canvas, kRadioTopY + 3 * kLineHeight, snapshot.radio);
    draw_tx(canvas, kRadioTopY + 4 * kLineHeight, snapshot.radio);
    draw_burst(canvas, kRadioTopY + 5 * kLineHeight, snapshot.radio);
    draw_error(canvas, kRadioTopY + 6 * kLineHeight, snapshot.radio);
}

}  // namespace skyblip::go
