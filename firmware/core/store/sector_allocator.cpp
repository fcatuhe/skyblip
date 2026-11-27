#include "core/store/sector_allocator.h"

#include <algorithm>

namespace skyblip::store {

namespace {

constexpr uint8_t kFree = static_cast<uint8_t>(SectorOwner::None);
// INFO: fc 20sep26 not an owner: a sector whose label this build could not read or does not know
constexpr uint8_t kQuarantined = 0x7F;
constexpr uint8_t kSessionStartBit = 0x80;

uint32_t ring_of(SectorOwner owner) { return owner == SectorOwner::Diagnostics ? 1 : 0; }

void label(const SectorTable& table, uint32_t sector, uint8_t code, uint32_t sequence = 0,
           uint32_t session_id = 0, bool session_start = false) {
    table.label[sector] = session_start ? code | kSessionStartBit : code;
    table.sequence[sector] = sequence;
    table.session_id[sector] = session_id;
}

}  // namespace

bool SectorAllocator::configure(uint32_t sector_count, uint32_t floor_sectors) {
    sector_count_ = sector_count > table_.capacity ? 0 : sector_count;
    floor_sectors_ = floor_sectors;
    reset();
    return configured();
}

void SectorAllocator::reset() {
    for (uint32_t sector = 0; sector < table_.capacity; sector++) label(table_, sector, kFree);
    spares_[0] = Spare{};
    spares_[1] = Spare{};
    lost_sectors_[0] = 0;
    lost_sectors_[1] = 0;
    recycled_[0] = Recycled{};
    recycled_[1] = Recycled{};
    sequence_ = 0;
    newest_ = 0;
    claimed_ = false;
}

void SectorAllocator::note_sector(uint32_t sector, const SectorHeader& header) {
    if (sector >= sector_count_ || header.owner == SectorOwner::None) return;
    // INFO: fc 20sep26 the counter starts at one, so a label numbered zero is not one we wrote
    if (header.sequence == 0) return;
    label(table_, sector, static_cast<uint8_t>(header.owner), header.sequence, header.session_id,
          header.session_start);
    if (!claimed_ || header.sequence > sequence_) {
        sequence_ = header.sequence;
        newest_ = sector;
    }
    claimed_ = true;
}

Claim SectorAllocator::claim(SectorOwner owner, uint32_t session_id) {
    Claim out{};
    if (!configured() || owner == SectorOwner::None) return out;

    Spare& spare = spare_of(owner);
    uint32_t sector = 0;
    if (spare.reserved) {
        sector = spare.sector;
        out.erased = spare.erased;
        spare = Spare{};
    } else if (!choose(owner, sector)) {
        return out;
    }

    out.session_start = !owns_session(owner, session_id);
    note_taken(sector);
    take(sector, owner, session_id, out.session_start);
    out.sector = sector;
    out.sequence = sequence_;
    out.granted = true;
    return out;
}

void SectorAllocator::abandon(uint32_t sector) {
    if (sector >= sector_count_) return;
    label(table_, sector, kFree);
}

bool SectorAllocator::owns_session(SectorOwner owner, uint32_t session_id) const {
    for (uint32_t sector = 0; sector < sector_count_; sector++)
        if (code_of(sector) == static_cast<uint8_t>(owner) &&
            table_.session_id[sector] == session_id)
            return true;
    return false;
}

// INFO: fc 20sep26 counted where the loss is decided: the ring recycling under its own frontier
void SectorAllocator::note_taken(uint32_t sector) {
    const uint8_t code = code_of(sector);
    if (code == kFree || code == kQuarantined) return;
    const SectorOwner owner = static_cast<SectorOwner>(code);
    note_recycled(owner, table_.session_id[sector]);
    uint32_t frontier_sector = 0;
    uint32_t frontier_sequence = 0;
    if (!frontier(owner, frontier_sector, frontier_sequence)) return;
    if (table_.session_id[frontier_sector] != table_.session_id[sector]) return;
    lost_sectors_[ring_of(owner)]++;
}

// INFO: fc 17nov26 a ring gives up only its oldest sector, so one session per ring loses its front
void SectorAllocator::note_recycled(SectorOwner owner, uint32_t session_id) {
    Recycled& recycled = recycled_[ring_of(owner)];
    if (recycled.session_id != session_id) recycled = Recycled{session_id, 0};
    recycled.sectors++;
}

uint32_t SectorAllocator::lost_sectors(SectorOwner owner) const {
    return owner == SectorOwner::None ? 0 : lost_sectors_[ring_of(owner)];
}

uint32_t SectorAllocator::recycled_sectors(SectorOwner owner, uint32_t session_id) const {
    if (owner == SectorOwner::None) return 0;
    const Recycled& recycled = recycled_[ring_of(owner)];
    return recycled.session_id == session_id ? recycled.sectors : 0;
}

Claim SectorAllocator::prepare(SectorOwner owner) {
    Claim out{};
    if (!configured() || owner == SectorOwner::None) return out;

    Spare& spare = spare_of(owner);
    if (spare.erased) return out;
    if (!spare.reserved) {
        uint32_t sector = 0;
        if (!choose(owner, sector)) return out;
        note_taken(sector);
        spare.sector = sector;
        spare.reserved = true;
        label(table_, sector, kFree);
    }
    out.sector = spare.sector;
    out.granted = true;
    return out;
}

void SectorAllocator::note_erased(SectorOwner owner) {
    Spare& spare = spare_of(owner);
    if (spare.reserved) spare.erased = true;
}

void SectorAllocator::release(SectorOwner owner) {
    if (owner == SectorOwner::None) return;
    for (uint32_t sector = 0; sector < sector_count_; sector++) {
        if (code_of(sector) == static_cast<uint8_t>(owner)) label(table_, sector, kFree);
    }
    spare_of(owner) = Spare{};
    recycled_[ring_of(owner)] = Recycled{};
}

void SectorAllocator::quarantine(uint32_t sector) {
    if (sector >= sector_count_) return;
    label(table_, sector, kQuarantined);
}

bool SectorAllocator::quarantined(uint32_t sector) const {
    return sector < sector_count_ && code_of(sector) == kQuarantined;
}

uint32_t SectorAllocator::quarantined() const {
    uint32_t count = 0;
    for (uint32_t sector = 0; sector < sector_count_; sector++)
        if (code_of(sector) == kQuarantined) count++;
    return count;
}

uint32_t SectorAllocator::owned(SectorOwner owner) const {
    uint32_t count = 0;
    for (uint32_t sector = 0; sector < sector_count_; sector++)
        if (code_of(sector) == static_cast<uint8_t>(owner)) count++;
    return count;
}

SectorOwner SectorAllocator::owner_of(uint32_t sector) const {
    if (sector >= sector_count_ || code_of(sector) == kQuarantined) return SectorOwner::None;
    return static_cast<SectorOwner>(code_of(sector));
}

uint32_t SectorAllocator::session_of(uint32_t sector) const {
    return sector < sector_count_ ? table_.session_id[sector] : 0;
}

bool SectorAllocator::session_start(uint32_t sector) const {
    return sector < sector_count_ && (table_.label[sector] & kSessionStartBit) != 0;
}

bool SectorAllocator::frontier(SectorOwner owner, uint32_t& sector, uint32_t& sequence) const {
    bool found = false;
    for (uint32_t candidate = 0; candidate < sector_count_; candidate++) {
        if (code_of(candidate) != static_cast<uint8_t>(owner)) continue;
        if (found && table_.sequence[candidate] < sequence) continue;
        found = true;
        sector = candidate;
        sequence = table_.sequence[candidate];
    }
    return found;
}

bool SectorAllocator::oldest(SectorOwner owner, uint32_t& sector, uint32_t& sequence) const {
    return lowest_above(Match{owner, true, 0}, 0, sector, sequence);
}

// INFO: fc 29sep26 three scans a run, not one a sector: that walk stalled a bench list 340 ms
bool SectorAllocator::session_run(SectorOwner owner, uint32_t above_sequence,
                                  SessionRun& run) const {
    uint32_t first_sector = 0;
    uint32_t first_sequence = 0;
    if (!lowest_above(Match{owner, true, 0}, above_sequence, first_sector, first_sequence))
        return false;
    run = SessionRun{};
    run.session_id = table_.session_id[first_sector];
    uint32_t until = UINT32_MAX;
    for (uint32_t candidate = 0; candidate < sector_count_; candidate++) {
        if (code_of(candidate) != static_cast<uint8_t>(owner)) continue;
        if (table_.session_id[candidate] == run.session_id) continue;
        const uint32_t sequence = table_.sequence[candidate];
        if (sequence > above_sequence && sequence < until) until = sequence;
    }
    const Match match{owner, false, run.session_id};
    for (uint32_t candidate = 0; candidate < sector_count_; candidate++) {
        const uint32_t sequence = table_.sequence[candidate];
        if (!matches(match, candidate) || sequence <= above_sequence || sequence >= until) continue;
        run.sectors++;
        run.last_sequence = std::max(run.last_sequence, sequence);
    }
    return true;
}

// INFO: fc 29sep26 bisected on the label, not a scan a sector: that cost a bench read 600 ms
bool SectorAllocator::session_sector(SectorOwner owner, uint32_t session_id, uint32_t index,
                                     uint32_t& sector) const {
    const Match match{owner, false, session_id};
    uint32_t count = 0;
    uint32_t lowest = UINT32_MAX;
    uint32_t highest = 0;
    for (uint32_t candidate = 0; candidate < sector_count_; candidate++) {
        if (!matches(match, candidate)) continue;
        count++;
        lowest = std::min(lowest, table_.sequence[candidate]);
        highest = std::max(highest, table_.sequence[candidate]);
    }
    if (index >= count) return false;
    const uint32_t labels_before = index;
    const uint32_t labels_after = count - 1 - index;
    uint32_t low = lowest + labels_before;
    uint32_t high = highest - labels_after;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2;
        if (matching_up_to(match, middle) > index)
            high = middle;
        else
            low = middle + 1;
    }
    for (uint32_t candidate = 0; candidate < sector_count_; candidate++) {
        if (!matches(match, candidate) || table_.sequence[candidate] != low) continue;
        sector = candidate;
        return true;
    }
    return false;
}

bool SectorAllocator::choose(SectorOwner owner, uint32_t& sector) const {
    if (next_free(sector)) return true;
    if (owner == SectorOwner::Flights) {
        if (evictable(SectorOwner::Diagnostics, sector)) return true;
        return evictable(SectorOwner::Flights, sector);
    }
    if (owned(SectorOwner::Flights) > floor_sectors_ && evictable(SectorOwner::Flights, sector))
        return true;
    return evictable(SectorOwner::Diagnostics, sector);
}

bool SectorAllocator::next_free(uint32_t& sector) const {
    const uint32_t start = claimed_ ? (newest_ + 1) % sector_count_ : 0;
    for (uint32_t step = 0; step < sector_count_; step++) {
        const uint32_t candidate = (start + step) % sector_count_;
        if (code_of(candidate) != kFree || reserved(candidate)) continue;
        sector = candidate;
        return true;
    }
    return false;
}

bool SectorAllocator::evictable(SectorOwner owner, uint32_t& sector) const {
    uint32_t oldest_sector = 0;
    uint32_t oldest_sequence = 0;
    uint32_t frontier_sector = 0;
    uint32_t frontier_sequence = 0;
    if (!oldest(owner, oldest_sector, oldest_sequence)) return false;
    if (!frontier(owner, frontier_sector, frontier_sequence)) return false;
    if (oldest_sector == frontier_sector) return false;
    sector = oldest_sector;
    return true;
}

bool SectorAllocator::reserved(uint32_t sector) const {
    return (spares_[0].reserved && spares_[0].sector == sector) ||
           (spares_[1].reserved && spares_[1].sector == sector);
}

void SectorAllocator::take(uint32_t sector, SectorOwner owner, uint32_t session_id,
                           bool session_start) {
    sequence_++;
    label(table_, sector, static_cast<uint8_t>(owner), sequence_, session_id, session_start);
    newest_ = sector;
    claimed_ = true;
}

uint32_t SectorAllocator::matching_up_to(const Match& match, uint32_t sequence) const {
    uint32_t count = 0;
    for (uint32_t candidate = 0; candidate < sector_count_; candidate++)
        if (matches(match, candidate) && table_.sequence[candidate] <= sequence) count++;
    return count;
}

bool SectorAllocator::lowest_above(const Match& match, uint32_t above, uint32_t& sector,
                                   uint32_t& sequence) const {
    bool found = false;
    for (uint32_t candidate = 0; candidate < sector_count_; candidate++) {
        if (!matches(match, candidate) || table_.sequence[candidate] <= above) continue;
        if (found && table_.sequence[candidate] > sequence) continue;
        found = true;
        sector = candidate;
        sequence = table_.sequence[candidate];
    }
    return found;
}

bool SectorAllocator::matches(const Match& match, uint32_t sector) const {
    if (code_of(sector) != static_cast<uint8_t>(match.owner)) return false;
    return match.any_session || table_.session_id[sector] == match.session_id;
}

uint8_t SectorAllocator::code_of(uint32_t sector) const {
    return table_.label[sector] & static_cast<uint8_t>(~kSessionStartBit);
}

SectorAllocator::Spare& SectorAllocator::spare_of(SectorOwner owner) {
    return spares_[ring_of(owner)];
}

}  // namespace skyblip::store
