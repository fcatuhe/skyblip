#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_DIAGNOSTICS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_DIAGNOSTICS_H

#include <algorithm>
#include <iterator>

#include "core/comms/diagnostics.h"
#include "hardware/parts/ag3335/ag3335.h"
#include "ports/capabilities.h"
#include "products/common/diagnostics_collector.h"
#include "products/skyblip_bravo/transponders.h"

namespace skyblip::bravo {

struct AdsbDiagnostics {
    template <class Product>
    static void collect(Product& product, comms::Diagnostics& snapshot, uint32_t now_ms) {
        snapshot.gnss_baud = parts::Ag3335::kBaudRate;
        snapshot.adsb_fitted = ports::has(product.capabilities(), ports::Capability::AdsbReceiver);
        snapshot.adsb = product.board().receiver().statistics();
        snapshot.adsb_crc = product.adsb().crc();
        snapshot.adsb_unsupported = product.adsb().unsupported();
        snapshot.adsb_folded = product.adsb().folded();
        snapshot.adsb_aircraft = static_cast<uint32_t>(product.adsb().assembly().count());
        const auto& formats = product.adsb().formats();
        std::copy(formats.begin(), formats.end(), std::begin(snapshot.adsb_formats));
        const Transponders& transponders = product.adsb().transponders();
        snapshot.adsb_transponders = static_cast<uint32_t>(transponders.count());
        snapshot.adsb_mode_s = static_cast<uint32_t>(transponders.mode_s_only());
        snapshot.adsb_mode_s_altitude =
            static_cast<uint32_t>(transponders.mode_s_only_with_altitude(now_ms));
        snapshot.adsb_unmatched = transponders.unmatched();
    }
};

template <class Product>
using DiagnosticsCollector = common::DiagnosticsCollector<Product, AdsbDiagnostics>;

}  // namespace skyblip::bravo

#endif
