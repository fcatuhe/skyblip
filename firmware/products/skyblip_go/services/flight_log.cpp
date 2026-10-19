#include "products/skyblip_go/services/flight_log.h"

#include "core/diag/record.h"
#include "core/events/link.h"
#include "core/traffic/alarm.h"

namespace skyblip::go {

Status FlightLogService::setup() {
    flights_.open();
    return Status::Ok;
}

void FlightLogService::tick(uint32_t now_ms) {
    serve_link(now_ms);
    watch_link_drops(now_ms);

    if (config_.log_erase_requested()) {
        config_.clear_log_erase_request();
        flights_.begin_erase();
    }
    if (flights_.erasing()) {
        flights_.step_erase(now_ms);
        if (!flights_.erasing()) {
            session_.reset();
            ack(comms::LogStore::Flights, true, "erased");
        }
        return;
    }
    if (!flights_.available()) return;

    const flight::LogAction action =
        session_.update(context_.state.own, context_.state.flight.rolling, now_ms);
    if (action == flight::LogAction::OpenSession) {
        flights_.begin_session(session_.session_id());
        events_.open();
    }
    if (action == flight::LogAction::CloseSession) {
        flights_.end_session();
        events_.close();
    }
    if (action == flight::LogAction::OpenSession || action == flight::LogAction::AppendRecord)
        watch(now_ms);
    drain(now_ms);
    if (flights_.index_stale() && !session_.closing()) flights_.rebuild_index();
}

// INFO: fc 15oct26 sampled with the fix, so an event is dated by the second the track holds
void FlightLogService::watch(uint32_t now_ms) {
    const model::OwnState& own = context_.state.own;
    events_.status(status_now(), own.utc, now_ms);
    for (int slot = 0; slot < traffic::TrafficTable::kCapacity; slot++) {
        const traffic::Target* target = context_.state.traffic.at(slot);
        if (target == nullptr || !target->used) {
            events_.vacant(slot);
            continue;
        }
        const traffic::AlarmAssessment assessment = traffic::assess(own, target->obs, now_ms);
        if (!assessment.valid) continue;
        flight::LogContact contact{};
        contact.address = target->obs.address;
        contact.address_mapping_table = target->obs.address_mapping_table;
        contact.aircraft_cat = target->obs.aircraft_cat;
        contact.dist_m = diag::clamp_u16(
            assessment.rel_dist_m < 0 ? 0 : static_cast<uint32_t>(assessment.rel_dist_m));
        contact.rel_alt_m = diag::clamp_i16(assessment.rel_alt_m);
        contact.rel_bearing_deg = assessment.rel_bearing_deg;
        contact.closing_mps = diag::clamp_i16(assessment.closing_mps);
        contact.alarm = traffic::to_number(target->alarm_level);
        contact.source = static_cast<uint8_t>(target->obs.source);
        contact.rssi_dbm = target->obs.rssi_dbm;
        contact.dismissed = target->alarm_dismissed;
        contact.in_formation = target->in_formation;
        events_.heard(slot, contact, own.utc);
    }
}

flight::LogStatus FlightLogService::status_now() const {
    const bus::PowerState& power = context_.state.power;
    flight::LogStatus status{};
    status.cell_mv = power.battery.millivolts;
    status.percent = power.battery.percent;
    status.power_level = static_cast<uint8_t>(power.level);
    status.charge = static_cast<uint8_t>(power.charge);
    status.tracked = static_cast<uint8_t>(context_.state.traffic.count());
    status.battery_valid = power.battery.valid;
    status.charging = power.battery.charging;
    status.external_power = power.battery.external_power;
    return status;
}

// INFO: fc 20sep26 the record leaves the ring once flash has it, so a refused write loses nothing
void FlightLogService::drain(uint32_t now_ms) {
    if (!drain_events(now_ms)) return;
    flight::LogRecord record{};
    while (session_.peek(record)) {
        flight::encode_log_record(record, flights_.base_session(), scratch_);
        if (flights_.append(scratch_, now_ms) != Append::Ok) break;
        session_.commit();
    }
    if (session_.open()) flights_.prepare_spare(now_ms);
}

// INFO: fc 15oct26 events go first, so the landing's session_end fix is still the last record
bool FlightLogService::drain_events(uint32_t now_ms) {
    flight::LogEvent event{};
    while (events_.peek(event)) {
        flight::encode_log_event(event, flights_.base_session(), scratch_);
        if (flights_.append(scratch_, now_ms) != Append::Ok) return false;
        events_.commit();
    }
    return true;
}

bool FlightLogService::on_ground() const {
    return context_.state.flight.confirmed_state == flight::FlightState::Ground;
}

void FlightLogService::ack(comms::LogStore store, bool ok, const char* reason) {
    RecordPool& pool = flights_.pool();
    pool.send(reply_to_, comms::format_log_ack(pool.reply_buffer(), pool.reply_cap(reply_to_), ok,
                                               reason, store));
}

RecordStore* FlightLogService::store_for(comms::LogStore store) {
    if (store == comms::LogStore::Flights) return &flights_;
    if (store == comms::LogStore::Diagnostics) return diagnostics_;
    return nullptr;
}

void FlightLogService::serve_link(uint32_t now_ms) {
    const Status held = flights_.pool().deliver_held(now_ms);
    if (held == Status::WouldBlock) return;
    if (!is_ok(held)) abandon_reads();
    flights_.continue_read();
    if (diagnostics_ != nullptr) diagnostics_->continue_read();
    events::RxFrame frame{};
    while (!replying() && context_.bus.log_rx.pop(frame)) {
        handle(comms::parse_log_request(frame));
        record_link(diag::LinkAction::Received, frame.session_id, frame.len, now_ms);
    }
}

void FlightLogService::abandon_reads() {
    flights_.abandon_read();
    if (diagnostics_ != nullptr) diagnostics_->abandon_read();
}

bool FlightLogService::replying() const {
    return flights_.pool().holding() || flights_.reading() ||
           (diagnostics_ != nullptr && diagnostics_->reading());
}

void FlightLogService::watch_link_drops(uint32_t now_ms) {
    if (link_drops() == recorded_drops_) return;
    recorded_drops_ = link_drops();
    record_link(diag::LinkAction::Dropped, reply_to_, 0, now_ms);
}

void FlightLogService::record_link(diag::LinkAction action, uint16_t session, uint16_t frame_bytes,
                                   uint32_t now_ms) {
    if (!context_.diag.armed()) return;
    diag::Link event{};
    event.session = session;
    event.payload_bytes = context_.roles.link.payload_bytes();
    event.frame_bytes = frame_bytes;
    event.holder = config_.claim().holder();
    event.drops = link_drops();
    event.action = action;
    event.endpoint = events::Endpoint::Log;
    event.claim_held = config_.claim().held();
    context_.diag.record(event, context_.instant(now_ms));
}

void FlightLogService::handle(const comms::LogRequest& request) {
    reply_to_ = request.link_session;
    if (!config_.claim_link(request.link_session)) {
        ack(request.store, false, "claimed");
        return;
    }
    if (!request.understood) {
        ack(request.store, false, request.reason == nullptr ? "unknown_cmd" : request.reason);
        return;
    }
    RecordStore* store = store_for(request.store);
    if (store == nullptr) {
        ack(request.store, false, "no_diagnostics");
        return;
    }
    if (!store->available()) {
        ack(request.store, false, "no_storage");
        return;
    }
    if (flights_.erasing()) {
        ack(request.store, false, "busy");
        return;
    }
    if (!on_ground()) {
        ack(request.store, false, "in_flight");
        return;
    }

    if (request.command == comms::LogCommand::Erase) {
        if (request.store != comms::LogStore::Flights) {
            ack(request.store, false, "flights_only");
            return;
        }
        config_.request_log_erase();
        return;
    }
    store->serve(request);
}

}  // namespace skyblip::go
