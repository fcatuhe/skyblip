#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RECORD_STORE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RECORD_STORE_H

#include "core/comms/log_link.h"
#include "core/flight/log_session.h"
#include "core/timing/durable_write.h"
#include "products/skyblip_go/services/record_pool.h"

namespace skyblip::go {

// INFO: fc 03oct26 MX25R tSE 40 ms typ (prj.conf), +1 ms poll, +commands; 240 max fits no dwell
constexpr uint32_t kSectorEraseCostMs = 42;
// INFO: fc 03oct26 one page program on the spi1 NOR, bench-settled, not a datasheet figure
constexpr uint32_t kSlotWriteCostMs = 2;

// INFO: fc 03oct26 both candidate parts program 256 B pages: the driver splits a write at each
constexpr uint32_t kNorPageBytes = 256;
// INFO: fc 03oct26 the record that straddles into a page, then the ten the page holds
constexpr uint32_t kRunMostSlots = (kNorPageBytes + kStoreRecordBytes - 1) / kStoreRecordBytes;

// INFO: fc 20sep26 a bulk erase still owes the dwell map its re-arm, so it goes a window at a time
constexpr uint32_t kEraseCeilingSectors = 8;

static_assert(kSectorEraseCostMs + 2 * kSlotWriteCostMs +
                      static_cast<uint32_t>(timing::kJitterGuardMs) <
                  static_cast<uint32_t>(timing::kUplinkRxEnd - timing::kUplinkRxStart),
              "claiming a sector no longer fits inside the narrowest dwell the map offers");

enum class Append : uint8_t { Ok, Deferred, NoSector, Fault };

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
    Append append(const uint8_t* record, uint32_t now_ms) { return append(record, 1, now_ms); }
    Append append(const uint8_t* records, uint32_t count, uint32_t now_ms);
    uint32_t run_slots() const;
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
    void note_session(const store::SessionRun& run);
    uint32_t frontier_slot(uint32_t sector);
    Tail tail_of(uint32_t sector, uint32_t session_id);
    Tail diagnostics_tail_of(uint32_t sector);
    bool erasable(uint32_t sector) const;
    Append claim_sector(uint32_t session_id);
    uint32_t append_cost_ms(bool claim_wanted, uint32_t count) const;
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
