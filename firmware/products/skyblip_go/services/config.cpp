#include "products/skyblip_go/services/config.h"

#include <cstring>

#include "core/events/link.h"

namespace skyblip::go {

Status ConfigLinkService::setup() {
    load();
    return Status::Ok;
}

void ConfigLinkService::tick(uint32_t now_ms) {
    drain_link_events(now_ms);
    // INFO: cf 02aug26 nobody calling this leaves the gate at Unknown, which refuses everything
    config_.set_flight_state(context_.state.flight.confirmed_state);

    // INFO: cf 02aug26 core/power decided what the divider reading means and
    // what a low cell is; this hands the already-decided numbers to the link
    // rather than keeping a second opinion, same as the flight gate above it.
    config_.set_battery_state(context_.state.power.battery, context_.state.power.level);
    // And the half no voltage can report: the SoC's own power-failure comparator
    // has fired. It is what turns a "set" from a phone into a refusal at the
    // door, so it travels the same way the level does - decided in core/power,
    // carried here, never re-derived.
    config_.set_supply_warned(power_.supply_warned());
    // Read where it is already sampled, on the cadence the sensor deserves: the
    // service that owns the cell owns the die too, and this only carries the
    // number to the link.
    config_.set_die_temperature(power_.die_temperature_dc(), power_.die_temperature_valid());
    // The range gate's own counter, read off the table that keeps it rather than
    // recounted here (core/traffic/table.h).
    config_.set_range_refused(context_.state.traffic.implausible_count());

    config_.resume_replies(now_ms);
    events::RxFrame frame{};
    while (!config_.replying() && context_.bus.link_rx.pop(frame)) {
        config_.on_rx(frame);
        record_link(diag::LinkAction::Received, frame.session_id, frame.len, now_ms);
    }

    config_.tick(now_ms);
    adopt_learned_trim();
    drain_settings(now_ms);
    spend_gnss_cold_start();
    confirm_image_once_healthy();
    watch_claim(now_ms);
    watch_link_drops(now_ms);
}

void ConfigLinkService::record_link(diag::LinkAction action, uint16_t session, uint16_t frame_bytes,
                                    uint32_t now_ms) {
    if (!context_.diag.armed()) return;
    diag::Link event{};
    event.session = session;
    event.payload_bytes = context_.roles.link.payload_bytes();
    event.frame_bytes = frame_bytes;
    event.holder = config_.claim().holder();
    event.drops = config_.link_drops();
    event.action = action;
    event.endpoint = events::Endpoint::Config;
    event.claim_held = config_.claim().held();
    context_.diag.record(event, context_.instant(now_ms));
}

// INFO: fc 20sep26 one central at a time commands the device, and which one is what stalls the next
void ConfigLinkService::watch_claim(uint32_t now_ms) {
    const comms::LinkClaim& claim = config_.claim();
    if (claim.held() == recorded_claim_held_ && claim.holder() == recorded_holder_) return;
    const bool taken = claim.held();
    recorded_claim_held_ = taken;
    const uint16_t was = recorded_holder_;
    recorded_holder_ = claim.holder();
    record_link(taken ? diag::LinkAction::ClaimTaken : diag::LinkAction::ClaimReleased,
                taken ? claim.holder() : was, 0, now_ms);
}

void ConfigLinkService::watch_link_drops(uint32_t now_ms) {
    if (config_.link_drops() == recorded_drops_) return;
    recorded_drops_ = config_.link_drops();
    record_link(diag::LinkAction::Dropped, config_.session(), 0, now_ms);
}

// A cold start costs the next fix and the driver puts our configuration back
// behind it, so it is spent where the request is read rather than reached for
// through the board.
void ConfigLinkService::adopt_learned_trim() {
    if (settings_.battery_offset_manual || !power_.trim_learned()) return;
    if (settings_.battery_offset_mv == power_.learned_offset_mv()) return;
    settings_.battery_offset_mv = power_.learned_offset_mv();
    config_.note_settings_changed();
}

void ConfigLinkService::spend_gnss_cold_start() {
    if (!config_.gnss_cold_start_requested()) return;
    config_.clear_gnss_cold_start_request();
    context_.roles.gnss.request_restart(ports::Restart::Cold);
}

// INFO: le 04aug26 The single reader of the connection, drained once per pass and
// first, because everything below it is said about a link: the standing prompt a
// disconnect cancels, the upload window it closes, and the gauge that only
// pushes while somebody is listening. Nothing published these events until the
// board raised them from the platform; the host suite was green because every
// case called on_link_up() by hand.
void ConfigLinkService::drain_link_events(uint32_t now_ms) {
    events::LinkEvent event{};
    while (context_.bus.link_events.pop(event)) {
        const bool up = event.type == events::LinkEventType::Up;
        if (up)
            config_.on_link_up(events::LinkUp{event.session_id, event.payload_bytes});
        else
            config_.on_link_down(events::LinkDown{event.session_id});
        record_link(up ? diag::LinkAction::Up : diag::LinkAction::Down, event.session_id, 0,
                    now_ms);
    }
}

// The whole of the deferral, and it is short because the policy owns the
// arithmetic: a request in, a verdict out, and one write when the verdict says so.
// Taken the moment it is raised, and never held: the blob is read at the instant
// it is written, so a stream of changes arrives here as a stream of requests and
// leaves as one write.
void ConfigLinkService::take_request(uint32_t now_ms) {
    if (!config_.settings_dirty()) return;
    config_.clear_dirty();
    writes_.request(now_ms);
}

// Asked BEFORE the request is taken, so a refusal leaves the change dirty rather
// than pending: a pending change the policy cannot place is one the bound would
// eventually force onto flash, which is exactly the write this refuses. Held
// here, it survives until the cell does or does not come back. Counted once per
// change rather than once per pass, so the number reads as changes waiting.
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
    const timing::DurableWriteVerdict verdict =
        writes_.decide(context_.state.rf.plan, context_.state.rf.dwell, now_ms);
    if (verdict != timing::DurableWriteVerdict::Idle) record_write(verdict, now_ms);
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

void ConfigLinkService::record_write(timing::DurableWriteVerdict verdict, uint32_t now_ms) {
    if (!context_.diag.armed()) return;
    diag::Write value{};
    value.waited_ms = writes_.waited_ms(now_ms);
    value.phase_ms = static_cast<uint16_t>(context_.state.rf.dwell.phase_ms);
    value.requests = writes_.requests();
    value.writes = writes_.writes();
    value.forced = writes_.forced();
    value.placement = verdict;
    value.kind = power::DurableWrite::Settings;
    value.pending = writes_.pending();
    context_.diag.record(value, context_.instant(now_ms));
}

// The deliberate power-off. Same gate: a cell that is collapsing takes the log
// record with it and leaves the settings sector alone, which is the whole of
// core/power's rule. A LowBattery shutdown is therefore the one power-off that
// does not flush, and that is the trade written down in the header - the change a
// pilot was making, against every change they ever made.
void ConfigLinkService::flush_settings(uint32_t now_ms) {
    if (hold_for_power()) return;
    take_request(now_ms);
    if (!writes_.pending()) return;
    write_settings(now_ms, /*forced=*/false);
}

void ConfigLinkService::load() {
    if (loaded_) return;
    load_image_state();
    settings_ = go::defaults();
    if (!storable()) {
        loaded_ = true;
        return;
    }

    uint8_t blob[kBlobCap];
    size_t n = 0;
    if (!is_ok(context_.roles.kv.read("settings", blob, sizeof(blob), n))) return;
    loaded_ = true;
    go::Settings loaded;
    if (is_ok(go::from_blob(blob, n, loaded)) && is_ok(go::validate(loaded))) settings_ = loaded;
    if (n <= kBlobCap) {
        std::memcpy(stored_, blob, n);
        stored_len_ = n;
    }
}

bool ConfigLinkService::persist() {
    if (!storable()) return true;
    uint8_t blob[kBlobCap];
    go::to_blob(settings_, blob, sizeof(blob));
    const size_t len = go::blob_size();
    if (stored_len_ == len && std::memcmp(stored_, blob, len) == 0) return true;
    if (!is_ok(context_.roles.kv.write("settings", blob, len))) return false;
    std::memcpy(stored_, blob, len);
    stored_len_ = len;
    return true;
}

void ConfigLinkService::load_image_state() {
    if (image_state_loaded_) return;
    image_state_loaded_ = true;
    const ports::Capabilities fitted = context_.roles.capabilities;
    const bool has_dfu = ports::has(fitted, ports::Capability::Dfu);
    update_recorded_ = false;
    if (storable()) {
        uint8_t blob[dfu::kUpdateRecordBytes];
        size_t n = 0;
        update_recorded_ = is_ok(context_.roles.kv.read(kUpdateKey, blob, sizeof(blob), n)) &&
                           dfu::from_blob(blob, n, update_record_);
    }
    image_confirmed_ = !has_dfu || context_.roles.dfu.confirmed();
    image_state_ = image_confirmed_ ? dfu::ImageState::Confirmed : dfu::ImageState::Probation;

    ports::ImageVersion running;
    if (update_recorded_) {
        if (!has_dfu || !context_.roles.dfu.running_version(running)) {
            forget_update();
        } else {
            switch (dfu::outcome(update_record_, running)) {
                case dfu::Outcome::Reverted:
                    if (image_confirmed_) image_state_ = dfu::ImageState::Reverted;
                    break;
                case dfu::Outcome::Landed:
                    if (image_confirmed_) forget_update();
                    break;
                case dfu::Outcome::Unrelated: forget_update(); break;
            }
        }
    }
    publish_image_state();
}

void ConfigLinkService::record_update() {
    if (!storable() || !ports::has(context_.roles.capabilities, ports::Capability::Dfu)) return;
    dfu::UpdateRecord record;
    if (!context_.roles.dfu.running_version(record.from)) return;
    if (!context_.roles.dfu.staged_version(record.to)) return;
    uint8_t blob[dfu::kUpdateRecordBytes];
    const size_t n = dfu::to_blob(record, blob, sizeof(blob));
    if (!is_ok(context_.roles.kv.write(kUpdateKey, blob, n))) return;
    update_record_ = record;
    update_recorded_ = true;
}

void ConfigLinkService::forget_update() {
    // INFO: fc 23sep26 a failed erase leaves the record for the next boot, which forgets it again
    if (update_recorded_) (void)context_.roles.kv.erase(kUpdateKey);
    update_recorded_ = false;
    update_record_ = dfu::UpdateRecord{};
}

void ConfigLinkService::publish_image_state() {
    config_.set_image_state(image_state_, update_record_);
}

// INFO: fc 07sep26 a solution need not be a fix: an RMC with status V still proves the UART
bool ConfigLinkService::hardware_proven() const {
    const bus::State& state = context_.state;
    if (!state.started || state.flight.gnss_solutions == 0) return false;
    if (!ports::has(context_.roles.capabilities, ports::Capability::Display)) return true;
    return state.panel_presented;
}

void ConfigLinkService::confirm_image_once_healthy() {
    if (image_confirmed_ || !hardware_proven()) return;
    if (confirm_attempts_ >= kConfirmAttempts) return;
    confirm_attempts_++;
    if (!context_.roles.dfu.confirm()) return;
    image_confirmed_ = true;
    image_state_ = dfu::ImageState::Confirmed;
    forget_update();
    publish_image_state();
}

}  // namespace skyblip::go
