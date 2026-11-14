#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_DIAGNOSTICS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_DIAGNOSTICS_H

#include "core/bus/state.h"
#include "core/comms/diagnostics.h"
#include "hardware/parts/ag3335/ag3335.h"
#include "ports/capabilities.h"

namespace skyblip::bravo {

// INFO: fc 12nov26 the Go's collector with Bravo's parts: the adsb group, the AG3335, no SX1262
template <class Product, class Sink>
class DiagnosticsDump {
   public:
    void step(Product& product, uint32_t now_ms) {
        comms::Diagnostics& snapshot = product.config_link().config().diagnostics();
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
    void refresh(Product& product, comms::Diagnostics& snapshot, uint32_t now_ms) {
        const bus::State& state = product.state();
        snapshot.uptime_s = now_ms / 1000;

        snapshot.tracked = static_cast<uint32_t>(state.traffic.count());
        snapshot.alarm = traffic::to_number(state.alarm_level);
        snapshot.uplink_frames = state.air.uplink_frames;
        snapshot.uplink_bad = state.air.uplink_bad;
        snapshot.uplink_targets = state.air.uplink_targets;

        snapshot.gnss_fixes = state.flight.gnss_solutions;
        snapshot.fix_valid = state.own.fix_valid;
        snapshot.residual_m = state.own.prediction_residual_m;
        snapshot.residual_valid = state.own.prediction_residual_valid;
        const parts::Ag3335& gnss = product.board().gnss();
        snapshot.gnss_baud = parts::Ag3335::kBaudRate;
        snapshot.gnss_config = gnss.config_stage();
        snapshot.gnss_identified = gnss.identified();
        snapshot.gnss_firmware = gnss.firmware_version();
        snapshot.gnss_reject = gnss.last_reject();
        snapshot.gnss_rejected = gnss.rejected();
        snapshot.gnss_overruns = gnss.port_overruns();
        snapshot.gnss_solution_phase_ms = state.gnss.solution_phase_ms;
        snapshot.gnss_solution_phase_valid = state.gnss.solution_phase_valid;

        snapshot.supply_warnings = product.power().supply_warnings();
        snapshot.battery_implausible = product.power().implausible_samples();
        snapshot.charge = product.power().charge_condition();
        snapshot.charge_warnings = product.power().charge_warnings();

        snapshot.adsb_fitted = ports::has(product.capabilities(), ports::Capability::AdsbReceiver);
        snapshot.adsb = product.board().receiver().statistics();
        snapshot.adsb_crc = product.adsb().crc();
        snapshot.adsb_unsupported = product.adsb().unsupported();
        snapshot.adsb_folded = product.adsb().folded();
        snapshot.adsb_aircraft = static_cast<uint32_t>(product.adsb().assembly().count());

        product.capture().describe(snapshot);

        snapshot.refreshes++;
    }

    void emit(const comms::Diagnostics& snapshot) {
        if (!sink_.ready()) return;
        comms::DiagnosticsReport report(snapshot, link::Command::Diagnostics);
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

}  // namespace skyblip::bravo

#endif
