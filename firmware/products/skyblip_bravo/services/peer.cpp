#include "products/skyblip_bravo/services/peer.h"

#include <algorithm>

#include "core/dfu/update.h"

namespace skyblip::bravo {

namespace {

bool same_pin(const PeerPin& a, const PeerPin& b) {
    return a.pinned == b.pinned && (!a.pinned || address_of(a) == address_of(b));
}

ConstByteSpan span_of(const events::RxFrame& frame) {
    return ConstByteSpan(frame.data.data(), frame.bytes);
}

}  // namespace

Status PeerService::setup() {
    config_.serve_pairing(common::PairingSide::Bravo);
    const std::optional<uint32_t> epoch = comms::draw_boot_epoch(context_.roles.random);
    if (!epoch) return Status::Down;
    boot_epoch_ = *epoch;
    keep_pin();
    return Status::Ok;
}

void PeerService::tick(uint32_t now_ms) {
    forget_gone();
    if (const std::optional<uint16_t> tag = config_.take_pair_request())
        acceptance_ = Acceptance{*tag, now_ms};
    events::RxFrame frame{};
    while (context_.bus.peer_rx.pop(frame)) take(frame, now_ms);
    pairing_.tick(now_ms);
    accept(now_ms);
    keep_pin();
    config_.set_paired_tag(settings_.peer.pinned ? std::optional<uint16_t>(events::bluetooth_tag(
                                                       address_of(settings_.peer)))
                                                 : std::nullopt);
    expire_call(now_ms);
    context_.state.link.pairing_standing = pairing_.requested();
    context_.state.followed_call =
        following_ ? std::optional<annunciation::Call>(following_->call) : std::nullopt;
    // INFO: fc 17nov26 a press on Bravo reaches its Go at once, not on the next health period
    if (alarm_.dismissed_announcement() != dismissed_told_) health_owed_ = true;

    const GateChoice choice = gate_.choose(context_.state.own, now_ms);
    const bool utc_valid = context_.instant(now_ms).utc_dated;
    send_health(choice, utc_valid, now_ms);
    send_observations(choice, utc_valid, now_ms);
}

uint32_t PeerService::due_in_ms(uint32_t now_ms) const {
    const bool connected =
        std::any_of(gos_.begin(), gos_.end(), [](const Go& go) { return go.used; });
    if (!connected) return runtime::kServiceRestCeilingMs;
    if (health_owed_) return runtime::kServicePassCeilingMs;
    return std::min(runtime::remaining_ms(now_ms - health_sent_ms_, kHealthPeriodMs),
                    ObservationPacer::kTargetUpdateFloorMs);
}

bool PeerService::answer(uint32_t now_ms) {
    if (!pairing_.answer(now_ms)) return false;
    health_owed_ = true;
    context_.state.link.pairing_standing = false;
    return true;
}

void PeerService::accept(uint32_t now_ms) {
    if (!acceptance_) return;
    const bool expired = now_ms - acceptance_->since_ms >= kAcceptanceWindowMs;
    if (expired || (pairing_.requested_by(acceptance_->tag) && answer(now_ms))) acceptance_.reset();
}

void PeerService::forget_gone() {
    for (Go& go : gos_) {
        events::BluetoothAddress live{};
        if (!go.used || context_.roles.link.central_of(go.peer, live)) continue;
        if (pairing_.pinned_to(go.address)) gate_.lose_link();
        if (follows(go.address)) following_.reset();
        if (go.asked) pairing_.withdraw(go.address);
        go = Go{};
        pacer_.forget(static_cast<int>(&go - gos_.data()));
    }
}

// INFO: fc 11nov26 a frame is read whole before its sender counts as a Go, so a refused one is none
void PeerService::take(const events::RxFrame& frame, uint32_t now_ms) {
    const Result<comms::PeerClaim> claim = comms::read_peer_claim(span_of(frame));
    if (!claim.ok()) {
        refused_++;
        return;
    }
    bool new_connection = false;
    Go* go = go_of(frame, new_connection);
    if (go == nullptr) return;
    const PeerPin before = settings_.peer;
    const Heard heard = pairing_.hear(go->address, claim.value().paired, new_connection, now_ms);
    const bool changed = !same_pin(before, settings_.peer);
    if (changed) {
        gate_.unpair();
        config_.note_settings_changed();
    }
    switch (heard) {
        case Heard::Pinned:
        case Heard::Committed:
            gate_.hear(claim.value(), context_.instant(now_ms), now_ms);
            adopt_aircraft_address(claim.value().aircraft_address);
            follow(go->address, claim.value(), now_ms);
            break;
        // INFO: fc 11nov26 a Go that unpaired is answered at once, and may be asking to pair again
        case Heard::Unpinned:
            health_owed_ = true;
            ask(*go, now_ms);
            break;
        case Heard::Asking: ask(*go, now_ms); break;
        case Heard::Stranger:
            go->claimed_pin = true;
            health_owed_ = true;
            break;
        case Heard::Awaited: break;
    }
}

// INFO: fc 15nov26 a press must not hand Bravo back to a replaced Go while the new one is here
void PeerService::ask(Go& go, uint32_t now_ms) {
    if (go.asked || (go.claimed_pin && pinned_go_connected())) return;
    if (pairing_.ask(go.address, now_ms)) go.asked = true;
}

bool PeerService::pinned_go_connected() const {
    return std::any_of(gos_.begin(), gos_.end(),
                       [&](const Go& go) { return go.used && pairing_.pinned_to(go.address); });
}

// INFO: fc 13nov26 a Go with none clears only what a Go gave, never what a phone set on Bravo
void PeerService::adopt_aircraft_address(uint32_t aircraft_address) {
    const bool cleared = aircraft_address == kAircraftAddressUnset;
    if (cleared && !settings_.aircraft_address_from_go) return;
    if (aircraft_address == settings_.aircraft_address &&
        settings_.aircraft_address_from_go == !cleared)
        return;
    settings_.aircraft_address = aircraft_address;
    settings_.aircraft_address_from_go = !cleared;
    config_.note_settings_changed();
    health_owed_ = true;
}

// INFO: fc 12nov26 only the pinned Go's claims reach here, so a stranger never sets Bravo's alarm
void PeerService::follow(const events::BluetoothAddress& go, const comms::PeerClaim& claim,
                         uint32_t now_ms) {
    following_ = Following{go, claim.call, now_ms};
    const Alarm alarm = static_cast<Alarm>(claim.alarm);
    if (settings_.go_alarm == alarm) return;
    settings_.go_alarm = alarm;
    config_.note_settings_changed();
}

void PeerService::expire_call(uint32_t now_ms) {
    if (following_ &&
        (!pairing_.pinned_to(following_->go) || now_ms - following_->heard_ms >= kCallMaxAgeMs))
        following_.reset();
}

bool PeerService::follows(const events::BluetoothAddress& go) const {
    return following_ && following_->go == go;
}

// INFO: fc 11nov26 a Go is known from its first claim on a connection, by the address it came from
PeerService::Go* PeerService::go_of(const events::RxFrame& frame, bool& new_connection) {
    const events::Peer peer = events::peer_of(frame);
    for (Go& go : gos_)
        if (go.used && go.peer == peer) return &go;
    events::BluetoothAddress address{};
    if (!context_.roles.link.central_of(peer, address)) return nullptr;
    for (Go& go : gos_) {
        if (go.used) continue;
        go = Go{peer, address, false, true};
        new_connection = true;
        pacer_.forget(static_cast<int>(&go - gos_.data()));
        health_owed_ = true;
        return &go;
    }
    return nullptr;
}

// INFO: fc 11nov26 a Full reservation is tried again each pass, until a phone leaves the Go a slot
void PeerService::keep_pin() {
    const PeerPin& pin = settings_.peer;
    if (same_pin(pin, kept_)) return;
    const Status reserved = pin.pinned
                                ? context_.roles.link.reserve_peer_connection_for(address_of(pin))
                                : context_.roles.link.reserve_peer_connection(false);
    if (reserved == Status::Full) return;
    kept_ = pin;
}

// INFO: fc 15nov26 Bravo has no glass, so its Go shows what it runs, holds and hears
void PeerService::describe_self(comms::PeerHealth& health) const {
    ports::ImageVersion running{};
    if (context_.roles.dfu.running_version(running))
        dfu::format_version(running, health.firmware, sizeof(health.firmware));
    const power::BatteryState& battery = context_.state.power.battery;
    if (battery.valid) health.battery_percent = battery.percent;
    health.charging = battery.charging;
    health.power_level = context_.state.power.level;
    if (health.listening) health.frame_rate_hz = receiver_.statistics().frame_rate_hz;
    health.gnss_stage = context_.state.gnss.stage;
    if (context_.state.gnss.stage == gnss::Stage::Fixed)
        health.satellites = context_.state.own.satellites;
}

// INFO: fc 11nov26 every Go hears whether Bravo holds its pin, so a Go holding one alone drops it
void PeerService::send_health(const GateChoice& choice, bool utc_valid, uint32_t now_ms) {
    if (!health_owed_ && now_ms - health_sent_ms_ < kHealthPeriodMs) return;
    comms::PeerHealth health{};
    health.boot_epoch = boot_epoch_;
    health.aircraft_address_set = settings_.aircraft_address != kAircraftAddressUnset;
    health.listening = receiver_.listening();
    health.utc_valid = utc_valid;
    health.last_sequence_valid = observed_;
    health.last_sequence = sequence_ - 1;
    health.export_reference = choice.reference;
    health.own_transponder = own_transponder_.heard();
    describe_self(health);
    dismissed_told_ = alarm_.dismissed_announcement();
    bool blocked = false;
    for (const Go& go : gos_) {
        if (!go.used) continue;
        health.paired = pairing_.holds(go.address);
        health.dismissed_announcement =
            pairing_.pinned_to(go.address) ? dismissed_told_ : std::nullopt;
        uint8_t frame[comms::kPeerPayloadFloorBytes];
        const int bytes = comms::write_health(health, frame, sizeof(frame));
        const Status sent = context_.roles.link.send_to(
            go.peer, events::Endpoint::Peer, ConstByteSpan(frame, static_cast<size_t>(bytes)));
        if (sent == Status::WouldBlock) blocked = true;
    }
    health_owed_ = blocked;
    health_sent_ms_ = now_ms;
}

// INFO: fc 11nov26 schemas/link.v1.json: nothing is exported without Bravo's UTC and a reference
void PeerService::send_observations(const GateChoice& choice, bool utc_valid, uint32_t now_ms) {
    if (!utc_valid || choice.reference == comms::ExportReference::None) return;
    std::array<bool, ObservationPacer::kRecipients> blocked{};
    for (int step = 0; step < traffic::TrafficTable::kMaxTargets; step++) {
        const int slot = (next_target_ + step) % traffic::TrafficTable::kMaxTargets;
        const traffic::Target& target = *context_.state.traffic.at(slot);
        if (!target.used) continue;
        serve(slot, target, blocked, now_ms);
        const bool anyone = std::any_of(gos_.begin(), gos_.end(), [&](const Go& go) {
            return go.used && !blocked[static_cast<size_t>(&go - gos_.data())];
        });
        if (!anyone) {
            next_target_ = slot;
            return;
        }
    }
}

// INFO: fc 11nov26 one frame for the Gos owed it, and a Go that blocks keeps its debt
void PeerService::serve(int slot, const traffic::Target& target,
                        std::array<bool, ObservationPacer::kRecipients>& blocked, uint32_t now_ms) {
    const model::AircraftObservation& observation = target.observation;
    const traffic::CallsignTable& callsigns = context_.state.callsigns;
    const uint32_t heard_s =
        callsigns.heard_s(observation.address_mapping_table, observation.address);
    comms::ObservationFrame header{};
    header.boot_epoch = boot_epoch_;
    header.sequence = sequence_;
    header.callsign = callsigns.find(observation.address_mapping_table, observation.address);
    // INFO: fc 11nov26 exports wait for UTC, and the callsign table forgets names dated before it
    header.callsign_heard_valid = true;
    header.callsign_heard_s = heard_s;
    uint8_t frame[comms::kPeerPayloadFloorBytes];
    int bytes = 0;
    bool delivered = false;
    for (int seat = 0; seat < ObservationPacer::kRecipients; seat++) {
        const Go& go = gos_[static_cast<size_t>(seat)];
        if (!go.used || blocked[static_cast<size_t>(seat)]) continue;
        if (!pacer_.owed(seat, slot, target, heard_s, now_ms)) continue;
        if (bytes == 0) bytes = comms::write_observation(observation, header, frame, sizeof(frame));
        const Status sent = context_.roles.link.send_to(
            go.peer, events::Endpoint::Peer, ConstByteSpan(frame, static_cast<size_t>(bytes)));
        if (sent == Status::WouldBlock) {
            blocked[static_cast<size_t>(seat)] = true;
            continue;
        }
        if (sent == Status::Ok)
            delivered = true;
        else
            dropped_++;
        pacer_.spend(seat, slot, target, heard_s, now_ms);
    }
    if (delivered) {
        sequence_++;
        observed_ = true;
    }
}

}  // namespace skyblip::bravo
