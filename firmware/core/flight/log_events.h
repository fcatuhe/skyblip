#ifndef SKYBLIP_CORE_FLIGHT_LOG_EVENTS_H
#define SKYBLIP_CORE_FLIGHT_LOG_EVENTS_H

#include <cstdint>

#include "core/flight/log_record.h"

namespace skyblip::flight {

// INFO: fc 15oct26 the cell and the aircraft heard, once a minute: one record in sixty of the
// track's
constexpr uint32_t kLogStatusPeriodMs = 60000;

// INFO: fc 15oct26 one per traffic table slot, which the service holds equal with a static_assert
constexpr int kLogContactSlots = 48;

// INFO: fc 15oct26 alarms and encounters waiting on a writer that drains every pass
constexpr int kLogEventBacklogRecords = 16;

class LogEvents {
   public:
    void open();
    void status(const LogStatus& now, uint32_t utc_s, uint32_t now_ms);
    void heard(int slot, const LogContact& now, uint32_t utc_s);
    void vacant(int slot);
    void close();

    bool peek(LogEvent& out) const;
    void commit();

    uint32_t dropped() const { return dropped_; }

   private:
    struct Track {
        LogContact closest{};
        uint32_t closest_utc_s{0};
        uint32_t first_utc_s{0};
        uint32_t last_utc_s{0};
        uint8_t alarm{0};
        uint8_t peak_alarm{0};
        bool active{false};
        bool ending{false};
    };

    static bool same_aircraft(const LogContact& a, const LogContact& b);
    static LogEvent encounter_of(const Track& track);
    void end(int slot);
    void push(const LogEvent& event);
    int first_ending() const;

    Track tracks_[kLogContactSlots]{};
    LogEvent backlog_[kLogEventBacklogRecords]{};
    int head_{0};
    int count_{0};
    uint32_t dropped_{0};
    uint32_t status_ms_{0};
    uint8_t status_level_{0};
    uint8_t tracked_peak_{0};
    bool status_due_{false};
    bool open_{false};
};

}  // namespace skyblip::flight

#endif
