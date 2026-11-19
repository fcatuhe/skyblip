#include "products/skyblip_go/services/bravo.h"

#include <cstring>

#include "core/comms/peer.h"
#include "core/util/format.h"
#include "ports/capabilities.h"

namespace skyblip::go {

namespace {

// INFO: fc 11nov26 Bravo advertises "skyBlip Bravo 3F2A" (products/common/README.md, Settings)
constexpr char kBravoNamePrefix[] = "skyBlip Bravo ";
constexpr int kTagDigits = 4;

// INFO: fc 16nov26 the name carries the tag Bravo reads off FICR DEVICEADDR, its address's low bits
bool named(const events::CentralEvent& event, uint16_t tag) {
    char name[sizeof(kBravoNamePrefix) + kTagDigits]{};
    const int written = format_string(name, kBravoNamePrefix);
    name[written + format_hex(name + written, tag, kTagDigits)] = 0;
    return std::strcmp(event.name, name) == 0 && events::bluetooth_tag(event.address) == tag;
}

link::Reason verdict_of(BravoRefusal refusal) {
    switch (refusal) {
        case BravoRefusal::PhonesConnected: return link::Reason::PhonesConnected;
        case BravoRefusal::NarrowLink: return link::Reason::NarrowLink;
        case BravoRefusal::NoAnswer: return link::Reason::NoAnswer;
        case BravoRefusal::UnknownProtocol: return link::Reason::UnknownProtocol;
        case BravoRefusal::NotFound: return link::Reason::NotFound;
        case BravoRefusal::LinkLost:
        case BravoRefusal::None: break;
    }
    return link::Reason::LinkLost;
}

}  // namespace

Status BravoService::setup() {
    config_.serve_pairing(comms::PairingSide::Go);
    const std::optional<uint32_t> epoch = comms::draw_boot_epoch(context_.roles.random);
    if (!epoch) return Status::Down;
    boot_epoch_ = *epoch;
    // INFO: fc 11nov26 a board with no Bluetooth answers Down, and that must not fail the setup
    if (paired(settings_)) (void)context_.roles.link.reserve_peer_connection(true);
    return Status::Ok;
}

void BravoService::tick(uint32_t now_ms) {
    if (!started_) {
        started_ = true;
        started_ms_ = now_ms;
    }
    if (const std::optional<uint16_t> tag = config_.take_pair_request()) pair_with(*tag, now_ms);
    if (seek_due_ && !leaving_) seek(now_ms);
    events::CentralEvent event{};
    while (central_events_.pop(event)) take_event(event, now_ms);
    events::RxFrame frame{};
    while (context_.bus.peer_rx.pop(frame)) take_frame(frame, now_ms);

    if (step_ == BravoStep::Refused && refusal_ == BravoRefusal::PhonesConnected) start_scan();
    if (step_ == BravoStep::Scanning && wanted_ && now_ms - wanted_since_ms_ >= kConnectCeilingMs)
        refuse(BravoRefusal::NotFound);
    if (step_ == BravoStep::Connecting && now_ms - step_since_ms_ >= kConnectCeilingMs)
        refuse(BravoRefusal::LinkLost);
    if (step_ == BravoStep::Waiting && now_ms - step_since_ms_ >= kAnswerCeilingMs)
        refuse(BravoRefusal::NoAnswer);

    if (step_ == BravoStep::Connecting && !connecting_ && !up_)
        connect(pairing_with_, kPairingScanWindowUs, kPairingScanPeriodUs);
    if (step_ == BravoStep::Idle && paired(settings_) && link_ == BravoLink::Down && !connecting_ &&
        !up_)
        connect(settings_.bravo, kReconnectScanWindowUs, kReconnectScanPeriodUs);

    if (linked() && (now_ms - claimed_ms_ >= kClaimPeriodMs || call_changed())) claim(now_ms);
    if (leaving_ && farewell_owed_) {
        farewell_owed_ = false;
        farewell_ms_ = now_ms;
        claim(now_ms);
    }
    if (leaving_ && now_ms - farewell_ms_ >= kFarewellCeilingMs) leave();
    // INFO: fc 12nov26 the health age is cleared once spent, so it never comes back 49.7 days on
    if (health_heard_ && now_ms - health_heard_ms_ > kHealthMaxAgeMs) health_heard_ = false;
    if (context_.state.flight.rolling && now_ms - started_ms_ >= kBootSettleMs) rolled_ = true;
    context_.state.bravo = state_now();
    context_.state.own_transponder = own_transponder_on_ground();
    config_.set_paired_tag(paired(settings_)
                               ? std::optional<uint16_t>(events::bluetooth_tag(settings_.bravo))
                               : std::nullopt);
}

uint32_t BravoService::due_in_ms(uint32_t now_ms) const {
    if (step_ != BravoStep::Idle || leaving_ || seek_due_) return runtime::kServicePassCeilingMs;
    if (linked()) return runtime::remaining_ms(now_ms - claimed_ms_, kClaimPeriodMs);
    return runtime::kServiceRestCeilingMs;
}

void BravoService::begin_pairing(uint32_t /*now_ms*/) {
    if (paired(settings_) || asking_again_) return;
    end_pairing();
    found_count_ = 0;
    start_scan();
}

void BravoService::end_pairing() {
    asking_again_ = false;
    if (step_ == BravoStep::Scanning) central_.stop_scan();
    if (step_ == BravoStep::Connecting || step_ == BravoStep::Waiting) central_.disconnect();
    step_ = BravoStep::Idle;
    refusal_ = BravoRefusal::None;
    release_unless_paired();
    wanted_.reset();
    seek_due_ = false;
    answer_link(link::Reason::Cancelled);
}

// INFO: fc 15nov26 the link to the Bravo just unpaired is the central's one, so a pairing waits for
// it
void BravoService::pair(int found_index, uint32_t now_ms) {
    if (step_ != BravoStep::Scanning || leaving_ || found_index < 0 || found_index >= found_count_)
        return;
    pick(found_[found_index], now_ms);
}

void BravoService::pick(const events::BluetoothAddress& address, uint32_t now_ms) {
    central_.stop_scan();
    wanted_.reset();
    pairing_with_ = address;
    step_ = BravoStep::Connecting;
    step_since_ms_ = now_ms;
    connect(pairing_with_, kPairingScanWindowUs, kPairingScanPeriodUs);
}

// INFO: fc 16nov26 the link already told an earlier browser cancelled, so only this one is owed
void BravoService::pair_with(uint16_t tag, uint32_t now_ms) {
    link_answer_owed_ = false;
    if (paired(settings_) && events::bluetooth_tag(settings_.bravo) == tag) {
        link_answer_owed_ = true;
        answer_link(link::Reason::Paired);
        return;
    }
    end_pairing();
    unpair();
    link_answer_owed_ = true;
    link_pairing_noticed_ = true;
    wanted_ = tag;
    wanted_since_ms_ = now_ms;
    seek_due_ = true;
}

bool BravoService::take_link_pairing_notice() {
    const bool noticed = link_pairing_noticed_;
    link_pairing_noticed_ = false;
    return noticed;
}

void BravoService::seek(uint32_t now_ms) {
    seek_due_ = false;
    found_count_ = 0;
    wanted_since_ms_ = now_ms;
    start_scan();
    if (step_ == BravoStep::Refused) {
        wanted_.reset();
        answer_link(verdict_of(refusal_));
    }
}

void BravoService::answer_link(link::Reason verdict) {
    if (!link_answer_owed_) return;
    link_answer_owed_ = false;
    config_.pairing_ended(verdict);
}

// INFO: fc 11nov26 a connected Go says it unpaired, so Bravo drops its pin and frees its connection
void BravoService::unpair() {
    if (!paired(settings_)) return;
    const bool farewell = up_ && link_ == BravoLink::Up;
    drop_pin();
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

bool BravoService::take_asking_notice() {
    const bool asking = asking_noticed_;
    asking_noticed_ = false;
    return asking;
}

void BravoService::take_event(const events::CentralEvent& event, uint32_t now_ms) {
    switch (event.type) {
        case events::CentralEventType::Found: hear(event, now_ms); return;
        case events::CentralEventType::Up: came_up(event.payload_bytes, now_ms); return;
        case events::CentralEventType::Down: went_down(); return;
    }
}

void BravoService::hear(const events::CentralEvent& event, uint32_t now_ms) {
    if (step_ != BravoStep::Scanning) return;
    if (wanted_) {
        if (named(event, *wanted_)) pick(event.address, now_ms);
        return;
    }
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
    aircraft_address_warning_.unpaired();
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
        if (observation.ok()) {
            fold(observation.value(), now_ms);
        } else {
            refused_++;
            log(BravoVerdict::Refused, nullptr, now_ms);
        }
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
        ask_again(now_ms);
        return;
    }
    if (step_ == BravoStep::Waiting) {
        asking_again_ = false;
        settings_.bravo = pairing_with_;
        // INFO: fc 17nov26 a plain Go's pair sounds out of the box, as Bravo alone does
        if (!common::can_sound(context_.roles.capabilities)) settings_.alarm = Alarm::Loud;
        config_.note_settings_changed();
        step_ = BravoStep::Idle;
        link_ = BravoLink::Up;
        answer_link(link::Reason::Paired);
    }
    if (!paired(settings_) || !up_) return;
    link_ = BravoLink::Up;
    aircraft_address_warning_.paired(health.value().aircraft_address_set);
    if (health.value().dismissed_announcement)
        alarm_.dismiss_announcement(*health.value().dismissed_announcement);
    health_ = health.value();
    if (health_.utc_valid) utc_heard_ = true;
    health_heard_ = true;
    health_heard_ms_ = now_ms;
}

// INFO: fc 14nov26 on the ground only, as the AIRCRAFT menu proposes it: aloft a wingman is as loud
std::optional<comms::OwnTransponderHeard> BravoService::own_transponder_on_ground() const {
    const bool on_ground = context_.state.bravo == comms::BravoState::Live &&
                           context_.state.flight.confirmed_state == flight::FlightState::Ground;
    return on_ground ? own_transponder() : std::nullopt;
}

// INFO: fc 12nov26 health alone says lost, never quiet traffic: an empty sky is a healthy one
comms::BravoState BravoService::state_now() const {
    if (!paired(settings_)) return comms::BravoState::Unpaired;
    if (health_heard_) {
        if (!health_.listening) return comms::BravoState::ReceiverDown;
        if (health_.utc_valid) return comms::BravoState::Live;
        return counted_on() ? comms::BravoState::NoUtc : comms::BravoState::Waiting;
    }
    return counted_on() ? comms::BravoState::LinkDown : comms::BravoState::Waiting;
}

// INFO: fc 11nov26 another protocol is never pinned, and a pinned Bravo on one waits for a boot
void BravoService::refuse_protocol() {
    if (step_ == BravoStep::Waiting) {
        refuse(BravoRefusal::UnknownProtocol);
        return;
    }
    if (!paired(settings_)) return;
    link_ = BravoLink::Unsupported;
    aircraft_address_warning_.unpaired();
    if (connecting_ || up_) central_.disconnect();
}

// INFO: fc 15nov26 Bravo holds no pin for this Go, so the Go asks for the press a new pairing takes
void BravoService::ask_again(uint32_t now_ms) {
    pairing_with_ = settings_.bravo;
    drop_pin();
    asking_again_ = true;
    asking_noticed_ = true;
    step_ = BravoStep::Waiting;
    step_since_ms_ = now_ms;
    claim(now_ms);
}

void BravoService::drop_pin() {
    settings_.bravo = events::BluetoothAddress{};
    if (!common::can_sound(context_.roles.capabilities) && common::audible(settings_.alarm))
        settings_.alarm = Alarm::Visual;
    config_.note_settings_changed();
    link_ = BravoLink::Down;
    health_heard_ = false;
    utc_heard_ = false;
    rolled_ = false;
    aircraft_address_warning_.unpaired();
}

// INFO: fc 11nov26 the Go asked again and Bravo never answered, so the Go says so once
void BravoService::forget() {
    asking_again_ = false;
    forgotten_ = true;
    step_ = BravoStep::Idle;
    refusal_ = BravoRefusal::None;
    release_unless_paired();
}

void BravoService::leave() {
    leaving_ = false;
    farewell_owed_ = false;
    if (up_ || connecting_) central_.disconnect();
    if (step_ == BravoStep::Idle) release_unless_paired();
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
    wanted_.reset();
    answer_link(verdict_of(refusal));
    if (asking_again_) {
        forget();
        return;
    }
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
    claim.aircraft_address = settings_.aircraft_address;
    claim.alarm = static_cast<uint8_t>(settings_.alarm);
    claim.call = context_.state.alarm_call;
    claim.paired = paired(settings_);
    uint8_t frame[comms::kPeerClaimWidestBytes];
    const int bytes = comms::write_peer_claim(claim, frame, sizeof(frame));
    // INFO: fc 11nov26 a refused claim is dropped, and the next one leaves kClaimPeriodMs later |
    // 17nov26 a changed call is written again on the next pass
    if (bytes == 0 ||
        central_.write(ConstByteSpan(frame, static_cast<size_t>(bytes))) != Status::Ok)
        return;
    claimed_call_ = claim.call;
    claimed_alarm_ = settings_.alarm;
}

bool BravoService::call_changed() const {
    return context_.state.alarm_call != claimed_call_ || settings_.alarm != claimed_alarm_;
}

}  // namespace skyblip::go
