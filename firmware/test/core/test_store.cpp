// The shared sector pool: what a label carries, and which sector a ring is handed next.
#include <cstring>
#include <utility>
#include <vector>

#include "core/store/sector.h"
#include "core/store/sector_allocator.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

constexpr uint32_t kBaseUtc = 1785628800;  // 2026-08-02T00:00:00Z
constexpr uint32_t kSecondsPerSector = 680;
constexpr uint32_t kSomeSession = 1;

using store::SectorOwner;

uint32_t claimed_sector(store::SectorAllocator& pool, SectorOwner owner,
                        uint32_t session = kSomeSession) {
    const store::Claim claim = pool.claim(owner, session);
    REQUIRE(claim.granted);
    return claim.sector;
}

void fill(store::SectorAllocator& pool, SectorOwner owner, uint32_t sectors) {
    for (uint32_t i = 0; i < sectors; i++) claimed_sector(pool, owner);
}

void note_label(store::SectorAllocator& pool, uint32_t sector, SectorOwner owner, uint32_t sequence,
                uint32_t session = kSomeSession) {
    store::SectorHeader header{};
    header.owner = owner;
    header.sequence = sequence;
    header.session_id = session;
    pool.note_sector(sector, header);
}

uint32_t session_sector(const store::SectorAllocator& pool, uint32_t session, uint32_t index) {
    uint32_t sector = 0;
    REQUIRE(pool.session_sector(SectorOwner::Flights, session, index, sector));
    return sector;
}

uint32_t oldest_sector(const store::SectorAllocator& pool, SectorOwner owner) {
    uint32_t sector = 0;
    uint32_t sequence = 0;
    REQUIRE(pool.oldest(owner, sector, sequence));
    return sector;
}

}  // namespace

TEST_CASE("sector label: sixteen bytes carry the owner, and a half-written one is refused") {
    store::SectorHeader in{};
    in.owner = SectorOwner::Flights;
    in.sequence = 4242;
    in.session_id = kBaseUtc;
    in.record_bytes = 24;
    uint8_t raw[store::kSectorHeaderBytes];
    store::encode_sector_header(in, raw);

    store::SectorHeader out{};
    REQUIRE(store::decode_sector_header(raw, out) == Status::Ok);
    CHECK(out.owner == SectorOwner::Flights);
    CHECK(out.sequence == 4242);
    CHECK(out.session_id == kBaseUtc);
    CHECK(out.record_bytes == 24);

    uint8_t erased[store::kSectorHeaderBytes];
    std::memset(erased, 0xFF, sizeof(erased));
    CHECK(store::decode_sector_header(erased, out) == Status::Empty);

    raw[6] ^= 0x40;
    CHECK(store::decode_sector_header(raw, out) == Status::Crc);
}

TEST_CASE("sector label: an owner this build does not know is not a sector it may reuse") {
    store::SectorHeader in{};
    in.owner = SectorOwner::Diagnostics;
    in.sequence = 7;
    uint8_t raw[store::kSectorHeaderBytes];
    store::encode_sector_header(in, raw);
    store::SectorHeader out{};
    REQUIRE(store::decode_sector_header(raw, out) == Status::Ok);
    CHECK(out.owner == SectorOwner::Diagnostics);

    in.owner = static_cast<SectorOwner>(9);
    store::encode_sector_header(in, raw);
    CHECK(store::decode_sector_header(raw, out) == Status::Unsupported);
}

// A suffix cannot claim this bit, which is the whole reason it is under the CRC.
TEST_CASE("sector label: the sector a session opened in says so, and the rest do not") {
    store::SectorHeader in{};
    in.owner = SectorOwner::Diagnostics;
    in.sequence = 12;
    in.session_start = true;
    uint8_t raw[store::kSectorHeaderBytes];
    store::encode_sector_header(in, raw);

    store::SectorHeader out{};
    REQUIRE(store::decode_sector_header(raw, out) == Status::Ok);
    CHECK(out.session_start);

    in.session_start = false;
    store::encode_sector_header(in, raw);
    REQUIRE(store::decode_sector_header(raw, out) == Status::Ok);
    CHECK_FALSE(out.session_start);
}

TEST_CASE("sector pool: only the first sector of a session is labelled as its start") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(4, 0));

    const store::Claim first = pool.claim(SectorOwner::Flights, kBaseUtc);
    CHECK(first.session_start);
    CHECK(pool.session_start(first.sector));
    const store::Claim second = pool.claim(SectorOwner::Flights, kBaseUtc);
    CHECK_FALSE(second.session_start);
    CHECK_FALSE(pool.session_start(second.sector));
    CHECK(pool.claim(SectorOwner::Flights, kBaseUtc + 900).session_start);
}

// Rotation is the design; a rotation nobody counted is the defect.
TEST_CASE("sector pool: a ring recycling under its own frontier counts what the session lost") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(3, 0));
    fill(pool, SectorOwner::Diagnostics, 3);
    CHECK(pool.lost_sectors(SectorOwner::Diagnostics) == 0);

    const store::Claim recycled = pool.claim(SectorOwner::Diagnostics, kSomeSession);
    CHECK(recycled.granted);
    CHECK_FALSE(recycled.session_start);
    CHECK(pool.lost_sectors(SectorOwner::Diagnostics) == 1);
    CHECK(pool.lost_sectors(SectorOwner::Flights) == 0);

    // A sector of an older session is not a hole in the session being written.
    store::SectorAllocator other;
    REQUIRE(other.configure(2, 0));
    claimed_sector(other, SectorOwner::Diagnostics, 1);
    claimed_sector(other, SectorOwner::Diagnostics, 2);
    CHECK(other.claim(SectorOwner::Diagnostics, 2).granted);
    CHECK(other.lost_sectors(SectorOwner::Diagnostics) == 0);
}

TEST_CASE("sector pool: flights eating a live capture sector is the capture's loss to report") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(3, 0));
    claimed_sector(pool, SectorOwner::Flights, kBaseUtc);
    fill(pool, SectorOwner::Diagnostics, 2);

    CHECK(pool.claim(SectorOwner::Flights, kBaseUtc).granted);
    CHECK(pool.lost_sectors(SectorOwner::Diagnostics) == 1);
}

TEST_CASE("sector pool: a quarantined sector is never handed out and never given back") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(3, 2));
    pool.quarantine(0);
    CHECK(pool.quarantined() == 1);
    CHECK(pool.quarantined(0));
    CHECK(pool.owner_of(0) == SectorOwner::None);

    CHECK(claimed_sector(pool, SectorOwner::Flights) == 1);
    CHECK(claimed_sector(pool, SectorOwner::Flights) == 2);
    CHECK_FALSE(pool.claim(SectorOwner::Diagnostics, 7).granted);

    pool.release(SectorOwner::Flights);
    CHECK(pool.quarantined(0));
}

TEST_CASE("sector pool: twelve flight hours is sixty-four sectors of the partition") {
    CHECK(store::kFlightsFloorHours == 12);
    // 12 h is 43200 s, a sector holds 680 s of records, 63.5 rounded up.
    CHECK(store::flights_floor_sectors(kSecondsPerSector) == 64);
    CHECK(store::flights_floor_sectors(0) == 0);
}

TEST_CASE("sector pool: the first sector of a virgin partition is sector zero") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(4, 0));

    const store::Claim first = pool.claim(SectorOwner::Flights, kBaseUtc);
    CHECK(first.granted);
    CHECK(first.sector == 0);
    CHECK(first.sequence == 1);
    CHECK_FALSE(first.erased);
    CHECK(pool.owned(SectorOwner::Flights) == 1);
    CHECK(pool.session_of(first.sector) == kBaseUtc);
}

TEST_CASE("sector pool: a free sector goes to whoever asks for it") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(4, 0));
    CHECK(claimed_sector(pool, SectorOwner::Flights) == 0);
    CHECK(claimed_sector(pool, SectorOwner::Diagnostics) == 1);
    CHECK(claimed_sector(pool, SectorOwner::Flights) == 2);
    CHECK(pool.sequence() == 3);
}

TEST_CASE("sector pool: the partition wraps and the sequence does not") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(3, 0));
    fill(pool, SectorOwner::Flights, 3);

    const store::Claim wrapped = pool.claim(SectorOwner::Flights, kSomeSession);
    CHECK(wrapped.sector == 0);
    CHECK(wrapped.sequence == 4);
    CHECK(pool.owned(SectorOwner::Flights) == 3);

    uint32_t sector = 0;
    uint32_t sequence = 0;
    REQUIRE(pool.frontier(SectorOwner::Flights, sector, sequence));
    CHECK(sector == 0);
    CHECK(sequence == 4);
    CHECK(oldest_sector(pool, SectorOwner::Flights) == 1);
}

TEST_CASE("sector pool: flights eats the oldest diagnostics sector before its own") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(6, 0));
    fill(pool, SectorOwner::Flights, 4);
    const uint32_t first_capture = claimed_sector(pool, SectorOwner::Diagnostics);
    claimed_sector(pool, SectorOwner::Diagnostics);

    CHECK(claimed_sector(pool, SectorOwner::Flights) == first_capture);
    CHECK(pool.owned(SectorOwner::Flights) == 5);
    CHECK(pool.owned(SectorOwner::Diagnostics) == 1);
}

// The one sector a ring owns is the one it is writing into, and nobody gets it.
TEST_CASE("sector pool: a ring's frontier is never handed out") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(6, 0));
    fill(pool, SectorOwner::Flights, 5);
    const uint32_t capture = claimed_sector(pool, SectorOwner::Diagnostics);

    const uint32_t taken = claimed_sector(pool, SectorOwner::Flights);
    CHECK(taken != capture);
    CHECK(pool.owned(SectorOwner::Diagnostics) == 1);
}

TEST_CASE("sector pool: diagnostics takes a flight sector before it recycles its own") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(6, 2));
    fill(pool, SectorOwner::Flights, 3);
    const uint32_t oldest_flight = oldest_sector(pool, SectorOwner::Flights);
    fill(pool, SectorOwner::Diagnostics, 3);

    CHECK(claimed_sector(pool, SectorOwner::Diagnostics) == oldest_flight);
    CHECK(pool.owned(SectorOwner::Flights) == 2);
    CHECK(pool.owned(SectorOwner::Diagnostics) == 4);
}

TEST_CASE("sector pool: diagnostics is refused rather than eating the protected flight hours") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(8, 7));
    fill(pool, SectorOwner::Flights, 8);

    // Eight sectors against a floor of seven: one may go, and then no more.
    CHECK(pool.claim(SectorOwner::Diagnostics, kSomeSession).granted);
    CHECK(pool.owned(SectorOwner::Flights) == 7);

    CHECK_FALSE(pool.claim(SectorOwner::Diagnostics, kSomeSession).granted);
    CHECK(pool.owned(SectorOwner::Flights) == 7);
    CHECK(pool.sequence() == 9);
}

TEST_CASE("sector pool: diagnostics grows to the pool above the floor and then recycles its own") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(10, 4));
    fill(pool, SectorOwner::Flights, 10);

    // Six sectors above the floor of four, so six captures come out of flights.
    for (uint32_t i = 0; i < 6; i++) CHECK(pool.claim(SectorOwner::Diagnostics, 7).granted);
    CHECK(pool.owned(SectorOwner::Flights) == 4);
    CHECK(pool.owned(SectorOwner::Diagnostics) == 6);

    // Steady state: a further capture recycles a sector diagnostics already holds.
    for (uint32_t i = 0; i < 20; i++) {
        const uint32_t taken = claimed_sector(pool, SectorOwner::Diagnostics);
        CHECK(pool.session_of(taken) == kSomeSession);
        CHECK(pool.owned(SectorOwner::Flights) == 4);
        CHECK(pool.owned(SectorOwner::Diagnostics) == 6);
    }
}

TEST_CASE("sector pool: diagnostics owning nothing at the floor is told there is no sector") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(4, 4));
    fill(pool, SectorOwner::Flights, 4);

    const store::Claim refused = pool.claim(SectorOwner::Diagnostics, kSomeSession);
    CHECK_FALSE(refused.granted);
    CHECK(pool.owned(SectorOwner::Flights) == 4);
    CHECK(pool.sequence() == 4);
}

TEST_CASE("sector pool: a prepared spare is claimed instantly and never handed out twice") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(4, 0));
    claimed_sector(pool, SectorOwner::Flights);

    const store::Claim spare = pool.prepare(SectorOwner::Flights);
    REQUIRE(spare.granted);
    CHECK(spare.sector == 1);
    CHECK(pool.prepare(SectorOwner::Flights).sector == 1);
    pool.note_erased(SectorOwner::Flights);
    CHECK_FALSE(pool.prepare(SectorOwner::Flights).granted);

    const store::Claim claim = pool.claim(SectorOwner::Flights, kSomeSession);
    CHECK(claim.sector == 1);
    CHECK(claim.erased);
    const store::Claim after = pool.claim(SectorOwner::Flights, kSomeSession);
    CHECK(after.sector == 2);
    CHECK_FALSE(after.erased);
}

TEST_CASE("sector pool: a spare belongs to the ring it was prepared for") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(4, 0));
    claimed_sector(pool, SectorOwner::Flights);
    const store::Claim spare = pool.prepare(SectorOwner::Flights);
    REQUIRE(spare.granted);
    pool.note_erased(SectorOwner::Flights);

    CHECK(claimed_sector(pool, SectorOwner::Diagnostics) != spare.sector);
    CHECK(claimed_sector(pool, SectorOwner::Flights) == spare.sector);
}

TEST_CASE("sector pool: an erase-all takes the spare with it") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(4, 0));
    fill(pool, SectorOwner::Flights, 2);
    REQUIRE(pool.prepare(SectorOwner::Flights).granted);
    pool.note_erased(SectorOwner::Flights);

    pool.reset();
    CHECK(pool.owned(SectorOwner::Flights) == 0);
    CHECK(pool.sequence() == 0);
    const store::Claim first = pool.claim(SectorOwner::Flights, kSomeSession);
    CHECK(first.sector == 0);
    CHECK_FALSE(first.erased);
}

TEST_CASE("sector pool: a header scan in any physical order finds both frontiers") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(330, 64));
    note_label(pool, 300, SectorOwner::Flights, 5);
    note_label(pool, 7, SectorOwner::Diagnostics, 9);
    note_label(pool, 100, SectorOwner::Flights, 12);
    note_label(pool, 2, SectorOwner::Diagnostics, 3);

    uint32_t sector = 0;
    uint32_t sequence = 0;
    REQUIRE(pool.frontier(SectorOwner::Flights, sector, sequence));
    CHECK(sector == 100);
    CHECK(sequence == 12);
    REQUIRE(pool.frontier(SectorOwner::Diagnostics, sector, sequence));
    CHECK(sector == 7);
    CHECK(sequence == 9);
    CHECK(oldest_sector(pool, SectorOwner::Flights) == 300);
    CHECK(oldest_sector(pool, SectorOwner::Diagnostics) == 2);

    // One counter for the whole partition: the next claim follows the highest label found.
    CHECK(pool.claim(SectorOwner::Flights, kSomeSession).sequence == 13);
}

TEST_CASE("sector pool: a session's sectors come back in the order they were written") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(6, 0));
    const uint32_t first = claimed_sector(pool, SectorOwner::Flights, kBaseUtc);
    const uint32_t capture = claimed_sector(pool, SectorOwner::Diagnostics, 99);
    const uint32_t second = claimed_sector(pool, SectorOwner::Flights, kBaseUtc);
    const uint32_t next_flight = claimed_sector(pool, SectorOwner::Flights, kBaseUtc + 600);

    CHECK(session_sector(pool, kBaseUtc, 0) == first);
    CHECK(session_sector(pool, kBaseUtc, 1) == second);
    uint32_t beyond = 0;
    CHECK_FALSE(pool.session_sector(SectorOwner::Flights, kBaseUtc, 2, beyond));
    CHECK(session_sector(pool, kBaseUtc + 600, 0) == next_flight);
    CHECK(pool.session_of(capture) == 99);

    store::SessionRun run{};
    REQUIRE(pool.session_run(SectorOwner::Flights, 0, run));
    CHECK(run.session_id == kBaseUtc);
    CHECK(run.sectors == 2);
    REQUIRE(pool.session_run(SectorOwner::Flights, run.last_sequence, run));
    CHECK(run.session_id == kBaseUtc + 600);
    CHECK(run.sectors == 1);
    CHECK_FALSE(pool.session_run(SectorOwner::Flights, run.last_sequence, run));
}

// The index of sessions is rebuilt from these runs, so a session written twice lists twice.
TEST_CASE("sector pool: a ring is walked a session run at a time, the other ring between") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(12, 0));
    fill(pool, SectorOwner::Diagnostics, 1);
    claimed_sector(pool, SectorOwner::Flights, kBaseUtc);
    fill(pool, SectorOwner::Diagnostics, 2);
    claimed_sector(pool, SectorOwner::Flights, kBaseUtc);
    claimed_sector(pool, SectorOwner::Flights, kBaseUtc + 600);
    fill(pool, SectorOwner::Diagnostics, 1);
    claimed_sector(pool, SectorOwner::Flights, kBaseUtc + 600);
    claimed_sector(pool, SectorOwner::Flights, kBaseUtc);

    store::SessionRun run{};
    uint32_t walked = 0;
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    while (pool.session_run(SectorOwner::Flights, walked, run)) {
        runs.emplace_back(run.session_id, run.sectors);
        walked = run.last_sequence;
    }
    const std::vector<std::pair<uint32_t, uint32_t>> expected{
        {kBaseUtc, 2}, {kBaseUtc + 600, 2}, {kBaseUtc, 1}};
    CHECK(runs == expected);
}

// A fetch reads every sector of a long capture by index, one chunk at a time.
TEST_CASE("sector pool: every sector of a long session is found by its index, gaps and all") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(330, 0));
    fill(pool, SectorOwner::Diagnostics, 17);
    std::vector<uint32_t> written;
    for (uint32_t i = 0; i < 280; i++) {
        written.push_back(claimed_sector(pool, SectorOwner::Flights, kBaseUtc));
        if (i % 20 == 3) fill(pool, SectorOwner::Diagnostics, 1 + i / 20 % 2);
    }

    for (uint32_t index = 0; index < written.size(); index++)
        CHECK(session_sector(pool, kBaseUtc, index) == written[index]);
    uint32_t beyond = 0;
    CHECK_FALSE(pool.session_sector(SectorOwner::Flights, kBaseUtc, 280, beyond));
    CHECK_FALSE(pool.session_sector(SectorOwner::Flights, kBaseUtc + 1, 0, beyond));
}

TEST_CASE("sector pool: a session that wrapped is still walked in sequence order") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(330, 64));
    note_label(pool, 328, SectorOwner::Flights, 11, kBaseUtc);
    note_label(pool, 329, SectorOwner::Diagnostics, 12, 99);
    note_label(pool, 0, SectorOwner::Flights, 13, kBaseUtc);

    // Physically 0 comes first and it is the last sector the session wrote.
    CHECK(session_sector(pool, kBaseUtc, 0) == 328);
    CHECK(session_sector(pool, kBaseUtc, 1) == 0);
}

TEST_CASE("sector pool: a partition larger than the pool can track is refused, not truncated") {
    store::SectorAllocator pool;
    CHECK_FALSE(pool.configure(store::kMaxPoolSectors + 1, 0));
    CHECK_FALSE(pool.configured());
    CHECK_FALSE(pool.claim(SectorOwner::Flights, kSomeSession).granted);
}

TEST_CASE("sector pool: one ring erased gives its sectors back and leaves the other standing") {
    store::SectorAllocator pool;
    REQUIRE(pool.configure(8, 2));
    const uint32_t flight = claimed_sector(pool, SectorOwner::Flights, kBaseUtc);
    const uint32_t capture = claimed_sector(pool, SectorOwner::Diagnostics, 99);
    const uint32_t sequence_before = pool.sequence();
    CHECK(pool.owner_of(flight) == SectorOwner::Flights);
    CHECK(pool.owner_of(capture) == SectorOwner::Diagnostics);

    pool.release(SectorOwner::Flights);
    CHECK(pool.owned(SectorOwner::Flights) == 0);
    CHECK(pool.owner_of(flight) == SectorOwner::None);
    CHECK(pool.owned(SectorOwner::Diagnostics) == 1);
    CHECK(pool.session_of(capture) == 99);

    // The counter never repeats, whatever was handed back.
    CHECK(pool.claim(SectorOwner::Flights, kBaseUtc).sequence == sequence_before + 1);
}
