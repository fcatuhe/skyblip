#include "products/skyblip_go/services/record_pool.h"

#include <algorithm>
#include <cstring>

#include "core/events/link.h"
#include "core/flight/log_session.h"
#include "core/timing/durable_write.h"
#include "core/util/span.h"
#include "ports/link.h"

namespace skyblip::go {

bool RecordPool::open() {
    if (opened_) return available_;
    opened_ = true;
    if (!ports::has(context_.roles.capabilities, ports::Capability::Storage) ||
        !context_.roles.log_flash.ready())
        return false;

    sector_bytes_ = context_.roles.log_flash.sector_bytes();
    sector_count_ = context_.roles.log_flash.sector_count();
    slots_per_sector_ = flight::log_slots_per_sector(sector_bytes_);
    const uint32_t floor_sectors =
        store::flights_floor_sectors(flight::log_seconds_per_sector(slots_per_sector_));
    if (slots_per_sector_ == 0 || !allocator_.configure(sector_count_, floor_sectors)) return false;

    available_ = true;
    scan();
    return true;
}

// INFO: fc 20sep26 a sector this build cannot read or cannot parse is quarantined, never reclaimed
void RecordPool::scan() {
    for (uint32_t sector = 0; sector < sector_count_; sector++) {
        store::SectorHeader header{};
        scan_bytes_read_ += store::kSectorHeaderBytes;
        const Status decoded = read_header(sector, header);
        if (decoded == Status::Down) {
            unreadable_sectors_++;
            allocator_.quarantine(sector);
            continue;
        }
        if (decoded == Status::Unsupported) {
            allocator_.quarantine(sector);
            continue;
        }
        if (!is_ok(decoded)) continue;
        if (header.record_bytes != kStoreRecordBytes) {
            allocator_.quarantine(sector);
            continue;
        }
        allocator_.note_sector(sector, header);
    }
}

uint32_t RecordPool::free_sectors() const {
    const uint32_t spoken_for = allocator_.owned(store::SectorOwner::Flights) +
                                allocator_.owned(store::SectorOwner::Diagnostics) +
                                allocator_.quarantined();
    return spoken_for >= sector_count_ ? 0 : sector_count_ - spoken_for;
}

// INFO: fc 03oct26 work starts at the clock or after what this pass booked, whichever is later
bool RecordPool::book_window(uint32_t cost_ms, uint32_t pass_ms) {
    if (!pass_seen_ || pass_ms != pass_ms_) {
        pass_seen_ = true;
        pass_ms_ = pass_ms;
        pass_booked_ms_ = 0;
    }
    const uint32_t into_pass_ms =
        std::max(context_.roles.clock.millis() - pass_ms_, pass_booked_ms_);
    if (!window_open(cost_ms, pass_ms_ + into_pass_ms)) return false;
    pass_booked_ms_ = into_pass_ms + cost_ms;
    return true;
}

bool RecordPool::window_open(uint32_t cost_ms, uint32_t at_ms) const {
    const bus::RfState& rf = context_.state.rf;
    // INFO: fc 20sep26 a plan may allow the PA before any dwell view has been published
    if (rf.plan.tx_allowed) return false;
    return timing::DurableWriteWindow::free_now(rf.plan, rf.dwell, at_ms, cost_ms);
}

Status RecordPool::read_header(uint32_t sector, store::SectorHeader& out) {
    uint8_t raw[store::kSectorHeaderBytes];
    if (!is_ok(context_.roles.log_flash.read(offset_of(sector), raw, sizeof(raw))))
        return Status::Down;
    return store::decode_sector_header(raw, out);
}

bool RecordPool::write_header(uint32_t sector, const store::SectorHeader& header) {
    uint8_t raw[store::kSectorHeaderBytes];
    store::encode_sector_header(header, raw);
    return noted(is_ok(context_.roles.log_flash.write(offset_of(sector), raw, sizeof(raw))));
}

bool RecordPool::read_slot(uint32_t sector, uint32_t slot, uint8_t* out) {
    return is_ok(context_.roles.log_flash.read(offset_of(sector) + flight::log_record_offset(slot),
                                               out, kStoreRecordBytes));
}

bool RecordPool::write_slots(uint32_t sector, uint32_t first, const uint8_t* in, uint32_t count) {
    return noted(is_ok(context_.roles.log_flash.write(
        offset_of(sector) + flight::log_record_offset(first), in, count * kStoreRecordBytes)));
}

bool RecordPool::erase(uint32_t sector) {
    return noted(is_ok(context_.roles.log_flash.erase_sector(sector)));
}

bool RecordPool::noted(bool ok) {
    if (!ok) faults_++;
    return ok;
}

int RecordPool::payload_bytes(uint16_t to) const {
    return static_cast<int>(context_.roles.link.payload_bytes_to(to));
}

int RecordPool::reply_cap(uint16_t to) const {
    const int room = payload_bytes(to) + 1;
    return room < comms::kLogReplyCap ? room : comms::kLogReplyCap;
}

bool RecordPool::send(uint16_t to, int len) {
    if (len <= 0 || len > payload_bytes(to) || holding()) {
        link_drops_++;
        return false;
    }
    const Status sent = context_.roles.link.send_to(
        to, events::Endpoint::Log,
        ConstByteSpan(reinterpret_cast<const uint8_t*>(reply_), static_cast<size_t>(len)));
    if (sent == Status::WouldBlock) {
        std::memcpy(held_, reply_, static_cast<size_t>(len));
        held_len_ = len;
        held_to_ = to;
        held_since_ms_ = now_ms_;
        return true;
    }
    if (is_ok(sent)) return true;
    link_drops_++;
    return false;
}

Status RecordPool::deliver_held(uint32_t now_ms) {
    now_ms_ = now_ms;
    if (!holding()) return Status::Ok;
    const Status sent = context_.roles.link.send_to(
        held_to_, events::Endpoint::Log,
        ConstByteSpan(reinterpret_cast<const uint8_t*>(held_), static_cast<size_t>(held_len_)));
    if (sent == Status::WouldBlock && now_ms - held_since_ms_ < ports::kReplyHoldMs)
        return Status::WouldBlock;
    held_len_ = 0;
    if (is_ok(sent)) return Status::Ok;
    link_drops_++;
    return sent == Status::WouldBlock ? Status::Timeout : sent;
}

}  // namespace skyblip::go
