#ifndef SKYBLIP_CORE_STORE_SECTOR_ALLOCATOR_H
#define SKYBLIP_CORE_STORE_SECTOR_ALLOCATOR_H

#include <cstdint>

#include "core/store/sector.h"

namespace skyblip::store {

// INFO: fc 15oct26 170 slots at a record a second is 2.8 min a sector, 128 of 298 for six hours
constexpr uint32_t kFlightsFloorHours = 6;

constexpr uint32_t flights_floor_sectors(uint32_t seconds_per_sector) {
    return seconds_per_sector == 0
               ? 0
               : (kFlightsFloorHours * 3600u + seconds_per_sector - 1) / seconds_per_sector;
}

struct SessionRun {
    uint32_t session_id{0};
    uint32_t sectors{0};
    uint32_t last_sequence{0};
};

struct Claim {
    uint32_t sector{0};
    uint32_t sequence{0};
    bool granted{false};
    bool erased{false};
    bool session_start{false};
};

struct SectorTable {
    uint32_t* sequence;
    uint32_t* session_id;
    uint8_t* label;
    uint32_t capacity;
};

// INFO: fc 21nov26 the product holds one sized for its board's partition, a table entry per sector
template <uint32_t Sectors>
struct SectorSlots {
    uint32_t sequences[Sectors]{};
    uint32_t session_ids[Sectors]{};
    uint8_t labels[Sectors]{};

    SectorTable table() { return SectorTable{sequences, session_ids, labels, Sectors}; }
};

class SectorAllocator {
   public:
    explicit SectorAllocator(SectorTable table) : table_(table) {}

    bool configure(uint32_t sector_count, uint32_t floor_sectors);
    void reset();

    void note_sector(uint32_t sector, const SectorHeader& header);

    void quarantine(uint32_t sector);
    bool quarantined(uint32_t sector) const;
    uint32_t quarantined() const;

    Claim claim(SectorOwner owner, uint32_t session_id);
    Claim prepare(SectorOwner owner);
    void note_erased(SectorOwner owner);

    // INFO: fc 20sep26 one ring erased, the other untouched, and the counter still never repeats
    void release(SectorOwner owner);

    bool configured() const { return sector_count_ > 0; }
    uint32_t sequence() const { return sequence_; }
    uint32_t owned(SectorOwner owner) const;
    SectorOwner owner_of(uint32_t sector) const;
    uint32_t session_of(uint32_t sector) const;
    bool owns_session(SectorOwner owner, uint32_t session_id) const;
    bool session_start(uint32_t sector) const;
    uint32_t lost_sectors(SectorOwner owner) const;
    uint32_t recycled_sectors(SectorOwner owner, uint32_t session_id) const;
    bool frontier(SectorOwner owner, uint32_t& sector, uint32_t& sequence) const;
    bool oldest(SectorOwner owner, uint32_t& sector, uint32_t& sequence) const;
    bool session_run(SectorOwner owner, uint32_t above_sequence, SessionRun& run) const;
    bool session_sector(SectorOwner owner, uint32_t session_id, uint32_t index,
                        uint32_t& sector) const;

   private:
    struct Spare {
        uint32_t sector{0};
        bool reserved{false};
        bool erased{false};
    };

    struct Recycled {
        uint32_t session_id{0};
        uint32_t sectors{0};
    };

    struct Match {
        SectorOwner owner{SectorOwner::None};
        bool any_session{true};
        uint32_t session_id{0};
    };

    Spare& spare_of(SectorOwner owner);
    bool choose(SectorOwner owner, uint32_t& sector) const;
    bool next_free(uint32_t& sector) const;
    bool evictable(SectorOwner owner, uint32_t& sector) const;
    bool reserved(uint32_t sector) const;
    void take(uint32_t sector, SectorOwner owner, uint32_t session_id, bool session_start);
    void note_taken(uint32_t sector);
    void note_recycled(SectorOwner owner, uint32_t session_id);
    void forget_recycled(SectorOwner owner, uint32_t session_id);
    uint32_t matching_up_to(const Match& match, uint32_t sequence) const;
    bool lowest_above(const Match& match, uint32_t above, uint32_t& sector,
                      uint32_t& sequence) const;
    bool matches(const Match& match, uint32_t sector) const;
    uint8_t code_of(uint32_t sector) const;

    const SectorTable table_;
    uint32_t lost_sectors_[2]{};
    Recycled recycled_[2]{};
    Spare spares_[2]{};
    uint32_t sector_count_{0};
    uint32_t floor_sectors_{0};
    uint32_t sequence_{0};
    uint32_t newest_{0};
    bool claimed_{false};
};

}  // namespace skyblip::store

#endif
