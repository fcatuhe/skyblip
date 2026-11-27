#include "products/skyblip_bravo/services/adsb.h"

#include <algorithm>
#include <iterator>

#include "core/events/stamp.h"
#include "core/protocol/adsb_observation.h"
#include "core/settings/address.h"
#include "products/skyblip_bravo/flash_window.h"
#include "protocols/adsb/message.h"

namespace skyblip::bravo {

void AdsbService::tick(uint32_t now_ms) {
    bus::State& state = context_.state;
    // INFO: fc 11nov26 placed against the Go's position while paired, so exports are around the Go
    const GateChoice gate = gate_.choose(state.own, now_ms);
    const uint32_t aircraft_address = gate_.aircraft_address(settings_.aircraft_address);
    state.traffic.set_own_reference(gate.own);
    state.traffic.set_aircraft_address(aircraft_address);
    assembly_.set_own_address(aircraft_address);
    const adsb::Reference here = reference(gate.own);
    events::AdsbFrame frame{};
    for (int heard = 0; heard < kFramesPerPassCeiling && frames_.pop(frame); heard++)
        hear(frame, here, now_ms);
    own_transponder_.tick(now_ms);
    transponders_.age_out(now_ms);
    // INFO: fc 15nov26 on the ground only, as the Go proposes it: aloft a wingman is as loud
    state.own_transponder = state.flight.confirmed_state == flight::FlightState::Ground
                                ? own_transponder_.heard()
                                : std::nullopt;
    // INFO: fc 12nov26 every pass, so no time the cache holds outlives its rule across the clock's
    // wrap
    assembly_.age_out(now_ms);
    const uint32_t now_s = state.traffic_now_s(now_ms);
    state.traffic.age_out(now_s);
    state.callsigns.age_out(now_s);
    record_second(gate, now_ms);
}

// INFO: fc 27nov26 the receiver thread screens out what the decoder refuses, so both are counted
uint32_t AdsbService::crc() const { return crc_ + receiver_.statistics().parity_refused; }

uint32_t AdsbService::unsupported() const {
    return unsupported_ + receiver_.statistics().format_refused;
}

std::array<uint32_t, adsb::kDownlinkFormats> AdsbService::formats() const {
    const ports::AdsbReceiverStatistics statistics = receiver_.statistics();
    std::array<uint32_t, adsb::kDownlinkFormats> formats{};
    std::copy(std::begin(statistics.formats), std::end(statistics.formats), formats.begin());
    return formats;
}

uint32_t AdsbService::impossible_formats(const ports::AdsbReceiverStatistics& statistics) {
    uint32_t impossible = 0;
    for (const uint8_t format : kImpossibleFormats) impossible += statistics.formats[format];
    return impossible;
}

// INFO: fc 26nov26 four records every second whatever the sky does, so silence still records
void AdsbService::record_second(const GateChoice& gate, uint32_t now_ms) {
    if (!context_.diagnostics.armed()) {
        recorded_ = false;
        return;
    }
    if (recorded_ && now_ms - recorded_ms_ < kRecordPeriodMs) return;
    recorded_ = true;
    recorded_ms_ = now_ms;
    const diagnostics::Instant at = context_.instant(now_ms);
    const ports::AdsbReceiverStatistics statistics = receiver_.statistics();
    context_.diagnostics.record(diagnostics::receiver_of(statistics, receiver_.listening()), at);
    context_.diagnostics.record(diagnostics::framing_of(statistics), at);
    context_.diagnostics.record(diagnostics::screening_of(statistics, transponders_.unmatched()),
                                at);
    diagnostics::Reception reception{};
    reception.crc = crc_ + statistics.parity_refused;
    reception.unsupported = unsupported_ + statistics.format_refused;
    reception.folded = folded_;
    reception.positions = positions_;
    reception.capture_refused = capture_.refused();
    reception.aircraft = static_cast<uint32_t>(assembly_.count());
    reception.tracked = static_cast<uint32_t>(context_.state.traffic.count());
    reception.reference = gate.reference;
    reception.fix_valid = context_.state.own.fix_valid;
    reception.framing_resyncs = statistics.framing_resyncs;
    reception.impossible_formats = impossible_formats(statistics);
    context_.diagnostics.record(reception, at);
}

uint32_t AdsbService::due_in_ms(uint32_t /*now_ms*/) const {
    if (!frames_.empty()) return 0;
    return events::kAdsbQueueDrainWindowMs - kFlashPassCeilingMs;
}

adsb::Reference AdsbService::reference(const model::Ownship& own) {
    adsb::Reference here{};
    if (!own.fix_valid) return here;
    here.position = {own.latitude_1e7, own.longitude_1e7};
    here.valid = true;
    return here;
}

void AdsbService::hear(const events::AdsbFrame& frame, const adsb::Reference& here,
                       uint32_t now_ms) {
    adsb::Message message{};
    const adsb::Reception reception{frame.at_us, frame.rssi_dbm, frame.rssi_valid};
    const Status status =
        status_of(adsb::receive(frame.bytes.data(), frame.bytes.size(), reception, message));
    if (status != Status::Ok) {
        refuse(status);
        return;
    }
    if (!transponders_.hear(message, now_ms)) return;
    // INFO: fc 13nov26 before the fold drops the aircraft address, so a set address still reads as
    // heard
    own_transponder_.hear(message, now_ms);
    if (message.kind == adsb::Kind::AllCall || message.kind == adsb::Kind::Reply) return;
    const bus::State& state = context_.state;
    const uint32_t now_s = state.traffic_now_s(now_ms);
    const events::Stamp received =
        events::stamp_of(frame.at_us, state.clock.pps_edge_us, state.clock.pps_locked, now_s);
    const uint32_t at_ms = static_cast<uint32_t>(frame.at_us / 1000);
    if (!protocol::adsb_fold(assembly_, message, at_ms, received, here, observation_)) return;
    folded_++;
    if (message.kind == adsb::Kind::Identification && message.identification.callsign_valid)
        context_.state.callsigns.learn(adsl::kAddressTableIcao, message.address,
                                       message.identification.callsign, now_s);
    (void)capture_.record(observation_, now_ms);
    if (!observation_.position_valid) return;
    positions_++;
    context_.state.traffic.update(observation_, now_s);
}

// INFO: fc 12nov26 a whole 14-byte frame is never Invalid, so any refusal but Unsupported is parity
void AdsbService::refuse(Status status) {
    if (status == Status::Unsupported) {
        unsupported_++;
    } else {
        crc_++;
    }
}

}  // namespace skyblip::bravo
