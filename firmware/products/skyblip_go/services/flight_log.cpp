#include "products/skyblip_go/services/flight_log.h"

#include "core/events/link.h"

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

    const flight::LogAction action = session_.update(context_.state.own, now_ms);
    if (action == flight::LogAction::OpenSession) flights_.begin_session(session_.session_id());
    if (action == flight::LogAction::CloseSession) flights_.end_session();
    drain(now_ms);
    if (flights_.index_stale() && !session_.closing()) flights_.rebuild_index();
}

// INFO: fc 20sep26 the record leaves the ring once flash has it, so a refused write loses nothing
void FlightLogService::drain(uint32_t now_ms) {
    flight::LogRecord record{};
    while (session_.peek(record)) {
        flight::encode_log_record(record, flights_.base_session(), scratch_);
        if (flights_.append(scratch_, now_ms) != Append::Ok) break;
        session_.commit();
    }
    if (session_.open()) flights_.prepare_spare(now_ms);
}

bool FlightLogService::on_ground() const {
    return context_.state.flight.confirmed_state == flight::FlightState::OnGround;
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
