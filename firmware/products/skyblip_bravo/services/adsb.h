#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_ADSB_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_ADSB_H

#include <array>
#include <cstdint>

#include "core/bus/bus.h"
#include "core/model/aircraft.h"
#include "ports/adsb_receiver.h"
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
    // INFO: fc 26nov26 the cadence of Power and Loop, so an outage lines up with them
    static constexpr uint32_t kRecordPeriodMs = 1000;
    // INFO: fc 26nov26 the radio thread refills the queue mid-drain: a flood held one pass 561 ms
    static constexpr int kFramesPerPassCeiling = events::kAdsbLoadFrames;
    // INFO: fc 27nov26 no transponder sends these downlink formats, so a frame naming one is noise
    static constexpr std::array<uint8_t, 12> kImpossibleFormats = {1, 2,  3,  6,  7,  8,
                                                                   9, 10, 12, 13, 14, 15};

    AdsbService(runtime::Context& context, bus::AdsbQueue& frames, ports::AdsbReceiver& receiver,
                common::TrafficSettings settings, const GateReference& gate)
        : runtime::Service(context),
          frames_(frames),
          receiver_(receiver),
          settings_(settings),
          gate_(gate),
          capture_(context) {}

    void tick(uint32_t now_ms) override;
    // INFO: fc 19nov26 the receiver wakes nothing, so the queue is drained inside its window
    uint32_t due_in_ms(uint32_t now_ms) const override;

    uint32_t crc() const;
    uint32_t unsupported() const;
    uint32_t folded() const { return folded_; }
    uint32_t positions() const { return positions_; }
    std::array<uint32_t, adsb::kDownlinkFormats> formats() const;
    const adsb::Assembly& assembly() const { return assembly_; }
    const common::AdsbCapture& capture() const { return capture_; }
    const OwnTransponder& own_transponder() const { return own_transponder_; }
    const Transponders& transponders() const { return transponders_; }
    static uint32_t impossible_formats(const ports::AdsbReceiverStatistics& statistics);

   private:
    static adsb::Reference reference(const model::Ownship& own);
    void hear(const events::AdsbFrame& frame, const adsb::Reference& here, uint32_t now_ms);
    void refuse(Status status);
    void record_second(const GateChoice& gate, uint32_t now_ms);

    bus::AdsbQueue& frames_;
    ports::AdsbReceiver& receiver_;
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
    uint32_t positions_{0};
    uint32_t recorded_ms_{0};
    bool recorded_{false};
};

}  // namespace skyblip::bravo

#endif
