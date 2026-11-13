// Harness, not a test: a busy and a widest device snapshot, and the dump read back as console lines
// and as link frames.
#ifndef SKYBLIP_TEST_SUPPORT_DIAGNOSTICS_REPORT_H
#define SKYBLIP_TEST_SUPPORT_DIAGNOSTICS_REPORT_H

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>

#include "core/comms/diagnostics.h"
#include "core/comms/link_registry.h"
#include "doctest/doctest.h"
#include "test/support/config_frame.h"

namespace skyblip {

using comms::Diagnostics;
using comms::DiagnosticsReport;

// A device that has been up for a while and has something to complain about, so
// that no field reads as its default.
inline Diagnostics busy_device() {
    Diagnostics diagnostics{};
    diagnostics.refreshes = 12;
    diagnostics.uptime_s = 3725;
    diagnostics.reset = power::ResetReason::Watchdog;
    diagnostics.link_drops = 2;

    diagnostics.noise_dbm = -101;
    diagnostics.duty_permille = 7;
    diagnostics.rx_ok = 1204;
    diagnostics.rx_bad = 37;
    diagnostics.rx_wait = 58;
    diagnostics.rx_type = 19;
    diagnostics.rx_unframed = 11;
    diagnostics.rx_miskeyed = 7;
    diagnostics.rx_noise = 96;
    diagnostics.rx_named = 12;
    diagnostics.tx_ok = 880;
    diagnostics.tx_lost = 3;
    diagnostics.tx_named = 41;
    diagnostics.tx_held_for_b5 = 6;
    diagnostics.rx_status = 9;
    diagnostics.uplink_stale = 2;
    diagnostics.tx_given_up = 1;
    diagnostics.tx_keyed_us = 609;
    diagnostics.tx_span_us = 5919;
    diagnostics.range_refused = 5;
    diagnostics.dio1_armed = true;
    diagnostics.dio1_edges = 1500;
    diagnostics.dio1_missed = 4;

    diagnostics.tracked = 4;
    diagnostics.alarm = 2;

    diagnostics.gnss_fixes = 5210;
    diagnostics.fix_valid = true;
    diagnostics.gnss_baud = 38400;
    diagnostics.gnss_config = ports::GnssConfigStage::Ready;
    diagnostics.gnss_raises = 1;
    diagnostics.gnss_identified = true;
    diagnostics.gnss_firmware = "URANUS5,V5.1.0.0";
    diagnostics.gnss_reject = gnss::FixReject::Stale;
    diagnostics.gnss_rejected = 6;
    diagnostics.gnss_overruns = 0;
    diagnostics.gnss_solution_phase_ms = 98;
    diagnostics.gnss_solution_phase_valid = true;
    diagnostics.residual_m = 13;
    diagnostics.residual_valid = true;

    diagnostics.battery.cell_mv = 3812;
    diagnostics.battery.percent = 64;
    diagnostics.battery.valid = true;
    diagnostics.battery.charging = false;
    diagnostics.power_level = power::PowerLevel::Normal;
    diagnostics.supply_warnings = 1;
    diagnostics.battery_implausible = 2;
    diagnostics.die_dc = 415;
    diagnostics.die_valid = true;

    diagnostics.adsb_fitted = true;
    diagnostics.adsb.frames = 51234;
    diagnostics.adsb.frame_rate_hz = 212;
    diagnostics.adsb_crc = 3021;
    diagnostics.adsb_unsupported = 412;
    diagnostics.adsb_folded = 1830;
    diagnostics.adsb_aircraft = 7;
    diagnostics.adsb.dropped = 4;
    diagnostics.adsb.rearms = 1;
    diagnostics.adsb.device_errors = 0x0080;
    const uint32_t levels[ports::AdsbReceiverStatistics::kRssiBuckets] = {0,    120, 900, 3000,
                                                                          2500, 800, 90,  3};
    std::copy(std::begin(levels), std::end(levels), std::begin(diagnostics.adsb.rssi));
    return diagnostics;
}

// Every counter at the widest value the dump carries, every string at the longest it can hold.
inline Diagnostics widest_device() {
    Diagnostics diagnostics{};
    diagnostics.refreshes = 0xFFFFFFFFu;
    diagnostics.uptime_s = 0xFFFFFFFFu;
    diagnostics.reset = power::ResetReason::Lockup;  // "CPU LOCKUP"
    diagnostics.link_drops = 0xFFFFFFFFu;
    diagnostics.noise_dbm = -128;
    diagnostics.duty_permille = 1000;
    diagnostics.rx_ok = 0xFFFFFFFFu;
    diagnostics.rx_bad = 0xFFFFFFFFu;
    diagnostics.rx_wait = 0xFFFFFFFFu;
    diagnostics.rx_type = 0xFFFFFFFFu;
    diagnostics.rx_unframed = 0xFFFFFFFFu;
    diagnostics.rx_miskeyed = 0xFFFFFFFFu;
    diagnostics.rx_noise = 0xFFFFFFFFu;
    diagnostics.rx_named = 0xFFFFFFFFu;
    diagnostics.tx_ok = 0xFFFFFFFFu;
    diagnostics.tx_lost = 0xFFFFFFFFu;
    diagnostics.tx_named = 0xFFFFFFFFu;
    diagnostics.tx_held_for_b5 = 0xFFFFFFFFu;
    diagnostics.rx_status = 0xFFFFFFFFu;
    diagnostics.uplink_stale = 0xFFFFFFFFu;
    diagnostics.tx_given_up = 0xFFFFFFFFu;
    diagnostics.tx_keyed_us = 65535;
    diagnostics.tx_span_us = 65535;
    diagnostics.range_refused = 0xFFFFFFFFu;
    diagnostics.dio1_armed = false;
    diagnostics.dio1_edges = 0xFFFFFFFFu;
    diagnostics.dio1_missed = 0xFFFFFFFFu;
    diagnostics.tracked = 0xFFFFFFFFu;
    diagnostics.alarm = 255;
    diagnostics.gnss_fixes = 0xFFFFFFFFu;
    diagnostics.gnss_baud = 921600;
    diagnostics.gnss_config = ports::GnssConfigStage::Degraded;  // "DEGRADED"
    diagnostics.gnss_raises = 0xFFFFFFFFu;
    diagnostics.gnss_raise_fallbacks = 0xFFFFFFFFu;
    // The driver's version buffer is 24 bytes, so 23 characters is the longest
    // string a receiver can ever hand us (core/gnss/nmea.h::kVersionBytes).
    diagnostics.gnss_firmware = "ABCDEFGHIJKLMNOPQRSTUVW";
    diagnostics.gnss_reject = gnss::FixReject::NoSolution;  // "NO SOLUTION"
    diagnostics.gnss_rejected = 0xFFFFFFFFu;
    diagnostics.gnss_overruns = 0xFFFFFFFFu;
    diagnostics.gnss_solution_phase_ms = 999;
    diagnostics.gnss_solution_phase_valid = true;
    diagnostics.residual_m = 65535;
    diagnostics.residual_valid = true;
    diagnostics.battery.cell_mv = 65535;
    diagnostics.battery.percent = 255;
    diagnostics.power_level = power::PowerLevel::Critical;  // "CRITICAL"
    diagnostics.supply_warnings = 0xFFFFFFFFu;
    diagnostics.battery_implausible = 0xFFFFFFFFu;
    diagnostics.die_dc = -1250;
    diagnostics.die_valid = true;
    diagnostics.charge = power::ChargeCondition::TooCold;
    diagnostics.charge_warnings = 0xFFFFFFFFu;
    diagnostics.adsb_fitted = true;
    diagnostics.adsb.frames = 0xFFFFFFFFu;
    diagnostics.adsb.frame_rate_hz = 0xFFFFFFFFu;
    diagnostics.adsb_crc = 0xFFFFFFFFu;
    diagnostics.adsb_unsupported = 0xFFFFFFFFu;
    diagnostics.adsb_folded = 0xFFFFFFFFu;
    diagnostics.adsb_aircraft = 0xFFFFFFFFu;
    diagnostics.adsb.dropped = 0xFFFFFFFFu;
    diagnostics.adsb.rearms = 0xFFFFFFFFu;
    diagnostics.adsb.resets = 0xFFFFFFFFu;
    diagnostics.adsb.command_errors = 0xFFFFFFFFu;
    diagnostics.adsb.device_errors = 0xFFFF;
    std::fill(std::begin(diagnostics.adsb.rssi), std::end(diagnostics.adsb.rssi), 0xFFFFFFFFu);
    return diagnostics;
}

inline std::string console(const Diagnostics& diagnostics) {
    DiagnosticsReport report(diagnostics, link::Command::Diagnostics);
    std::string all;
    char line[DiagnosticsReport::kLineBytes];
    for (int i = 0; i < report.line_count(); i++) {
        const int written = report.line(i, line, static_cast<int>(sizeof(line)));
        REQUIRE(written > 0);
        CHECK(static_cast<int>(std::string(line).size()) == written);
        all += line;
        all += "\n";
    }
    return all;
}

// Every frame the report produces at a given payload, joined, plus a count.
struct Frames {
    std::string joined;
    int count{0};
    int widest{0};
    std::string last;
};

inline std::string text(const uint8_t* frame, int bytes) {
    return link_text(std::string(reinterpret_cast<const char*>(frame), static_cast<size_t>(bytes)));
}

inline Frames frames(const Diagnostics& diagnostics, int payload,
                     link::Command command = link::Command::Diagnostics) {
    DiagnosticsReport report(diagnostics, command);
    Frames out;
    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    while (!report.exhausted()) {
        const int written = report.next_frame(payload, buffer, static_cast<int>(sizeof(buffer)));
        REQUIRE(written > 0);
        // INFO: fc 13nov26 REQUIRE does not stop this build, so a frame that is not cut ends it
        if (written <= 0) break;
        REQUIRE(written <= payload);
        out.last = text(buffer, written);
        REQUIRE(!out.last.empty());
        out.joined += out.last;
        if (written > out.widest) out.widest = written;
        out.count++;
        REQUIRE(out.count < 32);  // a dump that never ends is a bug, not a report
    }
    return out;
}

inline bool has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace skyblip

#endif
