#include "products/skyblip_go/services/bravo.h"

#include "core/comms/peer.h"
#include "ports/capabilities.h"

namespace skyblip::go {

namespace {

// INFO: fc 11nov26 Bravo advertises "skyBlip Bravo 3F2A" (products/common/README.md, Settings)
constexpr char kBravoNamePrefix[] = "skyBlip Bravo ";

}  // namespace

Status BravoService::setup() {
    uint8_t epoch[sizeof(boot_epoch_)] = {};
    if (context_.roles.identity.draw_random(ByteSpan(epoch, sizeof(epoch))))
        boot_epoch_ = static_cast<uint32_t>(epoch[0]) | (static_cast<uint32_t>(epoch[1]) << 8) |
                      (static_cast<uint32_t>(epoch[2]) << 16) |
                      (static_cast<uint32_t>(epoch[3]) << 24);
    // INFO: fc 11nov26 a board with no Bluetooth answers Down, and that must not fail the setup
    if (paired(settings_)) (void)context_.roles.link.reserve_peer_connection(true);
    return Status::Ok;
}

void BravoService::tick(uint32_t now_ms) {
    if (!started_) {
        started_ = true;
        started_ms_ = now_ms;
    }
    events::CentralEvent event{};
    while (context_.bus.central_events.pop(event)) take_event(event, now_ms);
    events::RxFrame frame{};
    while (context_.bus.peer_rx.pop(frame)) take_frame(frame, now_ms);

    if (step_ == BravoStep::Refused && refusal_ == BravoRefusal::PhonesConnected) start_scan();
    if (step_ == BravoStep::Connecting && now_ms - step_since_ms_ >= kConnectCeilingMs)
        refuse(BravoRefusal::LinkLost);
    if (step_ == BravoStep::Waiting && now_ms - step_since_ms_ >= kAnswerCeilingMs)
        refuse(BravoRefusal::NoAnswer);

    if (step_ == BravoStep::Connecting && !connecting_ && !up_)
        connect(pairing_with_, kPairingScanWindowUs, kPairingScanPeriodUs);
    if (step_ == BravoStep::Idle && paired(settings_) && link_ == BravoLink::Down && !connecting_ &&
        !up_)
        connect(settings_.bravo, kReconnectScanWindowUs, kReconnectScanPeriodUs);

    if (linked() && now_ms - claimed_ms_ >= kClaimPeriodMs) claim(now_ms);
    if (leaving_ && farewell_owed_) {
        farewell_owed_ = false;
        farewell_ms_ = now_ms;
        claim(now_ms);
    }
    if (leaving_ && now_ms - farewell_ms_ >= kFarewellCeilingMs) leave();
    // INFO: fc 12nov26 both ages are cleared once spent, so neither comes back 49.7 days on
    if (health_heard_ && now_ms - health_heard_ms_ > kHealthMaxAgeMs) health_heard_ = false;
    if (health_heard_ || now_ms - started_ms_ >= kBootSettleMs) settled_ = true;
    context_.state.bravo = state_now();
}

void BravoService::begin_pairing(uint32_t /*now_ms*/) {
    if (paired(settings_)) return;
    end_pairing();
    found_count_ = 0;
    start_scan();
}

void BravoService::end_pairing() {
    if (step_ == BravoStep::Scanning) central_.stop_scan();
    if (step_ == BravoStep::Connecting || step_ == BravoStep::Waiting) central_.disconnect();
    step_ = BravoStep::Idle;
    refusal_ = BravoRefusal::None;
    release_unless_paired();
}

void BravoService::pair(int found_index, uint32_t now_ms) {
    if (step_ != BravoStep::Scanning || found_index < 0 || found_index >= found_count_) return;
    central_.stop_scan();
    pairing_with_ = found_[found_index];
    step_ = BravoStep::Connecting;
    step_since_ms_ = now_ms;
    connect(pairing_with_, kPairingScanWindowUs, kPairingScanPeriodUs);
}

// INFO: fc 11nov26 a connected Go says it unpaired, so Bravo drops its pin and frees its connection
void BravoService::unpair() {
    if (!paired(settings_)) return;
    const bool farewell = up_ && link_ == BravoLink::Up;
    settings_.bravo = events::BluetoothAddress{};
    config_.note_settings_changed();
    link_ = BravoLink::Down;
    health_heard_ = false;
    utc_heard_ = false;
    own_icao_warning_.unpaired();
    if (farewell) {
        leaving_ = true;
        farewell_owed_ = true;
        return;
    }
    if (connecting_ || up_) central_.disconnect();
    release_unless_paired();
}

bool BravoService::take_forgotten_notice() {
    const bool forgotten = forgotten_;
    forgotten_ = false;
    return forgotten;
}

void BravoService::take_event(const events::CentralEvent& event, uint32_t now_ms) {
    switch (event.type) {
        case events::CentralEventType::Found: hear(event); return;
        case events::CentralEventType::Up: came_up(event.payload_bytes, now_ms); return;
        case events::CentralEventType::Down: went_down(); return;
    }
}

void BravoService::hear(const events::CentralEvent& event) {
    if (step_ != BravoStep::Scanning) return;
    for (int i = 0; i < found_count_; i++)
        if (found_[i] == event.address) return;
    if (found_count_ < kBravoFoundRows) found_[found_count_++] = event.address;
}

void BravoService::came_up(uint16_t payload_bytes, uint32_t now_ms) {
    connecting_ = false;
    up_ = true;
    const bool pairing = step_ == BravoStep::Connecting;
    const bool reconnecting = step_ == BravoStep::Idle && paired(settings_);
    if (!pairing && !reconnecting) {
        central_.disconnect();
        return;
    }
    if (payload_bytes < comms::kPeerPayloadFloorBytes && pairing) {
        refuse(BravoRefusal::NarrowLink);
        return;
    }
    if (payload_bytes < comms::kPeerPayloadFloorBytes) {
        link_ = BravoLink::Narrow;
        central_.disconnect();
        return;
    }
    // INFO: fc 11nov26 a paired Go reads CONNECTED only once Bravo's health says it holds the pin
    if (pairing) {
        step_ = BravoStep::Waiting;
        step_since_ms_ = now_ms;
    }
    claim(now_ms);
}

void BravoService::went_down() {
    connecting_ = false;
    up_ = false;
    if (leaving_) leave();
    own_icao_warning_.unpaired();
    if (step_ == BravoStep::Waiting) refuse(BravoRefusal::LinkLost);
    if (link_ == BravoLink::Up) link_ = BravoLink::Down;
}

void BravoService::take_frame(const events::RxFrame& frame, uint32_t now_ms) {
    const ConstByteSpan bytes(frame.data.data(), frame.bytes);
    const Result<link::Command> command = comms::peer_command(bytes);
    if (!command.ok()) return;
    if (command.value() == link::Command::Health) {
        take_health(bytes, now_ms);
    } else if (command.value() == link::Command::Observation) {
        const Result<comms::PeerObservation> observation = comms::read_observation(bytes);
        if (observation.ok())
            fold(observation.value(), now_ms);
        else
            refused_++;
    }
}

void BravoService::take_health(ConstByteSpan bytes, uint32_t now_ms) {
    const Result<comms::PeerHealth> health = comms::read_health(bytes);
    if (health.status() == Status::Unsupported) {
        refuse_protocol();
        return;
    }
    if (!health.ok()) return;
    if (leaving_) {
        if (!health.value().paired) leave();
        return;
    }
    // INFO: fc 11nov26 only Bravo's press offers its pin, so a health that says false pins nothing
    if (step_ == BravoStep::Waiting && !health.value().paired) return;
    if (step_ == BravoStep::Idle && paired(settings_) && !health.value().paired) {
        forget();
        return;
    }
    if (step_ == BravoStep::Waiting) {
        settings_.bravo = pairing_with_;
        config_.note_settings_changed();
        step_ = BravoStep::Idle;
        link_ = BravoLink::Up;
    }
    if (!paired(settings_) || !up_) return;
    link_ = BravoLink::Up;
    own_icao_warning_.paired(health.value().own_icao_set);
    health_ = health.value();
    if (health_.utc_valid) utc_heard_ = true;
    health_heard_ = true;
    health_heard_ms_ = now_ms;
}

// INFO: fc 12nov26 health alone says lost, never quiet traffic: an empty sky is a healthy one
comms::BravoState BravoService::state_now() const {
    if (!paired(settings_)) return comms::BravoState::Unpaired;
    if (health_heard_) {
        if (!health_.listening) return comms::BravoState::ReceiverDown;
        if (health_.utc_valid) return comms::BravoState::Live;
        return utc_heard_ ? comms::BravoState::NoUtc : comms::BravoState::Waiting;
    }
    return settled_ ? comms::BravoState::LinkDown : comms::BravoState::Waiting;
}

// INFO: fc 11nov26 another protocol is never pinned, and a pinned Bravo on one waits for a boot
void BravoService::refuse_protocol() {
    if (step_ == BravoStep::Waiting) {
        refuse(BravoRefusal::UnknownProtocol);
        return;
    }
    if (!paired(settings_)) return;
    link_ = BravoLink::Unsupported;
    own_icao_warning_.unpaired();
    if (connecting_ || up_) central_.disconnect();
}

// INFO: fc 11nov26 Bravo holds no pin for this Go, so the Go drops its own and says so once
void BravoService::forget() {
    settings_.bravo = events::BluetoothAddress{};
    config_.note_settings_changed();
    link_ = BravoLink::Down;
    health_heard_ = false;
    utc_heard_ = false;
    own_icao_warning_.unpaired();
    forgotten_ = true;
    central_.disconnect();
    release_unless_paired();
}

void BravoService::leave() {
    leaving_ = false;
    farewell_owed_ = false;
    if (up_ || connecting_) central_.disconnect();
    release_unless_paired();
}

void BravoService::start_scan() {
    if (context_.roles.link.reserve_peer_connection(true) != Status::Ok) {
        step_ = BravoStep::Refused;
        refusal_ = BravoRefusal::PhonesConnected;
        return;
    }
    if (central_.scan(ports::ScanDuty{kPairingScanWindowUs, kPairingScanPeriodUs},
                      kBravoNamePrefix) != Status::Ok) {
        refuse(BravoRefusal::LinkLost);
        return;
    }
    step_ = BravoStep::Scanning;
    refusal_ = BravoRefusal::None;
}

void BravoService::refuse(BravoRefusal refusal) {
    if (step_ == BravoStep::Scanning) central_.stop_scan();
    if (connecting_ || up_) central_.disconnect();
    step_ = BravoStep::Refused;
    refusal_ = refusal;
    release_unless_paired();
}

void BravoService::release_unless_paired() {
    if (!paired(settings_)) (void)context_.roles.link.reserve_peer_connection(false);
}

void BravoService::connect(const events::BluetoothAddress& address, uint32_t window_us,
                           uint32_t period_us) {
    connecting_ = central_.connect(address, ports::ScanDuty{window_us, period_us}) == Status::Ok;
}

void BravoService::claim(uint32_t now_ms) {
    claimed_ms_ = now_ms;
    const model::Ownship& own = context_.state.own;
    const diagnostics::Instant at = context_.instant(now_ms);
    comms::PeerClaim claim{};
    claim.boot_epoch = boot_epoch_;
    claim.utc_valid = at.utc_dated;
    claim.at_s = at.at_s;
    claim.phase_valid = at.phase_valid;
    claim.into_ms = at.into_ms;
    claim.fix_valid = own.fix_valid;
    claim.latitude_1e7 = own.latitude_1e7;
    claim.longitude_1e7 = own.longitude_1e7;
    claim.altitude_hae_mm = own.altitude_hae_mm;
    claim.fix_age_ms = now_ms - own.fix_at_ms;
    claim.altitude_hae_valid = own.fix_valid;
    claim.altitude_hae_age_ms = claim.fix_age_ms;
    claim.pressure_altitude_valid = own.pressure_altitude_valid;
    claim.pressure_altitude_mm = own.pressure_altitude_mm;
    claim.pressure_altitude_age_ms = now_ms - own.pressure_altitude_at_ms;
    claim.own_icao = settings_.own_icao;
    claim.alarm_requested = takes_alarm();
    claim.paired = paired(settings_);
    uint8_t frame[comms::kPeerClaimWidestBytes];
    const int bytes = comms::write_peer_claim(claim, frame, sizeof(frame));
    // INFO: fc 11nov26 the central counts and logs every write it refuses, so the result is its own
    if (bytes > 0) (void)central_.write(ConstByteSpan(frame, static_cast<size_t>(bytes)));
}

// INFO: fc 12nov26 a Go+ sounds for both units whatever its volume, so off on it silences the pair
bool BravoService::takes_alarm() const {
    const ports::Capabilities fitted = context_.roles.capabilities;
    const bool sounds = ports::has(fitted, ports::Capability::Buzzer) ||
                        ports::has(fitted, ports::Capability::Haptic);
    return sounds && context_.state.own.fix_valid && state_now() == comms::BravoState::Live;
}

}  // namespace skyblip::go
