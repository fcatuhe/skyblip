// The page a bench reads when a burst went missing, in the units the counters keep.
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/raw.h"
#include "test/support/glass_text.h"

using namespace skyblip::go;
using skyblip::reads_in;

namespace {

RawSnapshot bench_device() {
    RawSnapshot snapshot;
    snapshot.uptime_s = 1234;

    snapshot.gnss.health.baud = 115200;
    snapshot.gnss.health.config_stage = skyblip::ports::GnssConfigStage::Ready;
    snapshot.gnss.health.overruns = 0;
    snapshot.gnss.health.sentences = 5210;
    snapshot.gnss.health.rejected = 6;
    snapshot.gnss.health.pps_latency_ms = 28;
    snapshot.gnss.health.last_reject = skyblip::gnss::FixReject::Stale;
    snapshot.gnss.health.identified = true;
    snapshot.gnss.nav_ms = 98;
    snapshot.gnss.nav_valid = true;
    snapshot.gnss.fix_valid = true;
    snapshot.gnss.satellites = 22;
    snapshot.gnss.in_use = 12;
    snapshot.gnss.fix_mode = 3;
    snapshot.gnss.hdop_e2 = 90;
    snapshot.gnss.vdop_e2 = 150;
    snapshot.gnss.resid_m = 3;
    snapshot.gnss.resid_valid = true;
    snapshot.gnss.settled = true;
    snapshot.gnss.levels_live = true;
    snapshot.gnss.solutions = 5210;
    snapshot.gnss.utc_s = 12 * 3600 + 34 * 60 + 56;
    snapshot.gnss.utc_valid = true;
    snapshot.gnss.pps = PpsState::Lock;
    snapshot.gnss.pps_age_ms = 3;

    snapshot.radio.rx_ok = 30;
    snapshot.radio.rx_bad = 2;
    snapshot.radio.rx_unframed = 1;
    snapshot.radio.rx_miskeyed = 0;
    snapshot.radio.rx_noise = 16;
    snapshot.radio.rx_type = 4;
    snapshot.radio.rx_wait = 7;
    snapshot.radio.uplink_frames = 4;
    snapshot.radio.uplink_bad = 0;
    snapshot.radio.uplink_targets = 3;
    snapshot.radio.tx_ok = 19;
    snapshot.radio.tx_lost = 1;
    snapshot.radio.missed = 2;
    snapshot.radio.refused = 0;
    snapshot.radio.duty_permille = 5;
    snapshot.radio.tx_keyed_us = 609;
    snapshot.radio.tx_span_us = 5919;
    snapshot.radio.dwell_worst_us = 120;
    snapshot.radio.pps_worst_us = -12;
    snapshot.radio.holdover = 0;
    snapshot.radio.tracked = 4;
    snapshot.radio.alarm = 1;
    snapshot.radio.noise_dbm = -104;
    snapshot.radio.slot = skyblip::timing::SlotState::Slot0;
    snapshot.radio.frequency_hz = skyblip::timing::kMband0Hz;
    snapshot.radio.tx_allowed = true;
    return snapshot;
}

}  // namespace

TEST_CASE("raw page: the receiver block names the rate, the sentence phase and every refusal") {
    Glass canvas;
    draw_raw(canvas, bench_device());

    CHECK(reads_in(canvas, "PORT 115200 READY", 0, 12, 200, 30));
    CHECK(reads_in(canvas, "OVR 0", 0, 12, 200, 30));
    CHECK(reads_in(canvas, "NAV 098MS", 0, 24, 200, 41));
    CHECK(reads_in(canvas, "WIRE 28MS", 0, 24, 200, 41));
    CHECK(reads_in(canvas, "SENT 5210", 0, 24, 200, 41));
    CHECK(reads_in(canvas, "FIX OK SV 22 USE 12", 0, 35, 200, 52));
    CHECK(reads_in(canvas, "HDOP 0.90 VDOP 1.50 RES 3M", 0, 46, 200, 63));
    CHECK(reads_in(canvas, "REJ STALE N 6", 0, 57, 200, 74));
    CHECK(reads_in(canvas, "PPS LOCK 3MS", 0, 68, 200, 85));
    CHECK(reads_in(canvas, "UTC 12:34:56", 0, 68, 200, 85));
}

TEST_CASE("raw page: the radio block names the dwell it is in and every verdict it has counted") {
    Glass canvas;
    draw_raw(canvas, bench_device());

    CHECK(reads_in(canvas, "DWELL M0 868.2 TX Y", 0, 82, 200, 100));
    CHECK(reads_in(canvas, "-104DBM", 100, 82, 200, 100));
    CHECK(reads_in(canvas, "RX 30 CRC 2 SYNC 1 KEY 0", 0, 93, 200, 111));
    CHECK(reads_in(canvas, "DEC 16 TYPE 4 WAIT 7", 0, 104, 200, 122));
    CHECK(reads_in(canvas, "UPLINK 4/0/3 TRK 4 ALM 1", 0, 115, 200, 133));
    CHECK(reads_in(canvas, "TX 19 LOST 1 MISS 2 HELD 0", 0, 126, 200, 144));
    CHECK(reads_in(canvas, "DUTY 5/1000 KEYED 609US", 0, 137, 200, 155));
    CHECK(reads_in(canvas, "SPAN 5919US SLOT 120US", 0, 148, 200, 166));
    CHECK(reads_in(canvas, "PPS -12US HOLDOVER 0", 0, 159, 200, 177));
}

// A zero would read as a solution landing at the top of the second, which it never does.
TEST_CASE("raw page: a solution nothing has dated reports no phase at all") {
    RawSnapshot snapshot = bench_device();
    snapshot.gnss.nav_valid = false;
    snapshot.gnss.utc_valid = false;
    snapshot.gnss.pps = PpsState::None;
    snapshot.gnss.fix_valid = false;
    snapshot.gnss.stage = skyblip::gnss::Stage::Blind;
    snapshot.gnss.resid_valid = false;
    snapshot.gnss.hdop_e2 = 0;
    Glass canvas;
    draw_raw(canvas, snapshot);

    CHECK(reads_in(canvas, "NAV ---", 0, 24, 200, 41));
    CHECK(reads_in(canvas, "FIX BLIND", 0, 35, 200, 52));
    CHECK(reads_in(canvas, "HDOP --- VDOP 1.50 RES ---", 0, 46, 200, 63));
    CHECK(reads_in(canvas, "PPS NONE", 0, 68, 200, 85));
    CHECK(reads_in(canvas, "UTC --:--:--", 0, 68, 200, 85));
}
