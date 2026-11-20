#include "products/skyblip_go/services/traffic.h"

#include <optional>

#include "core/events/rf.h"
#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/alptas.h"
#include "core/protocol/callsign.h"
#include "core/settings/address.h"
#include "core/timing/slot.h"
#include "protocols/adsl/soft_versioning.h"
#include "protocols/adsl_edge/registration.h"
#include "protocols/alptas/frame.h"

namespace skyblip::go {

namespace {
// INFO: fc 20sep26 the branch bus::State::traffic_now_s() takes is what dates the second it returns
bool utc_dated(const bus::State& state) {
    return (state.clock.pps_locked && state.clock.utc_s != 0) || state.own.utc_valid;
}
}  // namespace

void TrafficService::tick(uint32_t now_ms) {
    // INFO: fc 20nov26 set before any frame drains, from the fix own-ship published this pass
    context_.state.traffic.set_own_reference(context_.state.own);
    // INFO: fc 18oct26 each pass: a ground relay echoes us, and an unlock hands the address mid-run
    context_.state.traffic.set_own_address(settings::kSkyblipAddressMappingTable,
                                           context_.state.activation.address().value_or(0));
    context_.state.traffic.set_aircraft_address(settings_.aircraft_address);
    events::RfEvent event{};
    while (rf_.pop(event)) {
        switch (event.type) {
            case events::RfEventType::RxDone: on_frame(event, now_ms); break;
            case events::RfEventType::CrcError:
                context_.state.air.rx_bad++;
                log(event, stamp_for(event, now_ms), radio::Verdict::BadCrc);
                break;
            case events::RfEventType::Missed:
                context_.state.air.tx_lost++;
                log(event, stamp_for(event, now_ms), radio::Verdict::Lost);
                break;
            case events::RfEventType::TxDone:
                context_.state.rf.note_tx_done(context_.state.air.tx_ok, event.tx_at_us);
                context_.state.air.tx_ok++;
                context_.state.rf.timing_statistics.record_dwell_phase(
                    static_cast<int64_t>(event.keyed_at_us) - static_cast<int64_t>(event.tx_at_us));
                log(event, stamp_for(event, now_ms), radio::Verdict::Transmitted);
                break;
            case events::RfEventType::Busy:
                context_.state.rf.note_busy({event.tx_at_us, event.rssi_dbm, event.rssi_valid});
                break;
        }
    }
    const uint32_t now_s = context_.state.traffic_now_s(now_ms);
    context_.state.traffic.age_out(now_s);
    context_.state.callsigns.age_out(now_s);
}

uint32_t TrafficService::due_in_ms(uint32_t /*now_ms*/) const {
    return runtime::kServiceRestCeilingMs;
}

events::Stamp TrafficService::stamp_for(const events::RfEvent& event, uint32_t now_ms) const {
    const bus::State& state = context_.state;
    return events::stamp_of(event.at_us, state.clock.pps_edge_us, state.clock.pps_locked,
                            state.traffic_now_s(now_ms));
}

// INFO: fc 16sep26 §C.5: the B dwell reaches kWindowBSpillMs past the second its sender keyed in
uint32_t TrafficService::keyed_utc_s(const events::Stamp& stamp, uint32_t now_s) {
    if (!stamp.phase_valid) return now_s;
    const bool in_spill = stamp.into_ms < timing::kWindowBSpillMs;
    return (in_spill && stamp.at_s > 0) ? stamp.at_s - 1 : stamp.at_s;
}

void TrafficService::log(const events::RfEvent& event, const events::Stamp& stamp,
                         radio::Verdict outcome, const model::AircraftObservation* observation,
                         int8_t key_offset_s) {
    const bus::State& state = context_.state;
    radio::Entry entry{};
    entry.verdict = outcome;
    entry.band = event.band;
    entry.channel = event.frequency_hz == adsl::kMBand1Hz ? 1 : 0;
    entry.at_s = stamp.at_s;
    entry.into_ms = stamp.into_ms;
    entry.phase_valid = stamp.phase_valid;
    entry.utc_dated = utc_dated(state);
    entry.airborne = flight::airborne(state.own.flight_state);
    entry.rssi_dbm = event.rssi_dbm;
    entry.rssi_valid = event.rssi_valid;
    if (event.type == events::RfEventType::RxDone) entry.bytes = event.bytes;
    if (event.type == events::RfEventType::TxDone || event.type == events::RfEventType::Missed) {
        entry.callsign = event.tx_at_us != 0 && event.tx_at_us == state.rf.callsign_at_us;
        entry.status = event.tx_at_us != 0 && event.tx_at_us == state.rf.status_at_us;
    }
    if (outcome == radio::Verdict::Transmitted && event.tx_at_us != 0) {
        entry.tx_keyed_us = radio::tx_span_of(event.keyed_at_us, event.tx_at_us);
        entry.tx_span_us = radio::tx_span_of(event.at_us, event.tx_at_us);
        entry.tx_stage_margin_us = radio::tx_stage_margin_of(event.staged_at_us, event.tx_at_us);
        entry.tx_span_valid = true;
        context_.state.rf.last_tx_keyed_us = entry.tx_keyed_us;
        context_.state.rf.last_tx_span_us = entry.tx_span_us;
    }
    entry.key_offset_s = key_offset_s;
    if (observation != nullptr) {
        entry.source = observation->source;
        entry.address = observation->no_track ? 0 : observation->address;
        entry.address_valid =
            observation->source != model::Source::AdslUplink && !observation->no_track;
    }
    context_.state.radio_log.record(entry);
    context_.diagnostics.record(entry);
}

void TrafficService::on_frame(const events::RfEvent& event, uint32_t now_ms) {
    const events::Stamp stamp = stamp_for(event, now_ms);
    protocol::Frame frame{};
    const protocol::System system =
        protocol::receive_burst(event.band, event.data.data(), event.bytes, frame);
    if (system == protocol::System::AdslUplink) {
        on_uplink(event, stamp, now_ms);
        return;
    }
    if (system == protocol::System::Unknown) {
        if (protocol::framed_noise(frame)) {
            context_.state.air.rx_noise++;
            return;
        }
        count_refusal(radio::Verdict::Unframed);
        log(event, stamp, radio::Verdict::Unframed);
        return;
    }

    const uint32_t utc_s = context_.state.traffic_now_s(now_ms);
    const uint32_t keyed = keyed_utc_s(stamp, utc_s);
    model::AircraftObservation observation{};
    const bool alptas = system == protocol::System::Alptas;
    int8_t key_offset_s = 0;
    const radio::Verdict outcome =
        alptas ? decode_alptas(frame, keyed, stamp, observation, key_offset_s)
               : decode_adsl(frame, keyed, stamp, observation);
    if (outcome == radio::Verdict::Named) {
        context_.state.air.rx_named++;
        log(event, stamp, outcome, &observation);
        return;
    }
    if (outcome == radio::Verdict::Declared) {
        context_.state.air.rx_status++;
        log(event, stamp, outcome, &observation);
        return;
    }
    if (outcome != radio::Verdict::Received) {
        count_refusal(outcome);
        const bool named = alptas && names_its_sender(outcome);
        log(event, stamp, outcome, named ? &observation : nullptr, key_offset_s);
        return;
    }

    observation.received.into_ms = stamp.into_ms;
    observation.received.phase_valid = stamp.phase_valid;
    observation.at_ms = now_ms - static_cast<uint32_t>(observation.fix_age_ms);
    observation.rssi_dbm = event.rssi_dbm;
    context_.state.traffic.update(observation, utc_s);
    context_.state.air.rx_ok++;
    log(event, stamp, radio::Verdict::Received, &observation);
}

// INFO: fc 20nov26 the uplink counts apart, so a packet Reed-Solomon refuses is never noise
void TrafficService::on_uplink(const events::RfEvent& event, const events::Stamp& stamp,
                               uint32_t now_ms) {
    context_.state.air.uplink_frames++;

    model::AircraftObservation relayed[protocol::AdslUplink::kMaxTargets];
    protocol::AdslUplink::DecodeStatistics statistics{};
    const uint32_t heard_s = context_.state.traffic_now_s(now_ms);
    const int32_t heard_ms = stamp.phase_valid ? stamp.into_ms : 0;
    const Status decoded =
        uplink_codec_.decode(event.data.data(), event.bytes, heard_s, heard_ms, relayed,
                             protocol::AdslUplink::kMaxTargets, statistics);
    context_.state.air.uplink_stale += static_cast<uint32_t>(statistics.stale);
    if (decoded != Status::Ok) {
        context_.state.air.uplink_bad++;
        log(event, stamp, radio::Verdict::BadCrc);
        return;
    }

    model::AircraftObservation relay{};
    relay.source = model::Source::AdslUplink;
    log(event, stamp, radio::Verdict::Received, &relay);

    // INFO: fc 20nov26 a relay has no age, so it takes its arrival second (core/traffic/table.h)
    const uint32_t utc_s = context_.state.traffic_now_s(now_ms);
    for (int i = 0; i < statistics.targets; i++) {
        model::AircraftObservation& observation = relayed[i];
        observation.received = stamp;
        observation.received.at_s = utc_s;
        observation.at_ms = now_ms;
        observation.rssi_dbm = event.rssi_dbm;
        if (context_.state.traffic.update(observation, utc_s) >= 0)
            context_.state.air.uplink_targets++;
    }
}

void TrafficService::count_refusal(radio::Verdict outcome) {
    switch (outcome) {
        case radio::Verdict::Unattempted: context_.state.air.rx_wait++; break;
        case radio::Verdict::Unsupported: context_.state.air.rx_type++; break;
        case radio::Verdict::Unframed: context_.state.air.rx_unframed++; break;
        case radio::Verdict::Miskeyed: context_.state.air.rx_miskeyed++; break;
        default: context_.state.air.rx_bad++; break;
    }
}

// INFO: fc 19sep26 the address word is in clear and the frame CRC covers it
bool TrafficService::names_its_sender(radio::Verdict outcome) {
    return outcome == radio::Verdict::Unsupported || outcome == radio::Verdict::Miskeyed ||
           outcome == radio::Verdict::Undecoded;
}

radio::Verdict TrafficService::decode_adsl(protocol::Frame& frame, uint32_t utc_s,
                                           const events::Stamp& stamp,
                                           model::AircraftObservation& observation) {
    adsl::Packet packet;
    const wire::Status heard =
        adsl::receive_as_heard(frame.data, frame.erasures, frame.length_heard, packet);
    if (heard == wire::Status::Crc) return radio::Verdict::BadCrc;
    if (heard != wire::Status::Ok) return radio::Verdict::Unsupported;
    if (adsl_edge::is_registration(packet)) return learn_callsign(packet, utc_s, observation);
    if (packet.payload_type == adsl::Packet::kPayloadTypeStatus)
        return learn_status(packet, observation);
    if (!packet.is_position()) return radio::Verdict::Unsupported;
    events::Stamp received = stamp;
    received.at_s = utc_s;
    if (!protocol::to_observation(packet, received, 0, model::Source::AdslDirect, observation))
        return radio::Verdict::Undecoded;
    return radio::Verdict::Received;
}

// INFO: fc 13nov26 B.5: a Status from an untracked aircraft is dropped, and its next one counts
radio::Verdict TrafficService::learn_status(const adsl::Packet& packet,
                                            model::AircraftObservation& observation) {
    observation.address = packet.address();
    observation.address_mapping_table = packet.address_mapping_table();
    observation.source = model::Source::AdslDirect;
    const std::optional<uint8_t> declared = adsl::status_max_version(packet);
    if (!declared || !context_.state.traffic.declare_max_version(observation.address_mapping_table,
                                                                 observation.address, *declared))
        return radio::Verdict::Unsupported;
    return radio::Verdict::Declared;
}

radio::Verdict TrafficService::learn_callsign(const adsl::Packet& packet, uint32_t utc_s,
                                              model::AircraftObservation& observation) {
    char callsign[traffic::CallsignTable::kTextBytes];
    if (protocol::callsign_of(packet, callsign, sizeof(callsign)) == 0)
        return radio::Verdict::Undecoded;
    observation.address = packet.address();
    observation.address_mapping_table = packet.address_mapping_table();
    observation.source = model::Source::AdslDirect;
    context_.state.callsigns.learn(observation.address_mapping_table, observation.address, callsign,
                                   utc_s);
    return radio::Verdict::Named;
}

// INFO: fc 16sep26 ALP-TAS keys on the second its sender keyed in, so an undated burst is a guess
radio::Verdict TrafficService::decode_alptas(protocol::Frame& frame, uint32_t utc_s,
                                             const events::Stamp& stamp,
                                             model::AircraftObservation& observation,
                                             int8_t& key_offset_s) {
    const model::Ownship& own = context_.state.own;
    if (!own.fix_valid || !own.utc_valid) return radio::Verdict::Unattempted;
    if (alptas::receive(frame.data, frame.erasures) != wire::Status::Ok)
        return radio::Verdict::BadCrc;
    observation.address = alptas::address(frame.data);
    observation.no_track = alptas::no_track(frame.data);
    observation.source = model::Source::Alptas;
    const int32_t latitude = own.latitude_1e7;
    const int32_t longitude = own.longitude_1e7;
    radio::Verdict verdict = verdict_of(
        alptas_receiver_.decode(frame.data, utc_s, latitude, longitude, stamp, observation));
    if (verdict == radio::Verdict::Undecoded && !stamp.phase_valid && utc_s != 0)
        verdict = verdict_of(alptas_receiver_.decode(frame.data, utc_s - 1, latitude, longitude,
                                                     stamp, observation));
    if (verdict != radio::Verdict::Undecoded) return verdict;

    uint32_t keyed = 0;
    if (alptas::keyed_second(frame.data, utc_s, keyed) != wire::Status::Ok) return verdict;
    const int32_t offset = static_cast<int32_t>(keyed - utc_s);
    if (offset == 0) return verdict;
    key_offset_s = static_cast<int8_t>(offset);
    return radio::Verdict::Miskeyed;
}

radio::Verdict TrafficService::verdict_of(Status decoded) {
    switch (decoded) {
        case Status::Ok: return radio::Verdict::Received;
        case Status::Unsupported: return radio::Verdict::Unsupported;
        default: return radio::Verdict::Undecoded;
    }
}

}  // namespace skyblip::go
