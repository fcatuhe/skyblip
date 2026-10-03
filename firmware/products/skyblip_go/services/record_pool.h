#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RECORD_POOL_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RECORD_POOL_H

#include "core/comms/log_link.h"
#include "core/diag/record.h"
#include "core/flight/log_record.h"
#include "core/store/sector_allocator.h"
#include "runtime/service.h"

namespace skyblip::go {

// INFO: fc 20sep26 both rings write 24-byte slots, so the partition has one slot geometry
static_assert(flight::kLogRecordBytes == diag::kRecordBytes,
              "the two rings share a partition and must share its slot size");
constexpr uint32_t kStoreRecordBytes = flight::kLogRecordBytes;

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

    // INFO: fc 03oct26 both rings stall one loop on one bus, so they book one window per pass
    bool book_window(uint32_t cost_ms, uint32_t pass_ms);

    bool read_slot(uint32_t sector, uint32_t slot, uint8_t* out);
    bool write_slots(uint32_t sector, uint32_t first, const uint8_t* in, uint32_t count);
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
    bool window_open(uint32_t cost_ms, uint32_t at_ms) const;
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
    uint32_t pass_ms_{0};
    uint32_t pass_booked_ms_{0};
    bool pass_seen_{false};
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

}  // namespace skyblip::go

#endif
