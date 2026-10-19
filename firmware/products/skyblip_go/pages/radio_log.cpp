#include "products/skyblip_go/pages/radio_log.h"

#include "core/util/format.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
using ui::kCellW;
constexpr int kColumn(int cell) { return kLeft + cell * kCellW; }

constexpr int kTitleY = 2;
constexpr int kGnssY = 13;
constexpr int kRuleY = 23;
constexpr int kFirstRowY = 27;
constexpr int kLineH = 10;

constexpr int kClockX = kColumn(0);
constexpr int kWayX = kColumn(10);
constexpr int kBandX = kColumn(13);
constexpr int kVerdictX = kColumn(16);
constexpr int kAddressX = kColumn(22);
constexpr int kRightEnd = kColumn(32);

constexpr int kPpsX = kColumn(9);

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int len) {
    canvas.draw_text(x_end - len * kCellW, y, text, true, 1);
}

bool own_burst(radio::Verdict verdict) {
    return verdict == radio::Verdict::Transmitted || verdict == radio::Verdict::Lost ||
           verdict == radio::Verdict::Held || verdict == radio::Verdict::Unarmed;
}

// What own-ship put on air, in the column a reception spends on its sender: the
// schedule the burst went out on, or the name that is on neither schedule.
const char* own_burst_word(const radio::Entry& entry) {
    if (entry.callsign) return "CALLSIGN";
    return entry.airborne ? "AIR" : "GROUND";
}

// INFO: fc 17sep26 a transmission that worked prints no verdict, as a reception does not
const char* verdict_of(const radio::Entry& entry) {
    switch (entry.verdict) {
        case radio::Verdict::Lost:
        case radio::Verdict::Unarmed: return "LOST";
        case radio::Verdict::Held: return "HELD";
        case radio::Verdict::BadCrc: return "CRC";
        case radio::Verdict::Unframed: return "SYNC";
        case radio::Verdict::Miskeyed: return "KEY";
        case radio::Verdict::Undecoded: return "DEC";
        case radio::Verdict::Unsupported: return "TYPE";
        case radio::Verdict::Unattempted: return "WAIT";
        case radio::Verdict::Named: return "NAMED";
        case radio::Verdict::Transmitted:
        case radio::Verdict::Received:
        default: return nullptr;
    }
}

int format_verdict(char* out, const radio::Entry& entry) {
    const char* word = verdict_of(entry);
    if (word == nullptr) return 0;
    int written = format_string(out, word);
    if (entry.verdict == radio::Verdict::Miskeyed)
        written += format_int(out + written, entry.key_offset_s);
    return written;
}

int format_stamp(char* out, const radio::Entry& entry) {
    if (!entry.utc_dated) {
        int written = format_string(out, "T+");
        return written + format_uint(out + written, entry.at_s % kUptimeClockWrapS);
    }
    int written = format_uint(out, entry.at_s / 60 % 60, 2);
    out[written++] = ':';
    written += format_uint(out + written, entry.at_s % 60, 2);
    if (!entry.phase_valid) return written;
    out[written++] = '.';
    return written + format_uint(out + written, entry.into_ms, 3);
}

int format_dwell(char* out, const radio::Entry& entry) {
    if (entry.band == model::Band::O) return format_string(out, "O");
    int n = format_string(out, "M");
    return n + format_uint(out + n, entry.channel);
}

void draw_title(ui::Canvas& canvas, const RadioLogSnapshot& snapshot) {
    canvas.draw_text(kLeft, kTitleY, "RADIO LOG", true, 1);

    char text[32];
    int written = format_string(text, "RX ");
    written += format_uint(text + written, snapshot.rx_ok);
    written += format_string(text + written, " TX ");
    written += format_uint(text + written, snapshot.tx_ok);
    written += format_string(text + written, " NOISE ");
    written += format_uint(text + written, snapshot.rx_noise);
    text[written] = 0;
    right_aligned(canvas, kRightEnd, kTitleY, text, written);
}

int format_pps(char* out, const GnssReception& gnss) {
    int written = format_string(out, "PPS ");
    switch (gnss.pps) {
        case PpsState::Lock: return written + format_string(out + written, "LOCK");
        case PpsState::Holdover:
            written += format_string(out + written, "HOLD ");
            return written + format_uint(out + written, gnss.pps_age_s);
        case PpsState::None:
        default: return written + format_string(out + written, "NONE");
    }
}

void draw_gnss(ui::Canvas& canvas, const RadioLogSnapshot& snapshot) {
    const GnssReception& gnss = snapshot.gnss;
    char text[32];
    int written = 0;
    if (!gnss.fix_valid) {
        written = format_string(text, "NO FIX");
    } else {
        written = format_string(text, gnss.sats >= kFewestSatellitesForAltitude ? "3D " : "2D ");
        written += format_uint(text + written, gnss.sats);
        written += format_string(text + written, "SV");
    }
    text[written] = 0;
    canvas.draw_text(kLeft, kGnssY, text, true, 1);

    written = format_pps(text, gnss);
    text[written] = 0;
    canvas.draw_text(kPpsX, kGnssY, text, true, 1);

    written = format_string(text, "BAND ");
    written += format_int(text + written, snapshot.noise_dbm, 1, 0, true);
    text[written] = 0;
    right_aligned(canvas, kRightEnd, kGnssY, text, written);
}

void draw_row(ui::Canvas& canvas, int y, const radio::Entry& entry) {
    char text[16];

    int written = format_stamp(text, entry);
    text[written] = 0;
    canvas.draw_text(kClockX, y, text, true, 1);

    const bool ours = own_burst(entry.verdict);
    canvas.draw_text(kWayX, y, ours ? "TX" : "RX", true, 1);

    written = format_dwell(text, entry);
    text[written] = 0;
    canvas.draw_text(kBandX, y, text, true, 1);

    written = format_verdict(text, entry);
    if (written > 0) {
        text[written] = 0;
        canvas.draw_text(kVerdictX, y, text, true, 1);
    } else if (!ours) {
        canvas.draw_text(kVerdictX, y, model::source_word(entry.source), true, 1);
    }

    if (ours) {
        canvas.draw_text(kAddressX, y, own_burst_word(entry), true, 1);
        return;
    }

    if (entry.address_valid) {
        written = format_hex(text, entry.address, 6);
        text[written] = 0;
        canvas.draw_text(kAddressX, y, text, true, 1);
    }

    if (!entry.rssi_valid) return;
    written = format_int(text, entry.rssi_dbm, 1, 0, true);
    text[written] = 0;
    right_aligned(canvas, kRightEnd, y, text, written);
}

}  // namespace

void draw_radio_log(ui::Canvas& canvas, const RadioLogSnapshot& snapshot) {
    draw_title(canvas, snapshot);
    draw_gnss(canvas, snapshot);
    canvas.hline(kLeft, kRuleY, kRightEnd - kLeft, true);

    if (snapshot.log == nullptr || snapshot.row_count == 0) {
        canvas.draw_text(kLeft, kFirstRowY + kLineH, "NOTHING ON AIR YET", true, 1);
        return;
    }

    const int rows = snapshot.row_count < kRadioLogRows ? snapshot.row_count : kRadioLogRows;
    for (int i = 0; i < rows; i++)
        draw_row(canvas, kFirstRowY + i * kLineH, snapshot.log->newest(i));
}

}  // namespace skyblip::go
