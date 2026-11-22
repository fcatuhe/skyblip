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

class LogSession {
   public:
    LogAction update(const model::Ownship& own, bool rolling, uint32_t now_ms);

    bool recording() const { return open_; }
    bool closing() const { return closing_; }
    // INFO: fc 20nov26 the opening UTC second, its records' time base and its name on the link
    uint32_t session_id() const { return session_id_; }

    int queued() const { return count_; }
    // INFO: fc 20nov26 on the ground the ring is a holding pen, overwritten and never written out
    bool peek(uint32_t base_utc_s, uint8_t* out);
    void commit();

    uint32_t dropped() const { return dropped_; }

    void reset();

   private:
    struct Held {
        uint32_t utc_s;
        uint8_t body[kLogFixBodyBytes];
    };

    void push(const LogRecord& record);
    bool flying(const model::Ownship& own, bool rolling, uint32_t now_ms);

    Held ring_[kLogPreTakeoffRecords]{};
    int head_{0};
    int count_{0};
    int to_flush_{0};
    uint32_t dropped_{0};
    uint32_t session_id_{0};
    uint32_t last_sample_ms_{0};
    // INFO: fc 18nov26 one record per UTC second, so a second own-ship never solved writes none
    uint32_t last_sample_s_{0};
    uint32_t last_rolled_ms_{0};
    bool sampled_{false};
    bool open_{false};
    bool closing_{false};
};

// INFO: fc 20nov26 the cursor inside a sector, while core/store chooses the sector and its sequence
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
