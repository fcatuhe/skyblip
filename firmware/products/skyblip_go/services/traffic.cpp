#include "products/skyblip_go/services/traffic.h"

#include "core/events/rf.h"
#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/timing/slot.h"

namespace skyblip::go {

namespace {
// INFO: fc 20sep26 the branch bus::State::traffic_now() takes is what dates the second it returns
bool utc_dated(const bus::State& state) {
    return (state.clock.pps_locked && state.clock.utc_s != 0) || state.own.utc_valid;
}
}  // namespace

// The one thing this service knows before any frame arrives: which aircraft is
// this one. A ground relay rebroadcasts everything it heard, us included, and
// the table is where that is refused (core/traffic/table.h).
Status TrafficService::setup() {
    context_.state.traffic.set_own_address(settings::kAddrTableSkyblip,
                                           settings::air_address(context_.roles.device_addr));
    uplink_ = has_feature(supported(declared_, context_.roles.capabilities), Feature::UplinkRx);
    return Status::Ok;
}

void TrafficService::tick(uint32_t now_ms) {
    // The reference the table's range gate measures a claimed position against
    // (core/traffic/sanity.h), refreshed before this pass drains a single frame:
    // own-ship has already run this pass, so this is the newest fix there is.
    context_.state.traffic.set_own_reference(context_.state.own);
    events::RfEvent event{};
    while (context_.bus.rf.pop(event)) {
        switch (event.type) {
            case events::RfEventType::RxDone: on_frame(event, now_ms); break;
            case events::RfEventType::CrcError:
                context_.state.air.rx_bad++;
                log(event, stamp_for(event, now_ms), radio::Event::BadCrc);
                break;
            case events::RfEventType::Missed:
                context_.state.air.tx_lost++;
                log(event, stamp_for(event, now_ms), radio::Event::Lost);
                break;
            // The executor's own timestamp, carried alongside the counter it
            // already bumps: RadioService owns the deadline this closes
            // against, and reads it from here rather than a second drain of
            // the same bus.
            case events::RfEventType::TxDone:
                context_.state.air.tx_ok++;
                context_.state.air.last_tx_done_at_us = event.at_us;
                log(event, stamp_for(event, now_ms), radio::Event::Transmitted);
                break;
        }
    }
    const uint32_t now_s = context_.state.traffic_now(now_ms);
    context_.state.traffic.age_out(now_s);
    context_.state.callsigns.age_out(now_s);
}

events::Stamp TrafficService::stamp_for(const events::RfEvent& event, uint32_t now_ms) const {
    const bus::State& state = context_.state;
    return events::stamp_of(event.at_us, state.clock.pps_edge_us, state.clock.pps_locked,
                            state.traffic_now(now_ms));
}

// INFO: fc 16sep26 §C.5: slot 1 reaches kSlot1Wrap past the second its sender keyed in
uint32_t TrafficService::keyed_utc(const events::Stamp& stamp, uint32_t now_s) {
    if (!stamp.phase_valid) return now_s;
    const bool in_slot1_tail = stamp.into_ms < timing::kSlot1Wrap;
    return (in_slot1_tail && stamp.at_s > 0) ? stamp.at_s - 1 : stamp.at_s;
}

void TrafficService::log(const events::RfEvent& event, const events::Stamp& stamp,
                         radio::Event outcome, const model::AircraftObs* obs, int8_t key_offset_s) {
    const bus::State& state = context_.state;
    radio::Entry entry{};
    entry.event = outcome;
    entry.band = event.band;
    entry.channel = event.freq_hz == timing::kMband1Hz ? 1 : 0;
    entry.at_s = stamp.at_s;
    entry.into_ms = stamp.into_ms;
    entry.phase_valid = stamp.phase_valid;
    entry.utc = utc_dated(state);
    entry.airborne = flight::airborne(state.own.flight_state);
    entry.rssi_dbm = event.rssi_dbm;
    entry.rssi_valid = event.rssi_valid;
    if (event.type == events::RfEventType::RxDone) entry.len = event.len;
    if (event.type == events::RfEventType::TxDone || event.type == events::RfEventType::Missed)
        entry.callsign = state.rf.tx_callsign;
    if (outcome == radio::Event::Transmitted && state.rf.tx_deadline_us != 0) {
        entry.tx_keyed_us = radio::tx_span_of(event.keyed_at_us, state.rf.tx_deadline_us);
        entry.tx_span_us = radio::tx_span_of(event.at_us, state.rf.tx_deadline_us);
        entry.tx_span_valid = true;
        context_.state.rf.last_tx_keyed_us = entry.tx_keyed_us;
        context_.state.rf.last_tx_span_us = entry.tx_span_us;
    }
    entry.key_offset_s = key_offset_s;
    if (obs != nullptr) {
        entry.source = obs->source;
        entry.addr = obs->addr;
        entry.addr_valid = obs->source != model::Source::AdslUplink;
    }
    context_.state.radio_log.record(entry);
    context_.diag.record(entry);
}

void TrafficService::on_frame(const events::RfEvent& event, uint32_t now_ms) {
    const events::Stamp stamp = stamp_for(event, now_ms);
    protocol::Frame frame{};
    const protocol::System system =
        protocol::receive_burst(event.band, event.data.data(), event.len, frame);
    if (system == protocol::System::AdslUplink) {
        on_uplink(event, stamp, now_ms);
        return;
    }
    if (system == protocol::System::Unknown) {
        if (protocol::framed_noise(frame)) {
            context_.state.air.rx_noise++;
            return;
        }
        count_refusal(radio::Event::Unframed);
        log(event, stamp, radio::Event::Unframed);
        return;
    }

    const uint32_t utc = context_.state.traffic_now(now_ms);
    const uint32_t keyed = keyed_utc(stamp, utc);
    model::AircraftObs obs{};
    const bool alptas = system == protocol::System::Alptas;
    int8_t key_offset_s = 0;
    const radio::Event outcome =
        alptas ? decode_alptas(frame, keyed, stamp.phase_valid, obs, key_offset_s)
               : decode_adsl(frame, keyed, stamp, obs);
    if (outcome == radio::Event::Named) {
        context_.state.air.rx_named++;
        log(event, stamp, outcome, &obs);
        return;
    }
    if (outcome != radio::Event::Received) {
        count_refusal(outcome);
        const bool named = alptas && names_its_sender(outcome);
        log(event, stamp, outcome, named ? &obs : nullptr, key_offset_s);
        return;
    }

    obs.received.into_ms = stamp.into_ms;
    obs.received.phase_valid = stamp.phase_valid;
    obs.at_ms = now_ms;
    obs.rssi_dbm = event.rssi_dbm;
    context_.state.traffic.update(obs, utc);
    context_.state.air.rx_ok++;
    log(event, stamp, radio::Event::Received, &obs);
}

// One frame from the ground, up to thirteen aircraft in it (§C.4's higher rate
// buys the room the M band has no space for), each one an observation in its
// own right.
// The counters are the uplink's own: a codeword Reed-Solomon refuses is not an
// M-band framing failure, and counting it as one is what let this whole path go
// missing without a single number moving.
void TrafficService::on_uplink(const events::RfEvent& event, const events::Stamp& stamp,
                               uint32_t now_ms) {
    if (!uplink_) return;
    context_.state.air.uplink_frames++;

    model::AircraftObs relayed[protocol::AdslUplink::kMaxTargets];
    protocol::AdslUplink::DecodeStats stats{};
    if (uplink_codec_.decode(event.data.data(), relayed, protocol::AdslUplink::kMaxTargets,
                             stats) != Status::Ok) {
        context_.state.air.uplink_bad++;
        log(event, stamp, radio::Event::BadCrc);
        return;
    }

    // The relay's own reception is older than this second by however long the
    // ground station took to compose the frame, and nothing in it says by how
    // much. Stamping it with the second it arrived in is the only honest
    // reading, and core/traffic/table.h is what stops that recency from
    // outranking a direct reception of the same aircraft.
    model::AircraftObs relay{};
    relay.source = model::Source::AdslUplink;
    log(event, stamp, radio::Event::Received, &relay);

    const uint32_t utc = context_.state.traffic_now(now_ms);
    for (int i = 0; i < stats.targets; i++) {
        model::AircraftObs& obs = relayed[i];
        obs.received = stamp;
        obs.received.at_s = utc;
        obs.at_ms = now_ms;
        obs.rssi_dbm = event.rssi_dbm;
        if (context_.state.traffic.update(obs, utc) >= 0) context_.state.air.uplink_targets++;
    }
}

void TrafficService::count_refusal(radio::Event outcome) {
    switch (outcome) {
        case radio::Event::Unattempted: context_.state.air.rx_wait++; break;
        case radio::Event::Unsupported: context_.state.air.rx_type++; break;
        case radio::Event::Unframed: context_.state.air.rx_unframed++; break;
        case radio::Event::Miskeyed: context_.state.air.rx_miskeyed++; break;
        default: context_.state.air.rx_bad++; break;
    }
}

// INFO: fc 19sep26 the address word is in clear and the frame CRC covers it
bool TrafficService::names_its_sender(radio::Event outcome) {
    return outcome == radio::Event::Unsupported || outcome == radio::Event::Miskeyed ||
           outcome == radio::Event::Undecoded;
}

// The Manchester error map travels with the frame, so the forward correction
// knows which bits the air already told us not to trust.
radio::Event TrafficService::decode_adsl(protocol::Frame& frame, uint32_t utc,
                                         const events::Stamp& stamp, model::AircraftObs& obs) {
    protocol::AdslPacket p{};
    p.init();
    __builtin_memcpy(&p.Version, frame.data, protocol::kAdslFrameBytes);
    if (p.check_crc() != 0 && (p.correct(frame.err) < 0 || p.check_crc() != 0))
        return radio::Event::BadCrc;
    p.descramble();
    if (p.is_registration()) return learn_callsign(p, utc, obs);
    if (!p.is_position()) return radio::Event::Unsupported;
    events::Stamp received = stamp;
    received.at_s = utc;
    if (!protocol::to_obs(p, received, 0, model::Source::AdslDirect, obs))
        return radio::Event::Undecoded;
    return radio::Event::Received;
}

radio::Event TrafficService::learn_callsign(const protocol::AdslPacket& p, uint32_t utc,
                                            model::AircraftObs& obs) {
    char callsign[traffic::CallsignTable::kTextBytes];
    if (protocol::callsign_of(p, callsign, sizeof(callsign)) == 0) return radio::Event::Undecoded;
    obs.addr = p.address();
    obs.addr_table = p.addr_table();
    obs.source = model::Source::AdslDirect;
    context_.state.callsigns.learn(obs.addr_table, obs.addr, callsign, utc);
    return radio::Event::Named;
}

// INFO: fc 16sep26 ALP-TAS keys on the second its sender keyed in, so an undated burst is a guess
radio::Event TrafficService::decode_alptas(protocol::Frame& frame, uint32_t utc, bool dated,
                                           model::AircraftObs& obs, int8_t& key_offset_s) const {
    const model::OwnState& own = context_.state.own;
    if (!own.fix_valid || !own.utc_valid) return radio::Event::Unattempted;
    if (protocol::alptas_correct(frame.data, frame.err) < 0) return radio::Event::BadCrc;
    obs.addr = protocol::alptas_address(frame.data);
    obs.source = model::Source::Alptas;
    const int32_t lat = own.lat_1e7;
    const int32_t lon = own.lon_1e7;
    radio::Event verdict = verdict_of(protocol::alptas_decode(frame.data, utc, lat, lon, obs));
    if (verdict == radio::Event::Undecoded && !dated && utc != 0)
        verdict = verdict_of(protocol::alptas_decode(frame.data, utc - 1, lat, lon, obs));
    if (verdict != radio::Event::Undecoded) return verdict;

    uint32_t keyed = 0;
    if (protocol::alptas_keyed_second(frame.data, utc, keyed) != Status::Ok) return verdict;
    const int32_t offset = static_cast<int32_t>(keyed - utc);
    if (offset == 0) return verdict;
    key_offset_s = static_cast<int8_t>(offset);
    return radio::Event::Miskeyed;
}

radio::Event TrafficService::verdict_of(Status decoded) {
    switch (decoded) {
        case Status::Ok: return radio::Event::Received;
        case Status::Unsupported: return radio::Event::Unsupported;
        default: return radio::Event::Undecoded;
    }
}

}  // namespace skyblip::go
