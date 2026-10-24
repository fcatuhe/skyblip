// core/flight/log_session.h: when a flight is worth recording, and where the
// next record goes. Both are policy, both are pure, and neither knows what
// flash is - the service does the writing and this decides what to write.
//
// A device parked on a trailer for a week must not fill the partition with its
// own stillness, so a session is bracketed by core/flight's flight state and by
// nothing else. That state is already the one the transmitter and the update
// lockout obey, so "the log started" and "the aircraft took off" cannot drift
// apart.
#ifndef SKYBLIP_CORE_FLIGHT_LOG_SESSION_H
#define SKYBLIP_CORE_FLIGHT_LOG_SESSION_H

#include "core/flight/log_record.h"
#include "core/flight/state.h"
#include "core/model/ownship.h"

namespace skyblip::flight {

// INFO: fc 15oct26 the ADS-L G.1.16 airborne rate, so the log holds what neighbours were told
constexpr uint32_t kLogRecordPeriodMs = 1000;

// INFO: fl 03aug26 a takeoff is declared at 12 m/s, so the roll is behind the log when it opens
constexpr int kLogPreTakeoffRecords = 60;

// INFO: fc 15oct26 a wing that flies at no ground speed has landed once it has stood this long
constexpr uint32_t kLogStillHoldMs = 120000;

enum class LogAction : uint8_t { Idle, OpenSession, AppendRecord, CloseSession };

// Fed own-ship every service pass. Holds the records it wants written in a
// bounded ring; the writer drains them when the radio is not keying.
class LogSession {
   public:
    LogAction update(const model::Ownship& own, bool rolling, uint32_t now_ms);

    bool recording() const { return open_; }
    // A closed session whose last record has not reached flash yet.
    bool closing() const { return closing_; }
    // The UTC second the session opened, which is also the time base every one
    // of its records is written against and the name the offload protocol uses.
    uint32_t session_id() const { return session_id_; }

    int queued() const { return count_; }
    // Yields records only while a session is running, plus the one last record a
    // landing leaves behind. On the ground the same ring is a holding pen, not a
    // queue: what it holds is overwritten, never written out.
    bool peek(LogRecord& out);
    void commit();

    // Records the ring had to overwrite because nothing drained it. Not silent:
    // the same accounting the bus queues get.
    uint32_t dropped() const { return dropped_; }

    // After an erase there is no history to be the tail of.
    void reset();

   private:
    void push(const LogRecord& record);
    bool flying(const model::Ownship& own, bool rolling, uint32_t now_ms);

    LogRecord ring_[kLogPreTakeoffRecords]{};
    int head_{0};
    int count_{0};
    int to_flush_{0};
    uint32_t dropped_{0};
    uint32_t session_id_{0};
    uint32_t last_sample_ms_{0};
    uint32_t last_rolled_ms_{0};
    bool sampled_{false};
    bool open_{false};
    bool closing_{false};
};

// Where the next record goes: which sector the log is filling and how far into
// it. Which sector that is, and the sequence it was labelled with, is
// core/store's decision - this is the cursor inside it.
class LogRing {
   public:
    void configure(uint32_t sector_count, uint32_t slots_per_sector);

    void restore(uint32_t sector, uint32_t slot);
    void rewind();

    uint32_t sector() const { return sector_; }
    uint32_t slot() const { return slot_; }
    uint32_t sector_count() const { return sector_count_; }
    uint32_t slots_per_sector() const { return slots_per_sector_; }

    bool configured() const { return sector_count_ > 0 && slots_per_sector_ > 0; }
    bool sector_exhausted() const { return slot_ >= slots_per_sector_; }
    void took_slot() { slot_++; }

   private:
    uint32_t sector_count_{0};
    uint32_t slots_per_sector_{0};
    uint32_t sector_{0};
    uint32_t slot_{0};
};

constexpr uint32_t log_seconds_per_sector(uint32_t slots_per_sector) {
    return slots_per_sector * (kLogRecordPeriodMs / 1000);
}
constexpr uint32_t log_seconds_for(uint32_t sector_count, uint32_t slots_per_sector) {
    return sector_count * log_seconds_per_sector(slots_per_sector);
}

}  // namespace skyblip::flight

#endif
