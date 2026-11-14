#ifndef SKYBLIP_PRODUCTS_COMMON_SERVICES_TRAFFIC_H
#define SKYBLIP_PRODUCTS_COMMON_SERVICES_TRAFFIC_H

#include <cstdint>

#include "core/events/rf.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "core/radio/log.h"
#include "core/traffic/callsigns.h"
#include "products/common/features.h"
#include "runtime/service.h"

namespace skyblip::common {

struct TrafficSettings {
    TrafficSettings(const uint32_t& own_icao_of) : own_icao(own_icao_of) {}
    TrafficSettings(uint32_t&&) = delete;
    TrafficSettings(const uint32_t&&) = delete;

    const uint32_t& own_icao;
};

// Owns state.traffic: radio events become targets. The dwell that heard a burst
// names the system it belongs to (core/protocol/air.h), so nothing here guesses:
// one M-band dwell reports two systems and the frame names itself out of its
// sync tail, while a burst from the O-band dwell is a ground station's uplink
// frame. A frame that fails CRC after forward correction is counted and dropped,
// never shown.
class TrafficService : public runtime::Service {
   public:
    TrafficService(runtime::Context& context, Feature declared, TrafficSettings settings)
        : runtime::Service(context), declared_(declared), settings_(settings) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;

    bool uplink_enabled() const { return uplink_enabled_; }

   private:
    void on_frame(const events::RfEvent& event, uint32_t now_ms);
    void on_uplink(const events::RfEvent& event, const events::Stamp& stamp, uint32_t now_ms);
    void count_refusal(radio::Verdict outcome);
    events::Stamp stamp_for(const events::RfEvent& event, uint32_t now_ms) const;
    static uint32_t keyed_utc_s(const events::Stamp& stamp, uint32_t now_s);
    void log(const events::RfEvent& event, const events::Stamp& stamp, radio::Verdict outcome,
             const model::AircraftObservation* observation = nullptr, int8_t key_offset_s = 0);
    radio::Verdict decode_adsl(protocol::Frame& frame, uint32_t utc_s, const events::Stamp& stamp,
                               model::AircraftObservation& observation);
    radio::Verdict learn_status(const adsl::Packet& packet,
                                model::AircraftObservation& observation);
    radio::Verdict learn_callsign(const adsl::Packet& packet, uint32_t utc_s,
                                  model::AircraftObservation& observation);
    radio::Verdict decode_alptas(protocol::Frame& frame, uint32_t utc_s, bool dated,
                                 model::AircraftObservation& observation,
                                 int8_t& key_offset_s) const;
    static bool names_its_sender(radio::Verdict outcome);
    static radio::Verdict verdict_of(Status decoded);

    protocol::AdslUplink uplink_codec_{};
    const Feature declared_;
    TrafficSettings settings_;
    bool uplink_enabled_{false};
};

}  // namespace skyblip::common

#endif
