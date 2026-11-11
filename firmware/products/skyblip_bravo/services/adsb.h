#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_ADSB_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_ADSB_H

#include <cstdint>

#include "core/bus/bus.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsb_assembly.h"
#include "products/common/services/adsb_capture.h"
#include "products/common/services/traffic.h"
#include "runtime/service.h"

namespace skyblip::bravo {

// INFO: fc 12nov26 owns state.traffic on Bravo: 1090 MHz is its only source, so no TrafficService
class AdsbService : public runtime::Service {
   public:
    AdsbService(runtime::Context& context, bus::AdsbQueue& frames, common::TrafficSettings settings)
        : runtime::Service(context), frames_(frames), settings_(settings), capture_(context) {}

    void tick(uint32_t now_ms) override;

    uint32_t invalid() const { return invalid_; }
    uint32_t folded() const { return folded_; }
    const protocol::AdsbAssembly& assembly() const { return assembly_; }
    const common::AdsbCapture& capture() const { return capture_; }

   private:
    protocol::AdsbReference reference() const;
    void hear(const events::AdsbFrame& frame, const protocol::AdsbReference& here, uint32_t now_ms);

    bus::AdsbQueue& frames_;
    common::TrafficSettings settings_;
    common::AdsbCapture capture_;
    protocol::AdsbAssembly assembly_{};
    model::AircraftObservation observation_{};
    uint32_t invalid_{0};
    uint32_t folded_{0};
};

}  // namespace skyblip::bravo

#endif
