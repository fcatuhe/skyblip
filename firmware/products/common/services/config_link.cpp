#include "products/common/services/config_link.h"

#include <cstring>

#include "core/events/link.h"

namespace skyblip::common {

static_assert(comms::TrustedBrowsers::kBlobBytes + 64 <=
                  size_t{timing::DurableWriteWindow::kBlobWords} * 4,
              "the durable-write budget copies every live record a garbage collection moves");

Status ConfigLinkService::setup() {
    load();
    load_browsers();
    adopt_install_trust();
    return Status::Ok;
}

void ConfigLinkService::tick(uint32_t now_ms) {
    publish_upload_verdict();
    accrue_connected(now_ms);
    drain_link_events(now_ms);
    // INFO: fc 11nov26 nobody calling this leaves the gate at Unknown, so it never closes in flight
    config_.set_flight_state(context_.state.flight.confirmed_state);
    publish_upload_verdict();

    // INFO: cf 02aug26 core/power decided the level, and the link carries it rather than keeping a
    // second opinion
    config_.set_battery_state(context_.state.power.battery, context_.state.power.level);
    // INFO: fc 20nov26 the comparator's warning turns a phone's set into a refusal at the door
    config_.set_supply_warned(power_.supply_warned());
    config_.set_die_temperature(context_.state.power.die_dc, context_.state.power.die_valid);
    config_.set_range_refused(context_.state.traffic.implausible());
    config_.set_hub_image(context_.state.imu.image);
    config_.set_bravo_state(context_.state.bravo);
    config_.set_own_transponder(context_.state.own_transponder);
    config_.set_no_tx(context_.state.transmit.shown());
    config_.set_capture(context_.state.capture.available, context_.diagnostics.armed());

    config_.resume_replies(now_ms);
    events::RxFrame frame{};
    while (!config_.replying() && context_.bus.link_rx.pop(frame)) {
        if (!config_.from_live_connection(frame)) continue;
        config_.on_rx(frame);
        record_link(diagnostics::LinkAction::Received, frame.session_id, frame.bytes, now_ms);
    }
    place_unlock(now_ms);
    publish_identity(now_ms);

    config_.tick(now_ms);
    publish_link();
    drain_settings(now_ms);
    drain_browsers(now_ms);
    drain_install_trust(now_ms);
    confirm_image_once_healthy();
    watch_claim(now_ms);
    watch_link_drops(now_ms);
}

uint32_t ConfigLinkService::due_in_ms(uint32_t /*now_ms*/) const {
    const bool owed = config_.replying() || (config_.status_push_due() && config_.link_up()) ||
                      writes_.pending() || browser_writes_.pending() || unlock_writes_.pending() ||
                      trust_page_erases_.pending();
    return owed ? runtime::kServicePassCeilingMs : runtime::kServiceRestCeilingMs;
}

// INFO: fc 18oct26 before the link advertises at all: the name says whether the unit is activated
void ConfigLinkService::load_identity() {
    unit_.load();
    publish_identity(context_.roles.clock.milliseconds());
}

void ConfigLinkService::publish_identity(uint32_t now_ms) {
    const identity::Activation activation = unit_.activation();
    if (!identity_published_ || activation != context_.state.activation) {
        identity_published_ = true;
        advertised_ = false;
        advertise_asked_ = false;
        context_.state.activation = activation;
    }
    if (!advertised_) advertise(now_ms);
}

// INFO: fc 18oct26 asked again until the link takes it, a unit nobody sees serves no one
void ConfigLinkService::advertise(uint32_t now_ms) {
    if (!ports::has(context_.roles.capabilities, ports::Capability::Link)) return;
    if (advertise_asked_ && now_ms - advertise_asked_ms_ < kAdvertiseRetryPeriodMs) return;
    advertise_asked_ = true;
    advertise_asked_ms_ = now_ms;
    char name[identity::kBluetoothNameBytes + 1];
    name_rule_(name, sizeof(name), product_name_, context_.state.activation);
    advertised_ = context_.roles.link.advertise_as(name) == Status::Ok;
}

// INFO: fc 18oct26 the code was verified and its nonce spent on receipt; only the write waits here
void ConfigLinkService::place_unlock(uint32_t now_ms) {
    if (!config_.unlock_due()) {
        unlock_writes_ = timing::DurableWriteWindow{};
        return;
    }
    // INFO: fc 18oct26 never forced onto a low cell: settle_unlock drops it, the pilot asks again
    if (!config_.settings_writable()) {
        config_.settle_unlock();
        unlock_writes_ = timing::DurableWriteWindow{};
        return;
    }
    if (!unlock_writes_.pending()) unlock_writes_.request(now_ms);
    const timing::DurableWriteVerdict verdict = unlock_writes_.decide(
        context_.state.rf.plan, context_.state.rf.dwell, context_.roles.clock.milliseconds());
    if (verdict != timing::DurableWriteVerdict::Place &&
        verdict != timing::DurableWriteVerdict::Forced)
        return;
    config_.settle_unlock();
    unlock_writes_.placed(now_ms, verdict == timing::DurableWriteVerdict::Forced);
}

void ConfigLinkService::publish_link() {
    context_.state.link.phone_connected = context_.roles.link.phones() > 0;
    context_.state.link.prompt_standing =
        config_.pending() != Pending::None &&
        config_.prompt_allowed_in(context_.state.flight.confirmed_state);
}

void ConfigLinkService::publish_upload_verdict() {
    context_.roles.dfu.publish_approved_version(config_.approved_version());
    context_.roles.dfu.publish_upload_allowed(config_.upload_allowed());
}

void ConfigLinkService::accrue_connected(uint32_t now_ms) {
    connected_.observe(config_.link_up(), now_ms);
    context_.state.duty.ble_connected_ms = connected_.on_ms();
}

void ConfigLinkService::record_link(diagnostics::LinkAction action, uint16_t session,
                                    uint16_t frame_bytes, uint32_t now_ms) {
    if (!context_.diagnostics.armed()) return;
    context_.diagnostics.record(link_record(action, session, frame_bytes),
                                context_.instant(now_ms));
}

diagnostics::Link ConfigLinkService::link_record(diagnostics::LinkAction action, uint16_t session,
                                                 uint16_t frame_bytes) const {
    diagnostics::Link event{};
    event.link_session = session;
    event.payload_bytes = context_.roles.link.payload_bytes_to(session);
    event.frame_bytes = frame_bytes;
    event.holder = config_.claim().holder();
    event.drops = config_.link_drops();
    event.action = action;
    event.endpoint = events::Endpoint::Config;
    event.claim_held = config_.claim().held();
    return event;
}

void ConfigLinkService::record_link_down(const events::LinkEvent& down, uint32_t now_ms) {
    if (!context_.diagnostics.armed()) return;
    diagnostics::Link event = link_record(diagnostics::LinkAction::Down, down.session_id, 0);
    event.payload_bytes = down.payload_bytes;
    event.mtu_exchange_started_by = down.mtu_exchange_started_by;
    context_.diagnostics.record(event, context_.instant(now_ms));
}

// INFO: fc 20sep26 one central at a time commands the device, and which one is what stalls the next
void ConfigLinkService::watch_claim(uint32_t now_ms) {
    const comms::LinkClaim& claim = config_.claim();
    if (claim.held() == recorded_claim_held_ && claim.holder() == recorded_holder_) return;
    const bool taken = claim.held();
    recorded_claim_held_ = taken;
    const uint16_t previous_holder = recorded_holder_;
    recorded_holder_ = claim.holder();
    record_link(
        taken ? diagnostics::LinkAction::ClaimTaken : diagnostics::LinkAction::ClaimReleased,
        taken ? claim.holder() : previous_holder, 0, now_ms);
}

void ConfigLinkService::watch_link_drops(uint32_t now_ms) {
    if (config_.link_drops() == recorded_drops_) return;
    recorded_drops_ = config_.link_drops();
    record_link(diagnostics::LinkAction::Dropped, config_.claim_holder(), 0, now_ms);
}

// INFO: le 04aug26 the single reader of connection events, drained first, because the prompt, the
// upload window and the gauge depend on the link
void ConfigLinkService::drain_link_events(uint32_t now_ms) {
    events::LinkEvent event{};
    while (context_.bus.link_events.pop(event)) {
        if (event.type == events::LinkEventType::Up) {
            config_.on_link_up(
                events::LinkUp{event.session_id, event.payload_bytes, event.connection});
            record_link(diagnostics::LinkAction::Up, event.session_id, 0, now_ms);
        } else {
            config_.on_link_down(events::LinkDown{event.session_id, event.reason});
            record_link_down(event, now_ms);
        }
    }
}

// INFO: fc 20nov26 taken at once and never held, so a stream of changes leaves as one write
void ConfigLinkService::take_request(uint32_t now_ms) {
    if (!config_.settings_dirty()) return;
    config_.clear_dirty();
    writes_.request(now_ms);
}

// INFO: fc 20nov26 asked before the request is taken, so a refused change stays dirty and the bound
// never forces it onto flash
bool ConfigLinkService::hold_for_power() {
    if (may_persist()) {
        held_ = false;
        return false;
    }
    if (!held_ && (config_.settings_dirty() || writes_.pending())) {
        held_ = true;
        refused_++;
    }
    return true;
}

void ConfigLinkService::drain_settings(uint32_t now_ms) {
    if (hold_for_power()) return;
    take_request(now_ms);
    // INFO: fc 03oct26 the stall starts at the clock, not where the pass began
    const timing::DurableWriteVerdict verdict = writes_.decide(
        context_.state.rf.plan, context_.state.rf.dwell, context_.roles.clock.milliseconds());
    if (verdict != timing::DurableWriteVerdict::Idle) record_write(writes_, verdict, now_ms);
    if (verdict != timing::DurableWriteVerdict::Place &&
        verdict != timing::DurableWriteVerdict::Forced)
        return;
    write_settings(now_ms, verdict == timing::DurableWriteVerdict::Forced);
}

// INFO: fc 25sep26 a refusal restarts the bound, so a refusing store is forced once per bound
void ConfigLinkService::write_settings(uint32_t now_ms, bool forced) {
    if (persist()) {
        writes_.placed(now_ms, forced);
    } else {
        failed_++;
        writes_.refused(now_ms, forced);
    }
}

// INFO: fc 13nov26 a list that will not load is empty, so each browser asks for the press again
void ConfigLinkService::load_browsers() {
    if (!storable()) return;
    uint8_t blob[comms::TrustedBrowsers::kBlobBytes];
    size_t blob_bytes = 0;
    if (!is_ok(context_.roles.key_value.read(kBrowsersKey, blob, sizeof(blob), blob_bytes))) return;
    (void)config_.trusted_browsers().adopt(blob, blob_bytes);
}

// INFO: fc 13nov26 placed like the settings, and held on a low cell like them, never forced onto it
void ConfigLinkService::drain_browsers(uint32_t now_ms) {
    if (!may_persist()) return;
    if (config_.browsers_dirty()) {
        config_.clear_browsers_dirty();
        browser_writes_.request(now_ms);
    }
    const timing::DurableWriteVerdict verdict = browser_writes_.decide(
        context_.state.rf.plan, context_.state.rf.dwell, context_.roles.clock.milliseconds());
    if (verdict != timing::DurableWriteVerdict::Idle)
        record_write(browser_writes_, verdict, now_ms);
    if (verdict != timing::DurableWriteVerdict::Place &&
        verdict != timing::DurableWriteVerdict::Forced)
        return;
    write_browsers(now_ms, verdict == timing::DurableWriteVerdict::Forced);
}

void ConfigLinkService::write_browsers(uint32_t now_ms, bool forced) {
    uint8_t blob[comms::TrustedBrowsers::kBlobBytes];
    const size_t blob_bytes = config_.trusted_browsers().to_blob(blob, sizeof(blob));
    if (!storable() || is_ok(context_.roles.key_value.write(kBrowsersKey, blob, blob_bytes))) {
        browser_writes_.placed(now_ms, forced);
        install_trust_.landed();
    } else {
        failed_++;
        browser_writes_.refused(now_ms, forced);
    }
}

// INFO: fc 16nov26 only a unit that can store the list trusts the browser that installed it
void ConfigLinkService::adopt_install_trust() {
    if (!storable() || !install_trust_.adopt(context_.roles.dfu, config_.trusted_browsers()))
        return;
    config_.note_browsers_changed();
}

// INFO: fc 16nov26 a page erase stalls the core like a settings write, so it is placed like one
void ConfigLinkService::drain_install_trust(uint32_t now_ms) {
    if (!install_trust_.erase_due() || !may_persist()) return;
    if (!trust_page_erases_.pending()) trust_page_erases_.request(now_ms);
    const timing::DurableWriteVerdict verdict = trust_page_erases_.decide(
        context_.state.rf.plan, context_.state.rf.dwell, context_.roles.clock.milliseconds());
    if (verdict != timing::DurableWriteVerdict::Place &&
        verdict != timing::DurableWriteVerdict::Forced)
        return;
    const bool forced = verdict == timing::DurableWriteVerdict::Forced;
    if (install_trust_.erase(context_.roles.dfu)) {
        trust_page_erases_.placed(now_ms, forced);
    } else {
        failed_++;
        trust_page_erases_.refused(now_ms, forced);
    }
}

void ConfigLinkService::record_write(const timing::DurableWriteWindow& window,
                                     timing::DurableWriteVerdict verdict, uint32_t now_ms) {
    if (!context_.diagnostics.armed()) return;
    diagnostics::Write value{};
    value.waited_ms = window.waited_ms(now_ms);
    value.phase_ms = static_cast<uint16_t>(context_.state.rf.dwell.phase_ms);
    value.requests = window.requests();
    value.writes = window.writes();
    value.forced = window.forced();
    value.placement = verdict;
    value.kind = power::DurableWrite::Settings;
    value.pending = window.pending();
    context_.diagnostics.record(value, context_.instant(now_ms));
}

void ConfigLinkService::flush_settings(uint32_t now_ms) {
    if (hold_for_power()) return;
    if (config_.browsers_dirty() || browser_writes_.pending()) {
        config_.clear_browsers_dirty();
        write_browsers(now_ms, /*forced=*/false);
    }
    take_request(now_ms);
    if (!writes_.pending()) return;
    write_settings(now_ms, /*forced=*/false);
}

void ConfigLinkService::load() {
    if (loaded_) return;
    load_image_state();
    store_.restore_defaults();
    if (!storable()) {
        loaded_ = true;
        return;
    }

    uint8_t blob[kBlobBytes];
    size_t blob_bytes = 0;
    const Status read = context_.roles.key_value.read(kSettingsKey, blob, sizeof(blob), blob_bytes);
    if (read == Status::NotFound) return;
    loaded_ = true;
    // INFO: fc 21nov26 settings the store holds and cannot hand over are a fault, never a fresh
    // unit
    if (!is_ok(read)) {
        fallback_ = settings::Fallback::Defaults;
    } else if (adopt(blob, blob_bytes)) {
        remember(blob, blob_bytes);
    } else if (newer_layout(blob, blob_bytes)) {
        newer_kept_ = true;
        load_prior();
    } else {
        remember(blob, blob_bytes);
        fallback_ = settings::Fallback::Defaults;
    }
    config_.set_settings_fallback(fallback_);
}

bool ConfigLinkService::newer_layout(const uint8_t* blob, size_t blob_bytes) const {
    return settings::sealed(blob, blob_bytes) &&
           settings::blob_version(blob) > store_.blob_version();
}

void ConfigLinkService::load_prior() {
    uint8_t blob[kBlobBytes];
    size_t blob_bytes = 0;
    if (is_ok(context_.roles.key_value.read(kPriorSettingsKey, blob, sizeof(blob), blob_bytes)) &&
        adopt(blob, blob_bytes)) {
        remember(blob, blob_bytes);
        fallback_ = settings::Fallback::Prior;
    } else {
        fallback_ = settings::Fallback::Defaults;
    }
}

bool ConfigLinkService::adopt(const uint8_t* blob, size_t blob_bytes) {
    return is_ok(store_.adopt(blob, blob_bytes));
}

void ConfigLinkService::remember(const uint8_t* blob, size_t blob_bytes) {
    if (blob_bytes > kBlobBytes) return;
    std::memcpy(stored_, blob, blob_bytes);
    stored_bytes_ = blob_bytes;
}

bool ConfigLinkService::persist() {
    if (!storable()) return true;
    uint8_t blob[kBlobBytes];
    const size_t blob_bytes = store_.to_blob(blob, sizeof(blob));
    if (!blob_fits(blob_bytes)) return false;
    if (stored_bytes_ == blob_bytes && std::memcmp(stored_, blob, blob_bytes) == 0) return true;
    if (!is_ok(context_.roles.key_value.write(settings_key(), blob, blob_bytes))) return false;
    std::memcpy(stored_, blob, blob_bytes);
    stored_bytes_ = blob_bytes;
    return true;
}

}  // namespace skyblip::common
