#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_DIAGNOSTICS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_DIAGNOSTICS_H

#include "core/bus/state.h"
#include "core/comms/diagnostics.h"
#include "ports/rf.h"

namespace skyblip::go {

// The dump's collector: it reads counters the device already keeps and writes
// them into the one snapshot the companion link answers from
// (core/comms/diagnostics.h), then puts the same table on the console every
// kDiagnosticsDumpPeriodMs. No formatting and no policy live here - that is core's, and
// host-tested there.
//
// Not a runtime::Service, deliberately. A service is handed roles, the bus and
// bus::State; six of these numbers belong to sibling services and five to the GNSS
// part, and neither is reachable through a Context. Reaching for them would mean
// publishing counters through the bus for one reader, so instead this is driven
// from the shell with the product in hand, which is also the only place that knows
// how to write to a console.
//
// Sink is whatever can take a line: platform::zephyr::Console on silicon, a
// capturing sink in the tests. It answers ready() so a device with no host on its
// USB port formats nothing at all.
template <class Product, class Sink>
class DiagnosticsDump {
   public:
    void step(Product& product, uint32_t now_ms) {
        comms::Diagnostics& snapshot = product.config_link().config().diagnostics();
        // Unsigned subtraction throughout, so the 49.7-day wrap of a 32-bit
        // millisecond counter costs one late dump and nothing else.
        if (!started_ || now_ms - refreshed_ms_ >= comms::kDiagnosticsRefreshPeriodMs) {
            refresh(product, snapshot, now_ms);
            refreshed_ms_ = now_ms;
        }
        if (!started_ || now_ms - dumped_ms_ >= comms::kDiagnosticsDumpPeriodMs) {
            dumped_ms_ = now_ms;
            emit(snapshot);
        }
        started_ = true;
    }

    Sink& sink() { return sink_; }

   private:
    // Every read here is a const accessor of a counter that lives somewhere else.
    // Nothing is cleared by being read: a dump is a witness, and a witness that
    // consumes its evidence makes the second reader wrong.
    void refresh(Product& product, comms::Diagnostics& snapshot, uint32_t now_ms) {
        const bus::State& state = product.state();

        snapshot.uptime_s = now_ms / 1000;

        snapshot.noise_dbm = product.radio().noise_floor().level_dbm();
        snapshot.duty_permille = state.rf.duty_permille;
        snapshot.rx_ok = state.air.rx_ok;
        snapshot.rx_bad = state.air.rx_bad;
        snapshot.rx_wait = state.air.rx_wait;
        snapshot.rx_type = state.air.rx_type;
        snapshot.rx_unframed = state.air.rx_unframed;
        snapshot.rx_miskeyed = state.air.rx_miskeyed;
        snapshot.rx_noise = state.air.rx_noise;
        snapshot.rx_named = state.air.rx_named;
        snapshot.tx_ok = state.air.tx_ok;
        snapshot.tx_lost = state.air.tx_lost;
        snapshot.tx_named = state.air.tx_named;
        snapshot.tx_keyed_us = state.rf.last_tx_keyed_us;
        snapshot.tx_span_us = state.rf.last_tx_span_us;
        const ports::RfDio1 dio1 = product.board().roles().rf.dio1();
        snapshot.dio1_armed = dio1.armed;
        snapshot.dio1_edges = dio1.edges;
        snapshot.dio1_missed = dio1.missed;

        snapshot.tracked = static_cast<uint32_t>(state.traffic.count());
        snapshot.alarm = traffic::to_number(state.alarm_level);

        snapshot.gnss_fixes = state.flight.gnss_solutions;
        snapshot.fix_valid = state.own.fix_valid;
        snapshot.residual_m = state.own.prediction_residual_m;
        snapshot.residual_valid = state.own.prediction_residual_valid;

        // The receiver's own answers, which nothing else on the device reads: a
        // unit talking at a rate the devicetree did not ask for, or one whose fixes
        // are being refused for a named reason, is the support case this part
        // generates. The part is reached through the board because it is a part and
        // not a role - there is no ports:: seam that could carry a firmware string.
        const auto& gnss = product.board().gnss();
        snapshot.gnss_baud = gnss.baud_rate();
        snapshot.gnss_config = gnss.config_stage();
        snapshot.gnss_raises = gnss.baud_raises();
        snapshot.gnss_raise_fallbacks = gnss.raise_fallbacks();
        snapshot.gnss_identified = gnss.identified();
        snapshot.gnss_firmware = gnss.firmware_version();
        snapshot.gnss_reject = gnss.last_reject();
        snapshot.gnss_rejected = gnss.rejected();
        snapshot.gnss_overruns = gnss.port_overruns();
        snapshot.gnss_solution_phase_ms = state.gnss.solution_phase_ms;
        snapshot.gnss_solution_phase_valid = state.gnss.solution_phase_valid;

        // The two counters core/power keeps and acts on nothing for: comparator
        // firings, and readings the sanity floor threw away. They tell a bench a
        // supply problem from a divider problem and they had no reader at all.
        snapshot.supply_warnings = product.power().supply_warnings();
        snapshot.battery_implausible = product.power().implausible_samples();
        snapshot.charge = product.power().charge_condition();
        snapshot.charge_warnings = product.power().charge_warnings();

        snapshot.refreshes++;
    }

    void emit(const comms::Diagnostics& snapshot) {
        if (!sink_.ready()) return;
        comms::DiagnosticsReport report(snapshot, "diag");
        char line[comms::DiagnosticsReport::kLineBytes];
        for (int i = 0; i < report.line_count(); i++) {
            const int length = report.line(i, line, static_cast<int>(sizeof(line)));
            if (length > 0) sink_.line(line, length);
        }
    }

    Sink sink_{};
    uint32_t refreshed_ms_{0};
    uint32_t dumped_ms_{0};
    bool started_{false};
};

}  // namespace skyblip::go

#endif
