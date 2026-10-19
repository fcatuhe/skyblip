#include "core/flight/log_session.h"

#include "core/model/ownship.h"

namespace skyblip::flight {

void LogSession::push(const LogRecord& record) {
    if (count_ == kLogPreTakeoffRecords) {
        head_ = (head_ + 1) % kLogPreTakeoffRecords;
        count_--;
        // On the ground the ring is meant to turn over - that is what makes it a
        // pre-takeoff window. Only a record the writer owed and did not take is
        // a loss worth counting.
        if (open_ || closing_) dropped_++;
        if (to_flush_ > 0) to_flush_--;
    }
    ring_[(head_ + count_) % kLogPreTakeoffRecords] = record;
    count_++;
}

// INFO: fc 20sep26 a closed session yields what it held at the landing, never a later ground sample
bool LogSession::peek(LogRecord& out) {
    if (!open_ && to_flush_ == 0) {
        closing_ = false;
        return false;
    }
    if (count_ == 0) {
        closing_ = false;
        return false;
    }
    out = ring_[head_];
    return true;
}

void LogSession::commit() {
    if (count_ == 0) return;
    head_ = (head_ + 1) % kLogPreTakeoffRecords;
    count_--;
    if (to_flush_ > 0) to_flush_--;
    if (!open_ && to_flush_ == 0) closing_ = false;
}

void LogSession::reset() {
    head_ = 0;
    count_ = 0;
    to_flush_ = 0;
    session_id_ = 0;
    sampled_ = false;
    open_ = false;
    closing_ = false;
}

// INFO: fc 15oct26 the radio's full rate would log a parked paraglider for ever, so it moves
// instead
bool LogSession::flying(const model::Ownship& own, bool rolling, uint32_t now_ms) {
    if (airborne(own.flight_state)) return true;
    if (!flies_at_no_ground_speed(own.aircraft_cat)) return false;
    if (rolling) {
        last_rolled_ms_ = now_ms;
        return true;
    }
    return open_ && now_ms - last_rolled_ms_ < kLogStillHoldMs;
}

LogAction LogSession::update(const model::Ownship& own, bool rolling, uint32_t now_ms) {
    const bool in_flight = flying(own, rolling, now_ms);
    // A record with no position or no UTC is a row of zeroes in a flight log.
    // The session survives a fix outage - the aircraft is still where it was -
    // but nothing is written across it.
    const bool usable = own.fix_valid && own.utc_valid;

    if (open_ && !in_flight) {
        if (usable) {
            LogRecord last = log_record_from(own);
            last.session_end = true;
            push(last);
        }
        open_ = false;
        closing_ = true;
        to_flush_ = count_;
        return LogAction::CloseSession;
    }

    if (!usable) return LogAction::Idle;
    if (sampled_ && now_ms - last_sample_ms_ < kLogRecordPeriodMs) return LogAction::Idle;
    last_sample_ms_ = now_ms;
    sampled_ = true;

    const LogRecord record = log_record_from(own);
    if (closing_) return LogAction::Idle;
    if (!in_flight && !open_) {
        // On the ground and staying there: kept in RAM, never written, and
        // overwritten by the next one. This is the whole answer to "the device
        // does not log while it is parked".
        push(record);
        return LogAction::Idle;
    }

    if (!open_) {
        // The oldest sample the ring still holds names the session, so the file
        // begins where the aircraft began moving rather than where the criterion
        // finally agreed. Named after the push, not before it: the push is what
        // decides which sample is the oldest one that survived.
        push(record);
        session_id_ = ring_[head_].utc_s;
        open_ = true;
        closing_ = false;
        return LogAction::OpenSession;
    }

    push(record);
    return LogAction::AppendRecord;
}

void LogRing::configure(uint32_t sector_count, uint32_t slots_per_sector) {
    sector_count_ = sector_count;
    slots_per_sector_ = slots_per_sector;
    rewind();
}

void LogRing::rewind() {
    sector_ = 0;
    slot_ = 0;
}

void LogRing::restore(uint32_t sector, uint32_t slot) {
    sector_ = sector;
    slot_ = slot;
}

}  // namespace skyblip::flight
