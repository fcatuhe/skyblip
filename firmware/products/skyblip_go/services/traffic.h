#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_TRAFFIC_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_TRAFFIC_H

#include <cstdint>

#include "core/events/rf.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "core/protocol/alptas.h"
#include "core/radio/log.h"
#include "core/traffic/callsigns.h"
#include "core/traffic/versions.h"
#include "products/common/traffic_settings.h"
#include "runtime/service.h"

namespace skyblip::go {

// INFO: fc 20nov26 the dwell and the sync tail name the system of a burst (core/protocol/air.h)
class TrafficService : public runtime::Service {
   public:
    TrafficService(runtime::Context& context, bus::RfQueue& rf, common::TrafficSettings settings,
                   traffic::VersionTable& versions)
        : runtime::Service(context), rf_(rf), settings_(settings), versions_(versions) {}

    void tick(uint32_t now_ms) override;
    // INFO: fc 19nov26 every frame wakes the loop, and the table ages by the second
    uint32_t due_in_ms(uint32_t now_ms) const override;

   private:
    void on_frame(const events::RfEvent& event, uint32_t now_ms);
    void on_uplink(const events::RfEvent& event, const events::Stamp& stamp, uint32_t now_ms);
    void count_refusal(radio::Verdict outcome);
    events::Stamp stamp_for(const events::RfEvent& event, uint32_t now_ms) const;
    static uint32_t keyed_utc_s(const events::Stamp& stamp, uint32_t now_s);
    void log(const events::RfEvent& event, const events::Stamp& stamp, radio::Verdict outcome,
             const model::AircraftObservation* observation = nullptr, int8_t key_offset_s = 0);
    radio::Verdict decode_adsl(protocol::Frame& frame, uint32_t utc_s, const events::Stamp& stamp,
                               model::AircraftObservation& observation, bool& relayed);
    radio::Verdict learn_status(const adsl::Packet& packet, uint32_t utc_s,
                                model::AircraftObservation& observation);
    radio::Verdict learn_callsign(const adsl::Packet& packet, uint32_t utc_s,
                                  model::AircraftObservation& observation);
    radio::Verdict decode_alptas(protocol::Frame& frame, uint32_t utc_s, const events::Stamp& stamp,
                                 model::AircraftObservation& observation, int8_t& key_offset_s);
    static bool names_its_sender(radio::Verdict outcome);
    static radio::Verdict verdict_of(Status decoded);

    protocol::AdslUplink uplink_codec_{};
    protocol::AlptasReceiver alptas_receiver_{};
    bus::RfQueue& rf_;
    common::TrafficSettings settings_;
    traffic::VersionTable& versions_;
};

}  // namespace skyblip::go

#endif
