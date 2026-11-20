#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_DIAGNOSTICS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_DIAGNOSTICS_H

#include "core/bus/state.h"
#include "core/comms/diagnostics.h"
#include "ports/rf.h"
#include "products/common/diagnostics_collector.h"

namespace skyblip::go {

struct RadioDiagnostics {
    template <class Product>
    static void collect(Product& product, comms::Diagnostics& snapshot, uint32_t /*now_ms*/) {
        const bus::State& state = product.state();
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
        snapshot.tx_held_for_b5 = state.air.tx_held_for_b5;
        snapshot.rx_status = state.air.rx_status;
        snapshot.uplink_stale = state.air.uplink_stale;
        snapshot.tx_given_up = state.air.tx_given_up;
        snapshot.tx_deferred = state.air.tx_deferred;
        snapshot.tx_forced = state.air.tx_forced;
        snapshot.tx_busy_missed = state.air.tx_busy_missed;
        snapshot.tx_held_for_fix = state.air.tx_held_for_fix;
        snapshot.tx_redrawn_for_fix = state.air.tx_redrawn_for_fix;
        snapshot.no_tx = state.transmit.unmet();
        snapshot.tx_keyed_us = state.rf.last_tx_keyed_us;
        snapshot.tx_span_us = state.rf.last_tx_span_us;
        const ports::RfDio1 dio1 = product.board().roles().rf.dio1();
        snapshot.dio1_armed = dio1.armed;
        snapshot.dio1_edges = dio1.edges;
        snapshot.dio1_missed = dio1.missed;
        snapshot.uplink_frames = state.air.uplink_frames;
        snapshot.uplink_bad = state.air.uplink_bad;
        snapshot.uplink_targets = state.air.uplink_targets;

        const auto& gnss = product.board().gnss();
        snapshot.gnss_baud = gnss.baud_rate();
        snapshot.gnss_raises = gnss.baud_raises();
        snapshot.gnss_raise_fallbacks = gnss.raise_fallbacks();
        snapshot.gnss_recoveries = gnss.recoveries();
        snapshot.gnss_recoveries_valid = true;
    }
};

template <class Product>
using DiagnosticsCollector = common::DiagnosticsCollector<Product, RadioDiagnostics>;

}  // namespace skyblip::go

#endif
