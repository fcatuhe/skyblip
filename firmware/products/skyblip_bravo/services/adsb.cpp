#include "products/skyblip_bravo/services/adsb.h"

#include "core/events/stamp.h"
#include "core/protocol/adsb.h"

namespace skyblip::bravo {

void AdsbService::tick(uint32_t now_ms) {
    bus::State& state = context_.state;
    // INFO: fc 11nov26 gated on the Go's position while paired, so its exports are where the Go is
    const GateChoice gate = gate_.choose(state.own, now_ms);
    const uint32_t own_icao = gate_.own_icao(settings_.own_icao);
    state.traffic.set_own_reference(gate.own);
    state.traffic.set_own_icao(own_icao);
    assembly_.set_own_address(own_icao);
    const protocol::AdsbReference here = reference(gate.own);
    events::AdsbFrame frame{};
    while (frames_.pop(frame)) hear(frame, here, now_ms);
    // INFO: fc 12nov26 every pass, so no time the cache holds outlives its rule across the clock's
    // wrap
    assembly_.age_out(now_ms);
    const uint32_t now_s = state.traffic_now_s(now_ms);
    state.traffic.age_out(now_s);
    state.callsigns.age_out(now_s);
}

bool AdsbService::take_reject(AdsbReject& out) {
    const Result<AdsbReject> taken = rejects_.pop();
    if (!taken.ok()) return false;
    out = taken.value();
    return true;
}

protocol::AdsbReference AdsbService::reference(const model::Ownship& own) {
    protocol::AdsbReference here{};
    if (!own.fix_valid) return here;
    here.position = {own.latitude_1e7, own.longitude_1e7};
    here.valid = true;
    return here;
}

void AdsbService::hear(const events::AdsbFrame& frame, const protocol::AdsbReference& here,
                       uint32_t now_ms) {
    protocol::AdsbMessage message{};
    const protocol::AdsbReception reception{frame.at_us, frame.rssi_dbm, frame.rssi_valid};
    const Status status =
        protocol::adsb_receive(frame.chips.data(), frame.chips.size(), reception, message);
    if (status != Status::Ok) {
        refuse(frame, status);
        return;
    }
    const bus::State& state = context_.state;
    const uint32_t now_s = state.traffic_now_s(now_ms);
    const events::Stamp received =
        events::stamp_of(frame.at_us, state.clock.pps_edge_us, state.clock.pps_locked, now_s);
    const uint32_t at_ms = static_cast<uint32_t>(frame.at_us / 1000);
    if (!assembly_.fold(message, at_ms, received, here, observation_)) return;
    folded_++;
    if (message.kind == protocol::AdsbKind::Identification && message.identification.callsign_valid)
        context_.state.callsigns.learn(protocol::kIcaoAddressMappingTable, message.address,
                                       message.identification.callsign, now_s);
    (void)capture_.record(observation_, now_ms);
    if (!observation_.position_valid) return;
    context_.state.traffic.update(observation_, now_s);
}

// INFO: fc 12nov26 a whole 27-byte frame is never Invalid, so any refusal but Unsupported is parity
void AdsbService::refuse(const events::AdsbFrame& frame, Status status) {
    if (status == Status::Unsupported) {
        unsupported_++;
    } else {
        crc_++;
    }
    // INFO: fc 12nov26 a reject past kRejectsKept waiting for the console is counted and not kept
    (void)rejects_.push(AdsbReject{status, frame.rssi_dbm, frame.rssi_valid, frame.chips});
}

}  // namespace skyblip::bravo
