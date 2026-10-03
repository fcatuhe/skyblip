#ifndef SKYBLIP_CORE_STORE_SECTOR_ALLOCATOR_H
#define SKYBLIP_CORE_STORE_SECTOR_ALLOCATOR_H

#include <cstdint>

#include "core/store/sector.h"

namespace skyblip::store {

constexpr uint32_t kMaxPoolSectors = 512;

// INFO: fc 20sep26 170 slots at a record per 4 s is 11.3 min a sector, 5.3 an hour, 298 in all
constexpr uint32_t kFlightsFloorHours = 12;

constexpr uint32_t flights_floor_sectors(uint32_t seconds_per_sector) {
    return seconds_per_sector == 0
               ? 0
               : (kFlightsFloorHours * 3600u + seconds_per_sector - 1) / seconds_per_sector;
}

struct Claim {
    uint32_t sector{0};
    uint32_t sequence{0};
    bool granted{false};
    bool erased{false};
    bool session_start{false};
};

class SectorAllocator {
   public:
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
    bool session_start(uint32_t sector) const;
    uint32_t lost_sectors(SectorOwner owner) const;
    bool frontier(SectorOwner owner, uint32_t& sector, uint32_t& sequence) const;
    bool oldest(SectorOwner owner, uint32_t& sector, uint32_t& sequence) const;
    bool next_sector(SectorOwner owner, uint32_t above_sequence, uint32_t& sector,
                     uint32_t& sequence) const;
    bool session_sector(SectorOwner owner, uint32_t session_id, uint32_t index,
                        uint32_t& sector) const;

   private:
    struct Spare {
        uint32_t sector{0};
        bool reserved{false};
        bool erased{false};
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
    void take(uint32_t sector, SectorOwner owner, uint32_t session_id);
    void note_taken(uint32_t sector);
    bool owns_session(SectorOwner owner, uint32_t session_id) const;
    bool nth_matching(const Match& match, uint32_t index, uint32_t& sector) const;
    bool lowest_above(const Match& match, uint32_t above, uint32_t& sector,
                      uint32_t& sequence) const;
    bool matches(const Match& match, uint32_t sector) const;

    uint32_t sequence_of_[kMaxPoolSectors]{};
    uint32_t session_of_[kMaxPoolSectors]{};
    uint8_t owner_of_[kMaxPoolSectors]{};
    bool session_start_[kMaxPoolSectors]{};
    uint32_t lost_sectors_[2]{};
    Spare spares_[2]{};
    uint32_t sector_count_{0};
    uint32_t floor_sectors_{0};
    uint32_t sequence_{0};
    uint32_t newest_{0};
    bool claimed_{false};
};

}  // namespace skyblip::store

#endif
