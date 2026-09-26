#include "products/skyblip_go/services/record_store.h"

#include <cstring>

#include "core/events/link.h"
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

bool RecordPool::window_open(uint32_t cost_ms, uint32_t now_ms) const {
    const bus::RfState& rf = context_.state.rf;
    // INFO: fc 20sep26 a plan may allow the PA before any dwell view has been published
    if (rf.plan.tx_allowed) return false;
    return timing::DurableWriteWindow::free_now(rf.plan, rf.dwell, now_ms, cost_ms);
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

bool RecordPool::write_slot(uint32_t sector, uint32_t slot, const uint8_t* in) {
    return noted(is_ok(context_.roles.log_flash.write(
        offset_of(sector) + flight::log_record_offset(slot), in, kStoreRecordBytes)));
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

bool RecordStore::open() {
    pool_.open();
    available_ = pool_.available();
    if (!available_) return false;
    ring_.configure(pool_.sector_count(), pool_.slots_per_sector());
    recover();
    rebuild_index();
    return true;
}

void RecordStore::recover() {
    uint32_t sector = 0;
    uint32_t sequence = 0;
    claimed_ = false;
    if (!pool_.allocator().frontier(owner_, sector, sequence)) {
        ring_.rewind();
        return;
    }
    ring_.restore(sector, ring_.slots_per_sector());
}

uint32_t RecordStore::frontier_slot(uint32_t sector) {
    uint32_t low = 0;
    uint32_t high = ring_.slots_per_sector();
    while (low < high) {
        const uint32_t mid = low + (high - low) / 2;
        if (!pool_.read_slot(sector, mid, scratch_)) return low;
        if (flight::log_slot_erased(scratch_, kStoreRecordBytes))
            high = mid;
        else
            low = mid + 1;
    }
    return low;
}

RecordStore::Tail RecordStore::tail_of(uint32_t sector, uint32_t session_id) {
    if (owner_ != store::SectorOwner::Flights) return diagnostics_tail_of(sector);
    Tail tail{};
    tail.records = frontier_slot(sector);
    if (tail.records == 0) return tail;
    if (!pool_.read_slot(sector, tail.records - 1, scratch_)) return tail;
    flight::LogRecord record{};
    const Status decoded = flight::decode_log_record(scratch_, session_id, record);
    if (decoded == Status::Crc)
        tail.records--;
    else if (is_ok(decoded))
        tail.closed = record.session_end;
    return tail;
}

// INFO: fc 20sep26 no CRC on a diagnostics slot: only the end marker says the tail is whole
RecordStore::Tail RecordStore::diagnostics_tail_of(uint32_t sector) {
    Tail tail{};
    tail.records = frontier_slot(sector);
    if (tail.records == 0) return tail;
    if (!pool_.read_slot(sector, tail.records - 1, scratch_)) return tail;
    diag::Record record{};
    tail.closed = is_ok(diag::decode_record(scratch_, record)) && record.type == diag::Type::End;
    return tail;
}

void RecordStore::note_session(uint32_t session_id) {
    if (session_count_ > 0 && index_[session_count_ - 1].session_id == session_id) {
        index_[session_count_ - 1].sectors++;
        return;
    }
    if (session_count_ == kMaxSessions) {
        for (int i = 1; i < kMaxSessions; i++) index_[i - 1] = index_[i];
        session_count_--;
        index_truncated_ = true;
    }
    SessionInfo& entry = index_[session_count_++];
    entry = SessionInfo{};
    entry.session_id = session_id;
    entry.sectors = 1;
}

void RecordStore::rebuild_index() {
    session_count_ = 0;
    index_truncated_ = false;
    index_stale_ = false;
    if (!available_) return;

    uint32_t sector = 0;
    uint32_t sequence = 0;
    uint32_t walked = 0;
    while (pool_.allocator().next_sector(owner_, walked, sector, sequence)) {
        note_session(pool_.allocator().session_of(sector));
        walked = sequence;
    }

    const uint32_t slots = ring_.slots_per_sector();
    for (uint32_t i = 0; i < session_count_; i++) {
        SessionInfo& entry = index_[i];
        uint32_t last_sector = 0;
        if (!pool_.allocator().session_sector(owner_, entry.session_id, entry.sectors - 1,
                                              last_sector))
            continue;
        const Tail tail = tail_of(last_sector, entry.session_id);
        entry.closed = tail.closed;
        entry.records = (entry.sectors - 1) * slots + tail.records;

        uint32_t first_sector = 0;
        if (pool_.allocator().session_sector(owner_, entry.session_id, 0, first_sector))
            entry.truncated = !pool_.allocator().session_start(first_sector);
    }
}

Append RecordStore::claim_sector(uint32_t session_id) {
    claimed_ = false;
    const store::Claim claim = pool_.allocator().claim(owner_, session_id);
    spare_ready_ = false;
    if (!claim.granted) return Append::NoSector;
    if (!claim.erased && !pool_.erase(claim.sector)) return Append::Fault;
    ring_.restore(claim.sector, 0);

    store::SectorHeader header{};
    header.owner = owner_;
    header.sequence = claim.sequence;
    header.session_id = session_id;
    header.record_bytes = static_cast<uint8_t>(kStoreRecordBytes);
    header.session_start = claim.session_start;
    if (!pool_.write_header(claim.sector, header)) return Append::Fault;

    claimed_ = true;
    claimed_session_ = session_id;
    return Append::Ok;
}

// INFO: fc 20sep26 the claim waits for the first record: opening a session must not touch flash
void RecordStore::begin_session(uint32_t session_id) {
    session_id_ = session_id;
    session_records_ = 0;
    lost_sectors_seen_ = pool_.allocator().lost_sectors(owner_);
    claimed_ = false;
}

Append RecordStore::append(const uint8_t* record, uint32_t now_ms) {
    if (!available_) return Append::Fault;
    const bool claim_wanted = !claimed_ || ring_.sector_exhausted();
    if (!room_in_window(append_cost_ms(claim_wanted), now_ms)) return Append::Deferred;
    if (claim_wanted) {
        const Append claimed = claim_sector(base_session());
        if (claimed != Append::Ok) return claimed;
    }
    if (!pool_.write_slot(ring_.sector(), ring_.slot(), record)) return Append::Fault;
    ring_.took_slot();
    records_written_++;
    session_records_++;
    return Append::Ok;
}

uint32_t RecordStore::append_cost_ms(bool claim_wanted) const {
    if (!claim_wanted) return kSlotWriteCostMs;
    const uint32_t erase_ms = spare_ready_ ? 0 : kSectorEraseCostMs;
    return erase_ms + 2 * kSlotWriteCostMs;
}

// INFO: fc 20sep26 one published phase per pass, so the pass spends one window rather than many
bool RecordStore::room_in_window(uint32_t cost_ms, uint32_t now_ms) {
    if (!pass_seen_ || now_ms != pass_ms_) {
        pass_seen_ = true;
        pass_ms_ = now_ms;
        pass_cost_ms_ = 0;
    }
    if (!pool_.window_open(pass_cost_ms_ + cost_ms, now_ms)) return false;
    pass_cost_ms_ += cost_ms;
    return true;
}

uint32_t RecordStore::take_lost_records() {
    const uint32_t lost = pool_.allocator().lost_sectors(owner_);
    const uint32_t since = lost - lost_sectors_seen_;
    lost_sectors_seen_ = lost;
    return since * ring_.slots_per_sector();
}

void RecordStore::prepare_spare(uint32_t now_ms) {
    if (!available_) return;
    if (spare_ready_) return;
    if (!room_in_window(kSectorEraseCostMs, now_ms)) return;
    const store::Claim spare = pool_.allocator().prepare(owner_);
    if (!spare.granted) return;
    if (!pool_.erase(spare.sector)) return;
    pool_.allocator().note_erased(owner_);
    spare_ready_ = true;
}

bool RecordStore::room_beyond_this_sector() {
    if (!available_) return false;
    return spare_ready_ || pool_.allocator().prepare(owner_).granted;
}

uint32_t RecordStore::slots_left() const {
    if (!claimed_ || ring_.sector_exhausted()) return 0;
    return ring_.slots_per_sector() - ring_.slot();
}

void RecordStore::begin_erase() {
    if (!available_) return;
    erasing_ = true;
    erase_next_ = 0;
}

bool RecordStore::erasable(uint32_t sector) const {
    if (pool_.allocator().quarantined(sector)) return false;
    const store::SectorOwner owner = pool_.allocator().owner_of(sector);
    return owner == store::SectorOwner::None || owner == owner_;
}

void RecordStore::step_erase(uint32_t now_ms) {
    for (uint32_t erased = 0; erased < kEraseCeilingSectors; erased++) {
        while (erase_next_ < pool_.sector_count() && !erasable(erase_next_)) erase_next_++;
        if (erase_next_ >= pool_.sector_count()) break;
        if (!room_in_window(kSectorEraseCostMs, now_ms)) return;
        pool_.erase(erase_next_);
        erase_next_++;
    }
    if (erase_next_ < pool_.sector_count()) return;
    erasing_ = false;
    pool_.allocator().release(owner_);
    ring_.rewind();
    claimed_ = false;
    spare_ready_ = false;
    session_count_ = 0;
    session_id_ = 0;
    index_truncated_ = false;
    index_stale_ = false;
    records_written_ = 0;
    session_records_ = 0;
}

const RecordStore::SessionInfo* RecordStore::find(uint32_t session_id) const {
    for (uint32_t i = 0; i < session_count_; i++)
        if (index_[i].session_id == session_id) return &index_[i];
    return nullptr;
}

uint32_t RecordStore::sessions_within(uint32_t sectors) const {
    uint32_t taken = 0;
    uint32_t whole = 0;
    for (uint32_t i = 0; i < session_count_; i++) {
        taken += index_[i].sectors;
        if (taken > sectors) break;
        whole++;
    }
    return whole;
}

void RecordStore::reply(uint16_t to, int len) { pool_.send(to, len); }

void RecordStore::ack(uint16_t to, bool ok, const char* reason) {
    reply(to,
          comms::format_log_ack(pool_.reply_buffer(), pool_.reply_cap(to), ok, reason, selector()));
}

void RecordStore::serve(const comms::LogRequest& request) {
    switch (request.command) {
        case comms::LogCommand::List: answer_list(request); break;
        case comms::LogCommand::Read: answer_read(request); break;
        case comms::LogCommand::Erase:
        case comms::LogCommand::None: ack(request.link_session, false, "unknown_cmd"); break;
    }
}

void RecordStore::answer_list(const comms::LogRequest& request) {
    const uint16_t to = request.link_session;
    if (!request.index_valid) {
        rebuild_index();
        reply(to, comms::format_log_count(pool_.reply_buffer(), pool_.reply_cap(to), session_count_,
                                          index_truncated_, selector()));
        return;
    }
    if (request.index >= session_count_) {
        ack(to, false, "no_session");
        return;
    }
    const SessionInfo& entry = index_[request.index];
    reply(to, comms::format_log_session(pool_.reply_buffer(), pool_.reply_cap(to), request.index,
                                        session_count_, entry.session_id, entry.records,
                                        entry.closed, entry.truncated, selector()));
}

void RecordStore::answer_read(const comms::LogRequest& request) {
    const uint16_t to = request.link_session;
    const SessionInfo* entry = find(request.session);
    if (entry == nullptr) {
        rebuild_index();
        entry = find(request.session);
    }
    if (entry == nullptr) {
        ack(to, false, "no_session");
        return;
    }
    const int per_chunk = comms::log_records_per_chunk(pool_.payload_bytes(to), selector());
    if (per_chunk == 0) {
        ack(to, false, "payload");
        return;
    }
    if (ring_.slots_per_sector() == 0 || ring_.sector_count() == 0) {
        ack(to, false, "no_storage");
        return;
    }

    const comms::LogWindow window =
        comms::plan_log_window(request.from, request.count, per_chunk, entry->records);
    if (window.chunks == 0) {
        reply(to,
              comms::format_log_chunk(pool_.reply_buffer(), pool_.reply_cap(to), request.session,
                                      request.from, pool_.chunk_buffer(), 0, true, selector()));
        return;
    }
    reading_ = Reading{true, to, request.session, window, 0};
    continue_read();
}

void RecordStore::continue_read() {
    while (reading_.active && !pool_.holding()) {
        const uint16_t to = reading_.to;
        const comms::LogChunkSpan span = reading_.window.at(reading_.next);
        if (!read_records(reading_.session, span.from, span.records)) {
            reading_.active = false;
            ack(to, false, "read_failed");
            return;
        }
        const bool sent = pool_.send(
            to, comms::format_log_chunk(pool_.reply_buffer(), pool_.reply_cap(to), reading_.session,
                                        span.from, pool_.chunk_buffer(), span.records, span.eof,
                                        selector()));
        reading_.next++;
        if (!sent || reading_.next == reading_.window.chunks) reading_.active = false;
    }
}

bool RecordStore::read_records(uint32_t session_id, uint32_t from, int count) {
    const uint32_t slots = ring_.slots_per_sector();
    uint32_t sector = 0;
    for (int i = 0; i < count; i++) {
        const uint32_t index = from + static_cast<uint32_t>(i);
        if (i == 0 || index % slots == 0) {
            if (!pool_.allocator().session_sector(owner_, session_id, index / slots, sector))
                return false;
        }
        if (!pool_.read_slot(sector, index % slots,
                             pool_.chunk_buffer() + static_cast<size_t>(i) * kStoreRecordBytes))
            return false;
    }
    return true;
}

}  // namespace skyblip::go
