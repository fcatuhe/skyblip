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

void right_aligned(ui::Canvas& fb, int x_end, int y, const char* text, int len) {
    fb.draw_text(x_end - len * kCellW, y, text, true, 1);
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

int fmt_verdict(char* out, const radio::Entry& entry) {
    const char* word = verdict_of(entry);
    if (word == nullptr) return 0;
    int n = fmt_string(out, word);
    if (entry.event == radio::Event::Miskeyed) n += fmt_int(out + n, entry.key_offset_s);
    return n;
}

int fmt_stamp(char* out, const radio::Entry& entry) {
    if (!entry.utc) {
        int n = fmt_string(out, "T+");
        return n + fmt_uint(out + n, entry.at_s % kUptimeClockWrapS);
    }
    int n = fmt_uint(out, entry.at_s / 60 % 60, 2);
    out[n++] = ':';
    n += fmt_uint(out + n, entry.at_s % 60, 2);
    if (!entry.phase_valid) return n;
    out[n++] = '.';
    return n + fmt_uint(out + n, entry.into_ms, 3);
}

int fmt_dwell(char* out, const radio::Entry& entry) {
    if (entry.band == model::Band::O) return fmt_string(out, "O");
    int n = fmt_string(out, "M");
    return n + fmt_uint(out + n, entry.channel);
}

void draw_title(ui::Canvas& fb, const RadioLogSnapshot& snap) {
    fb.draw_text(kLeft, kTitleY, "RADIO LOG", true, 1);

    char buf[32];
    int n = fmt_string(buf, "RX ");
    n += fmt_uint(buf + n, snap.rx_ok);
    n += fmt_string(buf + n, " TX ");
    n += fmt_uint(buf + n, snap.tx_ok);
    n += fmt_string(buf + n, " NOISE ");
    n += fmt_uint(buf + n, snap.noise);
    buf[n] = 0;
    right_aligned(fb, kRightEnd, kTitleY, buf, n);
}

int fmt_pps(char* out, const GnssReception& gnss) {
    int n = fmt_string(out, "PPS ");
    switch (gnss.pps) {
        case PpsState::Lock: return n + fmt_string(out + n, "LOCK");
        case PpsState::Holdover:
            n += fmt_string(out + n, "HOLD ");
            return n + fmt_uint(out + n, gnss.pps_age_s);
        case PpsState::None:
        default: return n + fmt_string(out + n, "NONE");
    }
}

void draw_gnss(ui::Canvas& fb, const RadioLogSnapshot& snap) {
    const GnssReception& gnss = snap.gnss;
    char buf[32];
    int n = 0;
    if (!gnss.fix_valid) {
        n = fmt_string(buf, "NO FIX");
    } else {
        n = fmt_string(buf, gnss.sats >= kFewestSatsForAltitude ? "3D " : "2D ");
        n += fmt_uint(buf + n, gnss.sats);
        n += fmt_string(buf + n, "SV");
    }
    buf[n] = 0;
    fb.draw_text(kLeft, kGnssY, buf, true, 1);

    n = fmt_pps(buf, gnss);
    buf[n] = 0;
    fb.draw_text(kPpsX, kGnssY, buf, true, 1);

    n = fmt_string(buf, "BAND ");
    n += fmt_int(buf + n, snap.band_dbm, 1, 0, false);
    buf[n] = 0;
    right_aligned(fb, kRightEnd, kGnssY, buf, n);
}

void draw_row(ui::Canvas& fb, int y, const radio::Entry& entry) {
    char buf[16];

    int n = fmt_stamp(buf, entry);
    buf[n] = 0;
    fb.draw_text(kClockX, y, buf, true, 1);

    const bool ours = own_burst(entry.event);
    fb.draw_text(kWayX, y, ours ? "TX" : "RX", true, 1);

    n = fmt_dwell(buf, entry);
    buf[n] = 0;
    fb.draw_text(kBandX, y, buf, true, 1);

    n = fmt_verdict(buf, entry);
    if (n > 0) {
        buf[n] = 0;
        fb.draw_text(kVerdictX, y, buf, true, 1);
    } else if (!ours) {
        fb.draw_text(kVerdictX, y, model::source_word(entry.source), true, 1);
    }

    if (ours) {
        fb.draw_text(kAddressX, y, own_burst_word(entry), true, 1);
        return;
    }

    if (entry.address_valid) {
        n = fmt_hex(buf, entry.address, 6);
        buf[n] = 0;
        fb.draw_text(kAddressX, y, buf, true, 1);
    }

    if (!entry.rssi_valid) return;
    n = fmt_int(buf, entry.rssi_dbm, 1, 0, false);
    buf[n] = 0;
    right_aligned(fb, kRightEnd, y, buf, n);
}

}  // namespace

void draw_radio_log(ui::Canvas& fb, const RadioLogSnapshot& snap) {
    draw_title(fb, snap);
    draw_gnss(fb, snap);
    fb.hline(kLeft, kRuleY, kRightEnd - kLeft, true);

    if (snap.log == nullptr || snap.n_rows == 0) {
        fb.draw_text(kLeft, kFirstRowY + kLineH, "NOTHING ON AIR YET", true, 1);
        return;
    }

    const int rows = snap.n_rows < kRadioLogRows ? snap.n_rows : kRadioLogRows;
    for (int i = 0; i < rows; i++) draw_row(fb, kFirstRowY + i * kLineH, snap.log->newest(i));
}

}  // namespace skyblip::go
