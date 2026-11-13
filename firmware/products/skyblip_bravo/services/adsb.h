#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_ADSB_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_ADSB_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/bus/bus.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsb.h"
#include "core/protocol/adsb_assembly.h"
#include "core/util/fifo.h"
#include "core/util/result.h"
#include "products/common/services/adsb_capture.h"
#include "products/common/services/traffic.h"
#include "products/skyblip_bravo/gate_reference.h"
#include "products/skyblip_bravo/own_transponder.h"
#include "runtime/service.h"

namespace skyblip::bravo {

struct AdsbReject {
    Status status{Status::Ok};
    int8_t rssi_dbm{0};
    bool rssi_valid{false};
    std::array<uint8_t, protocol::kAdsbChipBytes> chips{};
};

// INFO: fc 12nov26 owns state.traffic on Bravo: 1090 MHz is its only source, so no TrafficService
class AdsbService : public runtime::Service {
   public:
    static constexpr size_t kRejectsKept = 32;

    AdsbService(runtime::Context& context, bus::AdsbQueue& frames, common::TrafficSettings settings,
                const GateReference& gate)
        : runtime::Service(context),
          frames_(frames),
          settings_(settings),
          gate_(gate),
          capture_(context) {}

    void tick(uint32_t now_ms) override;

    uint32_t crc() const { return crc_; }
    uint32_t unsupported() const { return unsupported_; }
    uint32_t folded() const { return folded_; }
    bool take_reject(AdsbReject& out);
    const protocol::AdsbAssembly& assembly() const { return assembly_; }
    const common::AdsbCapture& capture() const { return capture_; }
    const OwnTransponder& own_transponder() const { return own_transponder_; }

   private:
    static protocol::AdsbReference reference(const model::Ownship& own);
    void hear(const events::AdsbFrame& frame, const protocol::AdsbReference& here, uint32_t now_ms);
    void refuse(const events::AdsbFrame& frame, Status status);

    bus::AdsbQueue& frames_;
    common::TrafficSettings settings_;
    const GateReference& gate_;
    common::AdsbCapture capture_;
    protocol::AdsbAssembly assembly_{};
    OwnTransponder own_transponder_{};
    model::AircraftObservation observation_{};
    uint32_t crc_{0};
    uint32_t unsupported_{0};
    uint32_t folded_{0};
    Fifo<AdsbReject, kRejectsKept + 1> rejects_{};
};

}  // namespace skyblip::bravo

#endif
