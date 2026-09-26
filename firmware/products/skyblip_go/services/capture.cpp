#include "products/skyblip_go/services/capture.h"

namespace skyblip::go {

Status CaptureService::setup() {
    store_.open();
    return Status::Ok;
}

void CaptureService::tick(uint32_t now_ms) {
    if (context_.diag.armed() && !open_) open(now_ms);
    if (open_) {
        if (!context_.diag.armed() && stopped_ == bus::CaptureStop::None)
            stopped_ = bus::CaptureStop::Pilot;
        drain(now_ms);
        if (!context_.diag.armed() && context_.diag.queued() == 0) close(now_ms);
    }
    if (publish_due(now_ms)) publish(now_ms);
}

bool CaptureService::publish_due(uint32_t now_ms) const {
    const bus::CaptureState& published = context_.state.capture;
    if (!published_ || published.armed != open_ || published.stopped != stopped_) return true;
    return now_ms - published_ms_ >= kPublishPeriodMs;
}

void CaptureService::open(uint32_t now_ms) {
    stopped_ = bus::CaptureStop::None;
    if (!store_.available()) {
        context_.diag.disarm();
        stopped_ = bus::CaptureStop::NoStorage;
        return;
    }
    session_id_ = context_.state.traffic_now(now_ms);
    opened_ms_ = now_ms;
    steady_records_ = 0;
    steady_written_ms_ = now_ms;
    open_ = true;
    ended_ = false;
    lost_records_ = 0;
    store_.begin_session(session_id_);
    record_boot(now_ms);
    record_config(now_ms);
}

// INFO: fc 20sep26 a capture is armed long after setup(), so the corpus names its build here
void CaptureService::record_boot(uint32_t now_ms) {
    diag::Boot value{};
    value.capabilities = static_cast<uint32_t>(context_.roles.capabilities);
    ports::ImageVersion running{};
    if (context_.roles.dfu.running_version(running)) {
        value.fw_build = running.build;
        value.fw_revision = running.revision;
        value.fw_major = running.major;
        value.fw_minor = running.minor;
    }
    value.reset = config_.reset_reason();
    value.image_state = config_.image_state();
    context_.diag.record(value, context_.instant(now_ms));
}

void CaptureService::record_config(uint32_t now_ms) {
    diag::Config value{};
    value.addr = context_.roles.device_addr;
    value.battery_offset_mv = settings_.battery_offset_mv;
    value.freq_trim_e1_ppm = settings_.freq_trim_e1_ppm;
    value.aircraft_type = settings_.aircraft_type;
    value.addr_table = settings::kAddrTableSkyblip;
    value.alarm_volume = settings_.alarm_volume;
    value.settings_version = settings_.version;
    value.alarm_enabled = settings_.alarm_enabled;
    value.metric = settings_.units == Units::Metric;
    value.battery_trim_manual = settings_.battery_offset_manual;
    context_.diag.record(value, context_.instant(now_ms));
}

bool CaptureService::close(uint32_t now_ms) {
    if (!write_end(now_ms)) return false;
    open_ = false;
    store_.end_session();
    store_.rebuild_index();
    return true;
}

bool CaptureService::write_end(uint32_t now_ms) {
    if (ended_) return true;
    diag::End end{};
    end.records = store_.session_records();
    end.dropped = context_.diag.dropped();
    diag::encode_record(diag::record_of(end, context_.instant(now_ms)), scratch_);
    if (store_.append(scratch_, now_ms) == Append::Deferred) return false;
    ended_ = true;
    return true;
}

void CaptureService::drain(uint32_t now_ms) {
    uint32_t written = 0;
    while (context_.diag.queued() > 0 && written < kDrainCeilingRecords) {
        if (store_.slots_left() <= kTailSlotsReserved && !store_.room_beyond_this_sector()) {
            stop_on_refusal(now_ms);
            return;
        }
        diag::Record record{};
        if (!context_.diag.peek(record)) return;
        diag::encode_record(record, scratch_);
        switch (store_.append(scratch_, now_ms)) {
            case Append::Deferred: return;
            case Append::NoSector: stop_on_refusal(now_ms); return;
            case Append::Fault: return;
            case Append::Ok: break;
        }
        context_.diag.commit();
        count_toward_rate(record.type, now_ms);
        written++;
        if (!announce_rotation(now_ms)) return;
    }
    store_.prepare_spare(now_ms);
}

// INFO: fc 20sep26 the ring recycled under the session: say what went, then name the build again
bool CaptureService::announce_rotation(uint32_t now_ms) {
    lost_records_ += store_.take_lost_records();
    if (lost_records_ == 0) return true;
    diag::Gap gap{};
    gap.dropped = lost_records_;
    gap.total = context_.diag.dropped() + lost_records_;
    gap.capacity = static_cast<uint16_t>(diag::Recorder::kCapacity);
    diag::encode_record(diag::record_of(gap, context_.instant(now_ms)), scratch_);
    if (store_.append(scratch_, now_ms) != Append::Ok) return false;
    lost_records_ = 0;
    record_boot(now_ms);
    record_config(now_ms);
    return true;
}

void CaptureService::stop_on_refusal(uint32_t now_ms) {
    if (!write_gap(now_ms)) return;
    context_.diag.disarm();
    stopped_ = bus::CaptureStop::NoSectors;
    close(now_ms);
}

bool CaptureService::write_gap(uint32_t now_ms) {
    if (store_.slots_left() == 0) return true;
    diag::Gap gap{};
    gap.dropped = static_cast<uint32_t>(context_.diag.queued());
    gap.total = context_.diag.dropped() + gap.dropped;
    gap.capacity = static_cast<uint16_t>(diag::Recorder::kCapacity);
    diag::encode_record(diag::record_of(gap, context_.instant(now_ms)), scratch_);
    return store_.append(scratch_, now_ms) != Append::Deferred;
}

void CaptureService::park(uint32_t now_ms) {
    if (!open_) return;
    context_.diag.disarm();
    if (stopped_ == bus::CaptureStop::None) stopped_ = bus::CaptureStop::Pilot;
    while (context_.diag.queued() > 0) {
        const uint32_t before = store_.session_records();
        drain(now_ms);
        if (store_.session_records() == before) break;
    }
    close(now_ms);
    publish(now_ms);
}

uint32_t CaptureService::growth_sectors() const {
    const RecordPool& pool = store_.pool();
    const uint32_t floor_sectors =
        store::flights_floor_sectors(flight::log_seconds_per_sector(pool.slots_per_sector()));
    const uint32_t flights = pool.allocator().owned(store::SectorOwner::Flights);
    const uint32_t from_flights = flights > floor_sectors ? flights - floor_sectors : 0;
    return pool.free_sectors() + from_flights + store_.sectors_owned();
}

uint32_t CaptureService::growth_slots() const {
    return growth_sectors() * store_.pool().slots_per_sector();
}

uint32_t CaptureService::keeps_s(diag::Profile profile) const {
    return span_s(growth_slots(), diag::Recorder::records_per_hour(profile));
}

uint32_t CaptureService::span_s(uint32_t slots, uint32_t records_per_hour) {
    if (records_per_hour == 0) return 0;
    return static_cast<uint32_t>(static_cast<uint64_t>(slots) * diag::Recorder::kSecondsPerHour /
                                 records_per_hour);
}

void CaptureService::count_toward_rate(diag::Type type, uint32_t now_ms) {
    if (!diag::recurs(type)) return;
    steady_records_++;
    steady_written_ms_ = now_ms;
}

uint32_t CaptureService::records_per_hour() const {
    const uint32_t measured = measured_records_per_hour();
    return measured != 0 ? measured : diag::Recorder::records_per_hour(context_.diag.profile());
}

uint32_t CaptureService::measured_records_per_hour() const {
    if (!open_) return 0;
    const uint32_t span_ms = steady_written_ms_ - opened_ms_;
    if (span_ms == 0) return 0;
    return static_cast<uint32_t>(static_cast<uint64_t>(steady_records_) *
                                 diag::Recorder::kMsPerHour / span_ms);
}

void CaptureService::publish(uint32_t now_ms) {
    published_ = true;
    published_ms_ = now_ms;
    bus::CaptureState& out = context_.state.capture;
    const RecordPool& pool = store_.pool();
    out.available = store_.available();
    out.armed = open_;
    out.stopped = stopped_;
    out.session_id = session_id_;
    out.records = store_.session_records();
    out.dropped = context_.diag.dropped();
    out.sectors = store_.sectors_owned();
    out.pool_sectors = pool.sector_count();
    out.faults = pool.faults();
    out.unreadable_sectors = pool.unreadable_sectors();

    const uint32_t growth = growth_sectors();
    out.price_sectors = growth;
    const uint32_t floor_sectors =
        store::flights_floor_sectors(flight::log_seconds_per_sector(pool.slots_per_sector()));
    const uint32_t flights = pool.allocator().owned(store::SectorOwner::Flights);
    out.price_flights =
        flights_.sessions_within(flights > floor_sectors ? flights - floor_sectors : 0);
    // INFO: fc 20sep26 the ring rotates: this is what survives, not a deadline
    out.keeps_s = span_s(growth * pool.slots_per_sector(), records_per_hour());
}

}  // namespace skyblip::go
