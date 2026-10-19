#include "core/flight/log_events.h"

#include <algorithm>

namespace skyblip::flight {

void LogEvents::open() {
    for (Track& track : tracks_) track = Track{};
    head_ = 0;
    count_ = 0;
    tracked_peak_ = 0;
    status_due_ = true;
    open_ = true;
}

void LogEvents::status(const LogStatus& now, uint32_t utc_s, uint32_t now_ms) {
    if (!open_) return;
    tracked_peak_ = std::max(now.tracked, tracked_peak_);
    const bool due = status_due_ || now_ms - last_status_ms_ >= kLogStatusPeriodMs ||
                     now.power_level != status_level_;
    if (!due) return;

    LogEvent event{};
    event.kind = LogKind::Status;
    event.utc_s = utc_s;
    event.status = now;
    event.status.tracked_peak = tracked_peak_;
    push(event);
    last_status_ms_ = now_ms;
    status_level_ = now.power_level;
    tracked_peak_ = now.tracked;
    status_due_ = false;
}

void LogEvents::heard(int slot, const LogContact& now, uint32_t utc_s) {
    if (!open_ || slot < 0 || slot >= kLogContactSlots) return;
    Track& track = tracks_[slot];
    if (track.active && !same_aircraft(track.closest, now)) end(slot);
    if (!track.active) {
        track = Track{};
        track.active = true;
        track.first_utc_s = utc_s;
        track.closest = now;
        track.closest_utc_s = utc_s;
    }
    if (now.alarm != track.alarm) {
        LogEvent event{};
        event.kind = LogKind::Alarm;
        event.utc_s = utc_s;
        event.contact = now;
        push(event);
        track.alarm = now.alarm;
    }
    track.alarm_peak = std::max(now.alarm, track.alarm_peak);
    if (now.distance_m < track.closest.distance_m) {
        track.closest = now;
        track.closest_utc_s = utc_s;
    }
    track.last_utc_s = utc_s;
}

void LogEvents::vacant(int slot) {
    if (!open_ || slot < 0 || slot >= kLogContactSlots) return;
    end(slot);
}

// INFO: fc 15oct26 every encounter still running ends with the flight, drained after the backlog
void LogEvents::close() {
    for (Track& track : tracks_)
        if (track.active) track.ending = true;
    open_ = false;
}

bool LogEvents::peek(LogEvent& out) const {
    if (count_ > 0) {
        out = backlog_[head_];
        return true;
    }
    const int slot = first_ending();
    if (slot < 0) return false;
    out = encounter_of(tracks_[slot]);
    return true;
}

void LogEvents::commit() {
    if (count_ > 0) {
        head_ = (head_ + 1) % kLogEventBacklogRecords;
        count_--;
        return;
    }
    const int slot = first_ending();
    if (slot >= 0) tracks_[slot] = Track{};
}

bool LogEvents::same_aircraft(const LogContact& a, const LogContact& b) {
    return a.address == b.address && a.address_mapping_table == b.address_mapping_table;
}

LogEvent LogEvents::encounter_of(const Track& track) {
    LogEvent event{};
    event.kind = LogKind::Encounter;
    event.utc_s = track.closest_utc_s;
    event.contact = track.closest;
    event.contact.alarm = track.alarm_peak;
    const uint32_t heard_s = track.last_utc_s - track.first_utc_s;
    event.contact.heard_s = static_cast<uint16_t>(heard_s > 0xFFFF ? 0xFFFF : heard_s);
    return event;
}

void LogEvents::end(int slot) {
    Track& track = tracks_[slot];
    if (!track.active) return;
    push(encounter_of(track));
    track = Track{};
}

void LogEvents::push(const LogEvent& event) {
    if (count_ == kLogEventBacklogRecords) {
        head_ = (head_ + 1) % kLogEventBacklogRecords;
        count_--;
        dropped_++;
    }
    backlog_[(head_ + count_) % kLogEventBacklogRecords] = event;
    count_++;
}

int LogEvents::first_ending() const {
    for (int slot = 0; slot < kLogContactSlots; slot++)
        if (tracks_[slot].ending) return slot;
    return -1;
}

}  // namespace skyblip::flight
