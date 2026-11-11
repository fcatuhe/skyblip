#include "products/skyblip_bravo/services/adsb.h"

#include "core/events/stamp.h"
#include "core/protocol/adsb.h"

namespace skyblip::bravo {

void AdsbService::tick(uint32_t now_ms) {
    bus::State& state = context_.state;
    state.traffic.set_own_reference(state.own);
    state.traffic.set_own_icao(settings_.own_icao);
    assembly_.set_own_address(settings_.own_icao);
    const protocol::AdsbReference here = reference();
    events::AdsbFrame frame{};
    while (frames_.pop(frame)) hear(frame, here, now_ms);
    // INFO: fc 12nov26 every pass, so no time the cache holds outlives its rule across the clock's
    // wrap
    assembly_.age_out(now_ms);
    const uint32_t now_s = state.traffic_now_s(now_ms);
    state.traffic.age_out(now_s);
    state.callsigns.age_out(now_s);
}

protocol::AdsbReference AdsbService::reference() const {
    protocol::AdsbReference here{};
    const model::Ownship& own = context_.state.own;
    if (!own.fix_valid) return here;
    here.position = {own.latitude_1e7, own.longitude_1e7};
    here.valid = true;
    return here;
}

void AdsbService::hear(const events::AdsbFrame& frame, const protocol::AdsbReference& here,
                       uint32_t now_ms) {
    protocol::AdsbMessage message{};
    const protocol::AdsbReception reception{frame.at_us, frame.rssi_dbm, frame.rssi_valid};
    if (protocol::adsb_receive(frame.chips.data(), frame.chips.size(), reception, message) !=
        Status::Ok) {
        invalid_++;
        return;
    }
    const bus::State& state = context_.state;
    const uint32_t now_s = state.traffic_now_s(now_ms);
    const events::Stamp received =
        events::stamp_of(frame.at_us, state.clock.pps_edge_us, state.clock.pps_locked, now_s);
    const uint32_t at_ms = static_cast<uint32_t>(frame.at_us / 1000);
    if (!assembly_.fold(message, at_ms, received, here, observation_)) return;
    folded_++;
    (void)capture_.record(observation_, now_ms);
    if (!observation_.position_valid) return;
    context_.state.traffic.update(observation_, now_s);
}

}  // namespace skyblip::bravo
