#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RECORD_STORE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RECORD_STORE_H

#include "core/comms/log_link.h"
#include "core/diag/record.h"
#include "core/flight/log_record.h"
#include "core/flight/log_session.h"
#include "core/store/sector_allocator.h"
#include "core/timing/durable_write.h"
#include "runtime/service.h"

namespace skyblip::go {

// INFO: fc 20sep26 both rings write 24-byte slots, so the partition has one slot geometry
static_assert(flight::kLogRecordBytes == diag::kRecordBytes,
              "the two rings share a partition and must share its slot size");
constexpr uint32_t kStoreRecordBytes = flight::kLogRecordBytes;

// INFO: fc 20sep26 budgets for the external NOR on spi1, bench-settled, not datasheet figures
constexpr uint32_t kSectorEraseCostMs = 40;
constexpr uint32_t kSlotWriteCostMs = 2;

// INFO: fc 20sep26 a bulk erase still owes the dwell map its re-arm, so it goes a window at a time
constexpr uint32_t kEraseCeilingSectors = 8;

static_assert(kSectorEraseCostMs + 2 * kSlotWriteCostMs +
                      static_cast<uint32_t>(timing::kJitterGuardMs) <
                  static_cast<uint32_t>(timing::kUplinkRxEnd - timing::kUplinkRxStart),
              "claiming a sector no longer fits inside the narrowest dwell the map offers");

enum class Append : uint8_t { Ok, Deferred, NoSector, Fault };

class RecordPool {
   public:
    explicit RecordPool(runtime::Context& context) : context_(context) {}

    bool open();
    bool available() const { return available_; }

    store::SectorAllocator& allocator() { return allocator_; }
    const store::SectorAllocator& allocator() const { return allocator_; }

    uint32_t sector_count() const { return sector_count_; }
    uint32_t slots_per_sector() const { return slots_per_sector_; }
    uint32_t free_sectors() const;
    uint32_t scan_bytes_read() const { return scan_bytes_read_; }
    uint32_t unreadable_sectors() const { return unreadable_sectors_; }
    uint32_t faults() const { return faults_; }

    bool window_open(uint32_t cost_ms, uint32_t now_ms) const;

    bool read_slot(uint32_t sector, uint32_t slot, uint8_t* out);
    bool write_slot(uint32_t sector, uint32_t slot, const uint8_t* in);
    Status read_header(uint32_t sector, store::SectorHeader& out);
    bool write_header(uint32_t sector, const store::SectorHeader& header);
    bool erase(uint32_t sector);

    int payload_bytes(uint16_t to) const;
    int reply_cap(uint16_t to) const;
    char* reply_buffer() { return reply_; }
    uint8_t* chunk_buffer() { return chunk_; }
    // False when the frame was dropped. A link at its share holds it for deliver_held().
    bool send(uint16_t to, int len);
    // Ok once nothing is held, WouldBlock while the link still refuses, anything else a drop.
    Status deliver_held(uint32_t now_ms);
    bool holding() const { return held_len_ > 0; }
    uint32_t link_drops() const { return link_drops_; }

   private:
    void scan();
    bool noted(bool ok);
    uint32_t offset_of(uint32_t sector) const { return sector * sector_bytes_; }

    runtime::Context& context_;
    store::SectorAllocator allocator_{};
    uint32_t sector_bytes_{0};
    uint32_t sector_count_{0};
    uint32_t slots_per_sector_{0};
    uint32_t scan_bytes_read_{0};
    uint32_t unreadable_sectors_{0};
    uint32_t faults_{0};
    uint32_t link_drops_{0};
    bool opened_{false};
    bool available_{false};

    char reply_[comms::kLogReplyCap]{};
    char held_[comms::kLogReplyCap]{};
    int held_len_{0};
    uint16_t held_to_{0};
    uint32_t held_since_ms_{0};
    uint32_t now_ms_{0};
    uint8_t chunk_[comms::kLogChunkRawBytes]{};
};

class RecordStore {
   public:
    static constexpr int kMaxSessions = 16;

    struct SessionInfo {
        uint32_t session_id{0};
        uint32_t sectors{0};
        uint32_t records{0};
        bool closed{false};
        bool truncated{false};
    };

    RecordStore(RecordPool& pool, store::SectorOwner owner) : pool_(pool), owner_(owner) {}

    bool open();
    bool available() const { return available_; }

    RecordPool& pool() { return pool_; }
    const RecordPool& pool() const { return pool_; }

    void begin_session(uint32_t session_id);
    Append append(const uint8_t* record, uint32_t now_ms);
    void end_session() { index_stale_ = true; }

    // INFO: fc 20sep26 records the ring recycled under the session being written, once each
    uint32_t take_lost_records();
    bool index_stale() const { return index_stale_; }
    void rebuild_index();

    void prepare_spare(uint32_t now_ms);
    bool room_beyond_this_sector();
    uint32_t slots_left() const;

    void begin_erase();
    bool erasing() const { return erasing_; }
    void step_erase(uint32_t now_ms);

    uint32_t records_written() const { return records_written_; }
    uint32_t session_records() const { return session_records_; }
    uint32_t sectors_owned() const { return pool_.allocator().owned(owner_); }
    uint32_t sessions() const { return session_count_; }
    uint32_t sessions_within(uint32_t sectors) const;
    const flight::LogRing& ring() const { return ring_; }
    uint32_t base_session() const { return claimed_ ? claimed_session_ : session_id_; }

    void serve(const comms::LogRequest& request);
    // INFO: fc 25sep26 chunks go out as the link takes them, not all in the pass that asked
    void continue_read();
    void abandon_read() { reading_.active = false; }
    bool reading() const { return reading_.active; }
    void ack(uint16_t to, bool ok, const char* reason);

   private:
    struct Tail {
        uint32_t records{0};
        bool closed{false};
    };

    struct Reading {
        bool active{false};
        uint16_t to{0};
        uint32_t session{0};
        comms::LogWindow window{};
        int next{0};
    };

    void recover();
    const SessionInfo* find(uint32_t session_id) const;
    void note_session(uint32_t session_id);
    uint32_t frontier_slot(uint32_t sector);
    Tail tail_of(uint32_t sector, uint32_t session_id);
    Tail diagnostics_tail_of(uint32_t sector);
    bool erasable(uint32_t sector) const;
    Append claim_sector(uint32_t session_id);
    uint32_t append_cost_ms(bool claim_wanted) const;
    bool room_in_window(uint32_t cost_ms, uint32_t now_ms);
    void answer_list(const comms::LogRequest& request);
    void answer_read(const comms::LogRequest& request);
    bool read_records(uint32_t session_id, uint32_t from, int count);
    void reply(uint16_t to, int len);
    comms::LogStore selector() const { return owner_; }

    RecordPool& pool_;
    const store::SectorOwner owner_;
    flight::LogRing ring_{};

    SessionInfo index_[kMaxSessions]{};
    Reading reading_{};
    uint32_t session_count_{0};
    uint32_t records_written_{0};
    uint32_t session_records_{0};
    uint32_t lost_sectors_seen_{0};
    uint32_t pass_ms_{0};
    uint32_t pass_cost_ms_{0};
    bool pass_seen_{false};
    uint32_t erase_next_{0};
    uint32_t session_id_{0};
    uint32_t claimed_session_{0};
    bool index_truncated_{false};
    bool index_stale_{false};
    bool claimed_{false};
    bool available_{false};
    bool erasing_{false};
    bool spare_ready_{false};

    uint8_t scratch_[kStoreRecordBytes]{};
};

}  // namespace skyblip::go

#endif
