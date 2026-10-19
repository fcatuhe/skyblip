#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_TRAFFIC_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_TRAFFIC_H

#include "core/events/rf.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "core/radio/log.h"
#include "core/traffic/callsigns.h"
#include "products/skyblip_go/features.h"
#include "runtime/service.h"

namespace skyblip::go {

// Owns state.traffic: radio events become targets. The dwell that heard a burst
// names the system it belongs to (core/protocol/air.h), so nothing here guesses:
// one M-band dwell reports two systems and the frame names itself out of its
// sync tail, while a burst from the O-band dwell is a ground station's uplink
// frame. A frame that fails CRC after forward correction is counted and dropped,
// never shown.
class TrafficService : public runtime::Service {
   public:
    TrafficService(runtime::Context& context, Feature declared)
        : runtime::Service(context), declared_(declared) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;

    bool uplink_enabled() const { return uplink_; }

   private:
    void on_frame(const events::RfEvent& event, uint32_t now_ms);
    void on_uplink(const events::RfEvent& event, const events::Stamp& stamp, uint32_t now_ms);
    void count_refusal(radio::Event outcome);
    events::Stamp stamp_for(const events::RfEvent& event, uint32_t now_ms) const;
    static uint32_t keyed_utc_s(const events::Stamp& stamp, uint32_t now_s);
    void log(const events::RfEvent& event, const events::Stamp& stamp, radio::Event outcome,
             const model::AircraftObservation* observation = nullptr, int8_t key_offset_s = 0);
    radio::Event decode_adsl(protocol::Frame& frame, uint32_t utc_s, const events::Stamp& stamp,
                             model::AircraftObservation& observation);
    radio::Event learn_callsign(const protocol::AdslPacket& p, uint32_t utc_s,
                                model::AircraftObservation& observation);
    radio::Event decode_alptas(protocol::Frame& frame, uint32_t utc_s, bool dated,
                               model::AircraftObservation& observation, int8_t& key_offset_s) const;
    static bool names_its_sender(radio::Event outcome);
    static radio::Event verdict_of(Status decoded);

    protocol::AdslUplink uplink_codec_{};
    const Feature declared_;
    bool uplink_{false};
};

}  // namespace skyblip::go

#endif
