#include "products/skyblip_go/pages/radio_log.h"

#include "core/util/format.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
constexpr int kCellW = 6;  // the 5x7 font's advance at scale 1
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

constexpr uint8_t kFewestSatsForAltitude = 4;

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int len) {
    canvas.draw_text(x_end - len * kCellW, y, text, true, 1);
}

bool own_burst(radio::Event event) {
    return event == radio::Event::Transmitted || event == radio::Event::Lost ||
           event == radio::Event::Held || event == radio::Event::Unarmed;
}

// What own-ship put on air, in the column a reception spends on its sender: the
// schedule the burst went out on, or the name that is on neither schedule.
const char* own_burst_word(const radio::Entry& entry) {
    if (entry.callsign) return "CALLSIGN";
    return entry.airborne ? "AIR" : "GROUND";
}

// INFO: fc 17sep26 a transmission that worked prints no verdict, as a reception does not
const char* verdict_of(const radio::Entry& entry) {
    switch (entry.event) {
        case radio::Event::Lost:
        case radio::Event::Unarmed: return "LOST";
        case radio::Event::Held: return "HELD";
        case radio::Event::BadCrc: return "CRC";
        case radio::Event::Unframed: return "SYNC";
        case radio::Event::Miskeyed: return "KEY";
        case radio::Event::Undecoded: return "DEC";
        case radio::Event::Unsupported: return "TYPE";
        case radio::Event::Unattempted: return "WAIT";
        case radio::Event::Named: return "NAMED";
        case radio::Event::Transmitted:
        case radio::Event::Received:
        default: return nullptr;
    }
}

int format_verdict(char* out, const radio::Entry& entry) {
    const char* word = verdict_of(entry);
    if (word == nullptr) return 0;
    int n = format_string(out, word);
    if (entry.event == radio::Event::Miskeyed) n += format_int(out + n, entry.key_offset_s);
    return n;
}

int format_stamp(char* out, const radio::Entry& entry) {
    if (!entry.utc_dated) {
        int n = format_string(out, "T+");
        return n + format_uint(out + n, entry.at_s % kUptimeClockWrapS);
    }
    int n = format_uint(out, entry.at_s / 60 % 60, 2);
    out[n++] = ':';
    n += format_uint(out + n, entry.at_s % 60, 2);
    if (!entry.phase_valid) return n;
    out[n++] = '.';
    return n + format_uint(out + n, entry.into_ms, 3);
}

int format_dwell(char* out, const radio::Entry& entry) {
    if (entry.band == model::Band::O) return format_string(out, "O");
    int n = format_string(out, "M");
    return n + format_uint(out + n, entry.channel);
}

void draw_title(ui::Canvas& canvas, const RadioLogSnapshot& snapshot) {
    canvas.draw_text(kLeft, kTitleY, "RADIO LOG", true, 1);

    char buf[32];
    int n = format_string(buf, "RX ");
    n += format_uint(buf + n, snapshot.rx_ok);
    n += format_string(buf + n, " TX ");
    n += format_uint(buf + n, snapshot.tx_ok);
    n += format_string(buf + n, " NOISE ");
    n += format_uint(buf + n, snapshot.noise);
    buf[n] = 0;
    right_aligned(canvas, kRightEnd, kTitleY, buf, n);
}

int format_pps(char* out, const GnssReception& gnss) {
    int n = format_string(out, "PPS ");
    switch (gnss.pps) {
        case PpsState::Lock: return n + format_string(out + n, "LOCK");
        case PpsState::Holdover:
            n += format_string(out + n, "HOLD ");
            return n + format_uint(out + n, gnss.pps_age_s);
        case PpsState::None:
        default: return n + format_string(out + n, "NONE");
    }
}

void draw_gnss(ui::Canvas& canvas, const RadioLogSnapshot& snapshot) {
    const GnssReception& gnss = snapshot.gnss;
    char buf[32];
    int n = 0;
    if (!gnss.fix_valid) {
        n = format_string(buf, "NO FIX");
    } else {
        n = format_string(buf, gnss.sats >= kFewestSatsForAltitude ? "3D " : "2D ");
        n += format_uint(buf + n, gnss.sats);
        n += format_string(buf + n, "SV");
    }
    buf[n] = 0;
    canvas.draw_text(kLeft, kGnssY, buf, true, 1);

    n = format_pps(buf, gnss);
    buf[n] = 0;
    canvas.draw_text(kPpsX, kGnssY, buf, true, 1);

    n = format_string(buf, "BAND ");
    n += format_int(buf + n, snapshot.band_dbm, 1, 0, false);
    buf[n] = 0;
    right_aligned(canvas, kRightEnd, kGnssY, buf, n);
}

void draw_row(ui::Canvas& canvas, int y, const radio::Entry& entry) {
    char buf[16];

    int n = format_stamp(buf, entry);
    buf[n] = 0;
    canvas.draw_text(kClockX, y, buf, true, 1);

    const bool ours = own_burst(entry.event);
    canvas.draw_text(kWayX, y, ours ? "TX" : "RX", true, 1);

    n = format_dwell(buf, entry);
    buf[n] = 0;
    canvas.draw_text(kBandX, y, buf, true, 1);

    n = format_verdict(buf, entry);
    if (n > 0) {
        buf[n] = 0;
        canvas.draw_text(kVerdictX, y, buf, true, 1);
    } else if (!ours) {
        canvas.draw_text(kVerdictX, y, model::source_word(entry.source), true, 1);
    }

    if (ours) {
        canvas.draw_text(kAddressX, y, own_burst_word(entry), true, 1);
        return;
    }

    if (entry.address_valid) {
        n = format_hex(buf, entry.address, 6);
        buf[n] = 0;
        canvas.draw_text(kAddressX, y, buf, true, 1);
    }

    if (!entry.rssi_valid) return;
    n = format_int(buf, entry.rssi_dbm, 1, 0, false);
    buf[n] = 0;
    right_aligned(canvas, kRightEnd, y, buf, n);
}

}  // namespace

void draw_radio_log(ui::Canvas& canvas, const RadioLogSnapshot& snapshot) {
    draw_title(canvas, snapshot);
    draw_gnss(canvas, snapshot);
    canvas.hline(kLeft, kRuleY, kRightEnd - kLeft, true);

    if (snapshot.log == nullptr || snapshot.n_rows == 0) {
        canvas.draw_text(kLeft, kFirstRowY + kLineH, "NOTHING ON AIR YET", true, 1);
        return;
    }

    const int rows = snapshot.n_rows < kRadioLogRows ? snapshot.n_rows : kRadioLogRows;
    for (int i = 0; i < rows; i++)
        draw_row(canvas, kFirstRowY + i * kLineH, snapshot.log->newest(i));
}

}  // namespace skyblip::go
