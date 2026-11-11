#include "products/skyblip_go/services/bravo.h"

namespace skyblip::go {

namespace {
uint32_t seconds_later_ms(uint32_t edge_ms, uint32_t at_s, uint32_t edge_s) {
    const int64_t later_ms = static_cast<int64_t>(static_cast<int32_t>(at_s - edge_s)) * 1000;
    return edge_ms + static_cast<uint32_t>(later_ms);
}
}  // namespace

// INFO: fc 12nov26 forwarding is never evidence: nothing here reads when the frame arrived
void BravoService::fold(const comms::PeerObservation& peer, uint32_t now_ms) {
    if (!paired(settings_) || link_ != BravoLink::Up) return;
    const std::optional<uint32_t> edge_ms = second_edge_ms(peer.at.at_s);
    if (!edge_ms) {
        unplaced_++;
        return;
    }
    model::AircraftObservation observation = peer.observation;
    observation.received = peer.at;
    // INFO: fc 12nov26 an instant known to the second only is its start, so a target only ever ages
    observation.at_ms = *edge_ms + (peer.at.phase_valid ? peer.at.into_ms : 0u);
    if (static_cast<int32_t>(observation.at_ms - now_ms) > static_cast<int32_t>(kAheadCeilingMs)) {
        unplaced_++;
        return;
    }

    adsb_capture_.record(observation, now_ms);
    if (peer.callsign[0] != 0)
        context_.state.callsigns.learn(observation.address_mapping_table, observation.address,
                                       peer.callsign,
                                       peer.callsign_dated ? peer.callsign_heard_s : peer.at.at_s);
    context_.state.traffic.update(observation, context_.state.traffic_now_s(now_ms));
}

// INFO: fc 12nov26 the PPS edge a UTC second was anchored to, else the fix that named it
std::optional<uint32_t> BravoService::second_edge_ms(uint32_t at_s) const {
    const timing::ClockState& clock = context_.state.clock;
    const model::Ownship& own = context_.state.own;
    if (clock.pps_locked && clock.utc_s != 0)
        return seconds_later_ms(static_cast<uint32_t>(clock.utc_edge_us / 1000), at_s, clock.utc_s);
    if (own.utc_valid) return seconds_later_ms(own.fix_at_ms, at_s, own.utc_s);
    return std::nullopt;
}

}  // namespace skyblip::go
