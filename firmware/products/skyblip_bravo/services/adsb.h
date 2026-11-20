#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_ADSB_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_ADSB_H

#include <array>
#include <cstdint>

#include "core/bus/bus.h"
#include "core/model/aircraft.h"
#include "products/common/services/adsb_capture.h"
#include "products/common/traffic_settings.h"
#include "products/skyblip_bravo/gate_reference.h"
#include "products/skyblip_bravo/own_transponder.h"
#include "products/skyblip_bravo/transponders.h"
#include "protocols/adsb/assembly.h"
#include "protocols/adsb/message.h"
#include "runtime/service.h"

namespace skyblip::bravo {

// INFO: fc 12nov26 owns state.traffic on Bravo: 1090 MHz is its only source, so no TrafficService
class AdsbService : public runtime::Service {
   public:
    AdsbService(runtime::Context& context, bus::AdsbQueue& frames, common::TrafficSettings settings,
                const GateReference& gate)
        : runtime::Service(context),
          frames_(frames),
          settings_(settings),
          gate_(gate),
          capture_(context) {}

    void tick(uint32_t now_ms) override;
    // INFO: fc 19nov26 the receiver wakes nothing, so the queue is drained inside its window
    uint32_t due_in_ms(uint32_t now_ms) const override;

    uint32_t crc() const { return crc_; }
    uint32_t unsupported() const { return unsupported_; }
    uint32_t folded() const { return folded_; }
    const std::array<uint32_t, adsb::kDownlinkFormats>& formats() const { return formats_; }
    const adsb::Assembly& assembly() const { return assembly_; }
    const common::AdsbCapture& capture() const { return capture_; }
    const OwnTransponder& own_transponder() const { return own_transponder_; }
    const Transponders& transponders() const { return transponders_; }

   private:
    static adsb::Reference reference(const model::Ownship& own);
    void hear(const events::AdsbFrame& frame, const adsb::Reference& here, uint32_t now_ms);
    void refuse(Status status);
    void count_format(uint8_t downlink_format);

    bus::AdsbQueue& frames_;
    common::TrafficSettings settings_;
    const GateReference& gate_;
    common::AdsbCapture capture_;
    adsb::Assembly assembly_{};
    OwnTransponder own_transponder_{};
    Transponders transponders_{};
    model::AircraftObservation observation_{};
    uint32_t crc_{0};
    uint32_t unsupported_{0};
    uint32_t folded_{0};
    std::array<uint32_t, adsb::kDownlinkFormats> formats_{};
};

}  // namespace skyblip::bravo

#endif
