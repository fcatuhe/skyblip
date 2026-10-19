// The page a bench reads: what it draws per burst, and what it refuses to invent.
#include <cstring>

#include "core/model/aircraft.h"
#include "core/model/band.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/radio_log.h"
#include "test/support/glass_text.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

constexpr int kFirstRowY = 27;
constexpr int kLineH = 10;

int ink_in_row(const Glass& canvas, int index) {
    const int y0 = kFirstRowY + index * kLineH;
    int n = 0;
    for (int y = y0; y < y0 + 8; y++)
        for (int x = 0; x < Glass::kW; x++)
            if (canvas.get_pixel(x, y)) n++;
    return n;
}

radio::Entry entry_of(radio::Verdict verdict) {
    radio::Entry e{};
    e.verdict = verdict;
    e.band = model::Band::M;
    e.at_s = 45296;  // 12:34:56
    e.into_ms = 462;
    e.phase_valid = true;
    e.utc_dated = true;
    return e;
}

radio::Entry received(uint32_t address, int8_t rssi_dbm) {
    radio::Entry e = entry_of(radio::Verdict::Received);
    e.source = model::Source::AdslDirect;
    e.address = address;
    e.address_valid = true;
    e.rssi_dbm = rssi_dbm;
    e.rssi_valid = true;
    return e;
}

RadioLogSnapshot with(const radio::Log& log) {
    RadioLogSnapshot snapshot;
    snapshot.gnss.fix_valid = true;
    snapshot.gnss.sats = 9;
    snapshot.gnss.pps = PpsState::Lock;
    snapshot.rx_noise = 42;
    snapshot.noise_dbm = -109;
    snapshot.row_count = log.count();
    snapshot.log = &log;
    return snapshot;
}

}  // namespace

TEST_CASE("radio log page: a received frame shows when, from whom, and how loud") {
    radio::Log log;
    log.record(received(0x3FA21C, -87));

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4, kFirstRowY, "34:56.462"));
    CHECK(reads_at(canvas, 4 + 10 * 6, kFirstRowY, "RX"));
    CHECK(reads_at(canvas, 4 + 13 * 6, kFirstRowY, "M0"));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "ADS-L"));
    CHECK(reads_at(canvas, 4 + 22 * 6, kFirstRowY, "3FA21C"));
    CHECK(reads_at(canvas, 4 + 29 * 6, kFirstRowY, "-87"));
}

// The reading two devices are compared on: the same burst, sent at one phase and heard at another.
TEST_CASE("radio log page: the phase a burst landed at is printed to the millisecond") {
    radio::Log log;
    radio::Entry e = received(0x3FA21C, -87);
    e.into_ms = 7;
    log.record(e);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4, kFirstRowY, "34:56.007"));
}

// A phase against an edge nothing latched would be a number the reader could act on wrongly.
TEST_CASE("radio log page: a burst with no phase measured shows the second alone") {
    radio::Log log;
    radio::Entry e = received(0x3FA21C, -87);
    e.phase_valid = false;
    log.record(e);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4, kFirstRowY, "34:56"));
    CHECK_FALSE(reads_at(canvas, 4, kFirstRowY, "34:56."));
}

// §C.2.5 alternates the two M-band channels: a receiver on one hears half its neighbours.
TEST_CASE("radio log page: the M band's two channels read apart, and the O band has none") {
    radio::Log log;
    radio::Entry second = received(0x3FA21C, -87);
    second.channel = 1;
    log.record(second);
    radio::Entry uplink = received(0x3FA21C, -87);
    uplink.band = model::Band::O;
    log.record(uplink);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 13 * 6, kFirstRowY, "O"));
    CHECK_FALSE(reads_at(canvas, 4 + 13 * 6, kFirstRowY, "O0"));
    CHECK(reads_at(canvas, 4 + 13 * 6, kFirstRowY + kLineH, "M1"));
}

// Sixteen rows of SENT is sixteen rows a reader scans past to find the one that failed.
TEST_CASE("radio log page: a transmission that worked prints no verdict at all") {
    radio::Log log;
    radio::Entry e = entry_of(radio::Verdict::Transmitted);
    e.tx_keyed_us = 1523;
    e.tx_span_us = 6344;
    e.tx_span_valid = true;
    log.record(e);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 10 * 6, kFirstRowY, "TX"));
    CHECK_FALSE(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "SENT"));
    CHECK_FALSE(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "1523"));
    CHECK_FALSE(reads_at(canvas, 4 + 25 * 6, kFirstRowY, "6344"));
}

// §G.1.16: one burst a second in the air and one in ten on the ground, so rows either side of a
// takeoff read differently.
TEST_CASE("radio log page: a sent burst names the schedule it went out on") {
    radio::Log log;
    log.record(entry_of(radio::Verdict::Transmitted));
    radio::Entry flying = entry_of(radio::Verdict::Transmitted);
    flying.airborne = true;
    log.record(flying);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 22 * 6, kFirstRowY, "AIR"));
    CHECK(reads_at(canvas, 4 + 22 * 6, kFirstRowY + kLineH, "GROUND"));
}

// A name is on neither schedule, so it says which burst it was where the rate would be.
TEST_CASE("radio log page: own-ship's callsign burst says so where its schedule is printed") {
    radio::Log log;
    radio::Entry named = entry_of(radio::Verdict::Transmitted);
    named.airborne = true;
    named.callsign = true;
    log.record(named);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 10 * 6, kFirstRowY, "TX"));
    CHECK(reads_at(canvas, 4 + 22 * 6, kFirstRowY, "CALLSIGN"));
    CHECK_FALSE(reads_at(canvas, 4 + 22 * 6, kFirstRowY, "AIR"));
    CHECK_FALSE(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "CALLSIGN"));
}

// A name carries no position, so the row has to say what it was or it reads as a target.
TEST_CASE("radio log page: a received registration says it named its sender, and who") {
    radio::Log log;
    radio::Entry named = received(0x3FA21C, -87);
    named.verdict = radio::Verdict::Named;
    log.record(named);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "NAMED"));
    CHECK(reads_at(canvas, 4 + 22 * 6, kFirstRowY, "3FA21C"));
}

// The one row that separates an empty sky from a receiver that frames nothing.
TEST_CASE("radio log page: a burst that never framed says so instead of naming an aircraft") {
    radio::Log log;
    radio::Entry bad = entry_of(radio::Verdict::BadCrc);
    bad.rssi_dbm = -101;
    bad.rssi_valid = true;
    bad.address = 0x3FA21C;
    log.record(bad);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "CRC"));
    CHECK_FALSE(reads_at(canvas, 4 + 22 * 6, kFirstRowY, "3FA21C"));
}

// A transmitter the page shows nothing for reads as a dead one, whatever refused the burst.
TEST_CASE("radio log page: the hour's refusal reads apart from a burst that never left") {
    radio::Log log;
    log.record(entry_of(radio::Verdict::Held));
    log.record(entry_of(radio::Verdict::Unarmed));
    log.record(entry_of(radio::Verdict::Lost));

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "LOST"));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY + kLineH, "LOST"));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY + 2 * kLineH, "HELD"));
}

// Bits the air corrupted and bits nothing here knew what to do with are different faults.
TEST_CASE("radio log page: an integrity failure and an undecodable frame read apart") {
    radio::Log log;
    log.record(entry_of(radio::Verdict::Undecoded));

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "DEC"));
}

// Own-ship still acquiring and a dialect we do not read are not the fault DEC stands for.
TEST_CASE("radio log page: the three things that are not a decode read apart") {
    radio::Log log;
    log.record(entry_of(radio::Verdict::Undecoded));
    log.record(entry_of(radio::Verdict::Unsupported));
    log.record(entry_of(radio::Verdict::Unattempted));

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "WAIT"));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY + kLineH, "TYPE"));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY + 2 * kLineH, "DEC"));
}

// A burst that named no system is a neighbour's dialect, not a frame this firmware got wrong.
TEST_CASE("radio log page: a burst that framed as neither system reads apart from a refusal") {
    radio::Log log;
    log.record(entry_of(radio::Verdict::Undecoded));
    log.record(entry_of(radio::Verdict::Unframed));

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "SYNC"));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY + kLineH, "DEC"));
}

// The row that ends the support case: a device and its neighbour disagreeing on the second.
TEST_CASE("radio log page: a frame keyed on another second prints the offset it wanted") {
    radio::Log log;
    radio::Entry ahead = entry_of(radio::Verdict::Miskeyed);
    ahead.key_offset_s = 18;
    log.record(ahead);
    radio::Entry behind = entry_of(radio::Verdict::Miskeyed);
    behind.key_offset_s = -2;
    log.record(behind);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "KEY-2"));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY + kLineH, "KEY+18"));
}

// A letter per system was a legend to learn; the column the verdicts use holds the word.
TEST_CASE("radio log page: a reception that worked names the system it framed as") {
    radio::Log log;
    radio::Entry flarm = received(0x4C11A0, -93);
    flarm.source = model::Source::Alptas;
    radio::Entry relayed = received(0, -101);
    relayed.source = model::Source::AdslUplink;
    relayed.address_valid = false;
    log.record(flarm);
    log.record(relayed);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "UPLINK"));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY + kLineH, "FLARM"));
    CHECK(reads_at(canvas, 4 + 22 * 6, kFirstRowY + kLineH, "4C11A0"));
}

// Two photos of a bench tape and no way to tell whose frames were being refused.
TEST_CASE("radio log page: a refused frame whose CRC held still names who sent it") {
    radio::Log log;
    radio::Entry refused = entry_of(radio::Verdict::Miskeyed);
    refused.key_offset_s = 18;
    refused.source = model::Source::Alptas;
    refused.address = 0xED4838;
    refused.address_valid = true;
    log.record(refused);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "KEY+18"));
    CHECK(reads_at(canvas, 4 + 22 * 6, kFirstRowY, "ED4838"));
}

// The M band reads a fixed 58 bytes whatever arrived, so the count was never a fact about the air.
TEST_CASE("radio log page: a failed burst spends no column on the length every burst has") {
    radio::Log log;
    radio::Entry bad = entry_of(radio::Verdict::BadCrc);
    bad.len = 58;
    log.record(bad);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK_FALSE(reads_at(canvas, 4 + 20 * 6, kFirstRowY, "58B"));
}

// A transmission that failed, read as a bad reception, sends a reader after the wrong fault.
TEST_CASE("radio log page: own-ship's two outcomes read apart, and both read as TX") {
    struct Case {
        radio::Verdict verdict;
        const char* reads;
    };
    const Case cases[] = {{radio::Verdict::Lost, "LOST"},
                          {radio::Verdict::Held, "HELD"},
                          {radio::Verdict::Unarmed, "LOST"}};
    for (const Case& c : cases) {
        radio::Log log;
        log.record(entry_of(c.verdict));
        Glass canvas;
        draw_radio_log(canvas, with(log));
        CHECK(reads_at(canvas, 4 + 10 * 6, kFirstRowY, "TX"));
        CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, c.reads));
    }
}

// A level nothing measured is a level the page does not print.
TEST_CASE("radio log page: a burst with no level reported shows none") {
    radio::Log log;
    log.record(entry_of(radio::Verdict::BadCrc));

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK_FALSE(reads_at(canvas, 4 + 29 * 6, kFirstRowY, "+0"));
    CHECK(reads_at(canvas, 4 + 16 * 6, kFirstRowY, "CRC"));
}

TEST_CASE("radio log page: the newest burst is the top row and the rest fall away") {
    radio::Log log;
    for (int i = 0; i < radio::Log::kCapacity + 3; i++)
        log.record(received(static_cast<uint32_t>(0xA00000 + i), -80));

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4 + 22 * 6, kFirstRowY, "A00012"));
    CHECK(ink_in_row(canvas, kRadioLogRows - 1) > 0);
    CHECK(ink_in_row(canvas, kRadioLogRows) == 0);
}

// Before a fix the stamp is not a wall clock, and does not pretend to be one.
TEST_CASE("radio log page: without UTC the stamp counts from boot") {
    radio::Log log;
    radio::Entry e = received(0x3FA21C, -87);
    e.utc_dated = false;
    e.at_s = 412;
    log.record(e);

    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4, kFirstRowY, "T+412"));
}

// Without a latched edge nothing may transmit and no row carries a phase, so the
// state of the receiver's one wire explains the whole tape under it.
TEST_CASE("radio log page: the second line is the fix, the edge and the band") {
    radio::Log log;
    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4, 13, "3D 9SV"));
    CHECK(reads_at(canvas, 4 + 9 * 6, 13, "PPS LOCK"));
    CHECK(reads_at(canvas, 196 - 9 * 6, 13, "BAND -109"));
}

TEST_CASE("radio log page: an edge that stopped arriving reads as holdover, with its age") {
    radio::Log log;
    RadioLogSnapshot snapshot = with(log);
    snapshot.gnss.pps = PpsState::Holdover;
    snapshot.gnss.pps_age_s = 12;

    Glass canvas;
    draw_radio_log(canvas, snapshot);
    CHECK(reads_at(canvas, 4 + 9 * 6, 13, "PPS HOLD 12"));
}

TEST_CASE("radio log page: no edge at all is stated rather than left blank") {
    radio::Log log;
    RadioLogSnapshot snapshot = with(log);
    snapshot.gnss.pps = PpsState::None;

    Glass canvas;
    draw_radio_log(canvas, snapshot);
    CHECK(reads_at(canvas, 4 + 9 * 6, 13, "PPS NONE"));
}

TEST_CASE("radio log page: no fix is stated rather than left blank") {
    radio::Log log;
    RadioLogSnapshot snapshot = with(log);
    snapshot.gnss.fix_valid = false;
    snapshot.gnss.sats = 0;

    Glass canvas;
    draw_radio_log(canvas, snapshot);
    CHECK(reads_at(canvas, 4, 13, "NO FIX"));
}

// The false syncs are the proof the receiver is listening when nothing arrives.
TEST_CASE("radio log page: the bursts the band's own noise framed are counted, not listed") {
    radio::Log log;
    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 196 - 18 * 6, 2, "RX 0 TX 0 NOISE 42"));
}

TEST_CASE("radio log page: a silent band says so rather than showing an empty grid") {
    radio::Log log;
    Glass canvas;
    draw_radio_log(canvas, with(log));
    CHECK(reads_at(canvas, 4, kFirstRowY + kLineH, "NOTHING ON AIR YET"));
}
