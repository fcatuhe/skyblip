#ifndef SKYBLIP_PRODUCTS_COMMON_DIAGNOSTICS_COLLECTOR_H
#define SKYBLIP_PRODUCTS_COMMON_DIAGNOSTICS_COLLECTOR_H

#include "core/bus/state.h"
#include "core/comms/diagnostics.h"

namespace skyblip::common {

// INFO: fc 19nov26 a reader, not a service: it copies counters only the product reaches
template <class Product, class Group>
class DiagnosticsCollector {
   public:
    void step(Product& product, uint32_t now_ms) {
        if (started_ && now_ms - refreshed_ms_ < comms::kDiagnosticsRefreshPeriodMs) return;
        started_ = true;
        refreshed_ms_ = now_ms;
        refresh(product, product.config_link().config().diagnostics(), now_ms);
    }

   private:
    static void refresh(Product& product, comms::Diagnostics& snapshot, uint32_t now_ms) {
        const bus::State& state = product.state();
        snapshot.uptime_s = now_ms / 1000;

        snapshot.tracked = static_cast<uint32_t>(state.traffic.count());
        snapshot.alarm = traffic::to_number(state.alarm_level);

        snapshot.gnss_fixes = state.flight.gnss_solutions;
        snapshot.fix_valid = state.own.fix_valid;
        snapshot.residual_m = state.own.prediction_residual_m;
        snapshot.residual_valid = state.own.prediction_residual_valid;
        const auto& gnss = product.board().gnss();
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

        Group::collect(product, snapshot, now_ms);
        product.capture().describe(snapshot);
        snapshot.refreshes++;
    }

    uint32_t refreshed_ms_{0};
    bool started_{false};
};

}  // namespace skyblip::common

#endif
