// What a capture does to the flash: the sectors it takes from the flights ring down to the floor,
// what the floor refuses, the rotation, and when a pass may write at all.
#include <cstring>
#include <string>

#include "core/diag/payload.h"
#include "core/events/link.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/services/capture.h"
#include "products/skyblip_go/services/flight_log.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/capture_rig.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"

using namespace skyblip;
using skyblip::reads_in;

namespace {

void label_sector(platform::host::FlashRegion& flash, uint32_t sector, store::SectorOwner owner,
                  uint32_t sequence, uint32_t session) {
    store::SectorHeader header{};
    header.owner = owner;
    header.sequence = sequence;
    header.session_id = session;
    header.record_bytes = static_cast<uint8_t>(diag::kRecordBytes);
    uint8_t raw[store::kSectorHeaderBytes];
    store::encode_sector_header(header, raw);
    REQUIRE(
        is_ok(flash.write(sector * platform::host::FlashRegion::kSectorBytes, raw, sizeof(raw))));
}

}  // namespace

// The pool is full and the pilot takes off: the flight comes first, the capture carries on.
TEST_CASE("capture: a flights session claims a diagnostics sector, and the capture keeps going") {
    Rig rig;
    const uint32_t count = platform::host::FlashRegion::kSectorCount;
    const uint32_t floor_sectors =
        store::flights_floor_sectors(flight::log_seconds_per_sector(flight::kLogSlotsPerSector));
    for (uint32_t sector = 0; sector < count; sector++)
        label_sector(
            rig.platform.log_flash(), sector,
            sector < count - 40 ? store::SectorOwner::Flights : store::SectorOwner::Diagnostics,
            sector + 1, sector < count - 40 ? 4000 : 9000);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_from_the_page(rig, t);
    taxi(rig, t, 5);
    const uint32_t captured = rig.product.capture().records_written();
    REQUIRE(captured > 0);

    fly(rig, t, 30);
    CHECK(rig.product.flight_log().recording());
    CHECK(rig.product.flight_log().records_written() > 0);
    CHECK(rig.product.flight_log().records_dropped() == 0);
    CHECK(rig.product.flight_log().sectors_owned() >= floor_sectors);
    CHECK(rig.product.capture().capturing());
    CHECK(rig.product.capture().records_written() > captured);
    CHECK(rig.product.capture().stopped() == bus::CaptureStop::None);
}

TEST_CASE("capture: the capture takes flights sectors down to the floor and no further") {
    Rig rig;
    const uint32_t count = platform::host::FlashRegion::kSectorCount;
    const uint32_t floor_sectors =
        store::flights_floor_sectors(flight::log_seconds_per_sector(flight::kLogSlotsPerSector));
    for (uint32_t sector = 0; sector < count; sector++)
        label_sector(rig.platform.log_flash(), sector, store::SectorOwner::Flights, sector + 1,
                     4000 + sector / 4);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 2);
    arm_from_the_page(rig, t);
    CHECK(rig.state().capture.price_flights > 0);

    feed(rig, t, flight::kLogSlotsPerSector * 6);
    CHECK(rig.product.capture().capturing());
    CHECK(rig.product.capture().sectors_owned() >= 5);

    const uint32_t flights_left = count - rig.product.capture().sectors_owned();
    CHECK(flights_left >= floor_sectors);
}

namespace {

// A partition the floor swallows whole, the one shape the allocator can refuse a capture in.
struct SmallPoolRig {
    static constexpr uint32_t kSectors = 65;

    platform::host::Clock clock;
    platform::host::Link link;
    platform::host::FlashRegion flash;
    ports::NullRoles null;
    ports::Roles roles{clock,    null.rf,          link,     null.display,         null.kv,
                       flash,    null.annunciator, null.dfu, null.die_temperature, null.indicator,
                       null.gnss};
    bus::Bus bus{};
    bus::State state{};
    diag::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    go::SettingsStore store{settings, roles.device_addr};
    comms::ConfigService config{link, store};
    go::RecordPool pool{context};
    go::RecordStore flights{pool, store::SectorOwner::Flights};
    go::RecordStore capture_store{pool, store::SectorOwner::Diagnostics};
    go::CaptureService capture{context, capture_store, flights, settings, config};

    explicit SmallPoolRig(uint32_t sectors = kSectors) : flash(sectors) {
        roles.capabilities = ports::Capability::Storage | ports::Capability::Link;
        state.own.utc_valid = true;
        state.own.utc = Rig::kUtcBase;
        link.raise_link(1);
    }

    void label(uint32_t sector, store::SectorOwner owner, uint32_t sequence, uint32_t session) {
        label_sector(flash, sector, owner, sequence, session);
    }

    void setup() {
        flights.open();
        REQUIRE(capture.setup() == Status::Ok);
    }

    void fill_flights_to_the_floor() {
        const uint32_t floor_sectors = store::flights_floor_sectors(
            flight::log_seconds_per_sector(flight::kLogSlotsPerSector));
        for (uint32_t sector = 0; sector < floor_sectors; sector++)
            label(sector, store::SectorOwner::Flights, sector + 1, 4000);
    }

    uint32_t feed_until_stopped(uint32_t& t, uint32_t records) {
        uint32_t pushed = 0;
        while (pushed < records && capture.capturing()) {
            for (int i = 0; i < diag::Recorder::kCapacity && pushed < records; i++)
                if (recorder.record(bench_record(Rig::kUtcBase + pushed))) pushed++;
            capture.tick(t);
            t += 50;
        }
        capture.tick(t);
        return pushed;
    }
};

struct CaptureWalk {
    int gaps{0};
    uint32_t biggest_gap{0};
    int boots_after_gap{0};
    int configs_after_gap{0};
};

CaptureWalk walk_sectors(SmallPoolRig& rig, uint32_t session) {
    CaptureWalk walk{};
    uint32_t index = 0;
    uint32_t sector = 0;
    while (rig.pool.allocator().session_sector(store::SectorOwner::Diagnostics, session, index,
                                               sector)) {
        for (uint32_t slot = 0; slot < rig.pool.slots_per_sector(); slot++) {
            uint8_t raw[diag::kRecordBytes];
            if (!rig.pool.read_slot(sector, slot, raw)) break;
            diag::Record record{};
            if (diag::decode_record(raw, record) != Status::Ok) break;
            if (record.type == diag::Type::Gap) {
                diag::Gap gap{};
                REQUIRE(diag::read(record, gap));
                walk.gaps++;
                if (gap.dropped > walk.biggest_gap) walk.biggest_gap = gap.dropped;
            }
            if (walk.gaps > 0 && record.type == diag::Type::Boot) walk.boots_after_gap++;
            if (walk.gaps > 0 && record.type == diag::Type::Config) walk.configs_after_gap++;
        }
        index++;
    }
    return walk;
}

// Slowly enough that the RAM ring never overflows, so every hole on the flash is the store's.
void feed_gently(SmallPoolRig& rig, uint32_t& t, uint32_t records) {
    uint32_t pushed = 0;
    while (pushed < records && rig.capture.capturing()) {
        for (int i = 0; i < 8 && pushed < records; i++)
            if (rig.recorder.record(bench_record(Rig::kUtcBase + pushed))) pushed++;
        rig.capture.tick(t);
        t += 50;
    }
    rig.capture.tick(t);
}

}  // namespace

TEST_CASE("capture: the floor refuses a sector, the refusal reaches the flash, the capture stops") {
    SmallPoolRig rig;
    const uint32_t floor_sectors =
        store::flights_floor_sectors(flight::log_seconds_per_sector(flight::kLogSlotsPerSector));
    REQUIRE(floor_sectors == SmallPoolRig::kSectors - 1);
    for (uint32_t sector = 0; sector < floor_sectors; sector++)
        rig.label(sector, store::SectorOwner::Flights, sector + 1, 4000);
    rig.setup();

    uint32_t t = 1000;
    rig.recorder.arm();
    rig.capture.tick(t);
    t += 50;
    REQUIRE(rig.capture.capturing());
    REQUIRE(rig.capture.sectors_owned() == 1);

    rig.feed_until_stopped(t, flight::kLogSlotsPerSector * 3);
    CHECK_FALSE(rig.capture.capturing());
    CHECK_FALSE(rig.recorder.armed());
    CHECK(rig.capture.stopped() == bus::CaptureStop::NoSectors);
    CHECK(rig.pool.allocator().owned(store::SectorOwner::Flights) == floor_sectors);
    CHECK(rig.capture.sectors_owned() == 1);

    uint8_t raw[diag::kRecordBytes];
    const uint32_t last = rig.capture.records_written() - 1;
    REQUIRE(rig.pool.read_slot(floor_sectors, last, raw));
    diag::Record ended{};
    REQUIRE(diag::decode_record(raw, ended) == Status::Ok);
    CHECK(ended.type == diag::Type::End);

    REQUIRE(rig.pool.read_slot(floor_sectors, last - 1, raw));
    diag::Record record{};
    REQUIRE(diag::decode_record(raw, record) == Status::Ok);
    CHECK(record.type == diag::Type::Gap);
    diag::Gap gap{};
    REQUIRE(diag::read(record, gap));
    CHECK(gap.dropped > 0);
}

// Rotation is the design; a rotation the corpus cannot see is the defect.
TEST_CASE("capture: a rolling capture writes the hole it made and names the build again") {
    SmallPoolRig rig{66};
    rig.fill_flights_to_the_floor();
    rig.setup();

    uint32_t t = 1000;
    rig.recorder.arm();
    rig.capture.tick(t);
    t += 50;
    REQUIRE(rig.capture.capturing());
    const uint32_t session = rig.capture.session_id();

    feed_gently(rig, t, flight::kLogSlotsPerSector * 3);
    CHECK(rig.capture.capturing());
    CHECK(rig.capture.stopped() == bus::CaptureStop::None);
    CHECK(rig.pool.allocator().owned(store::SectorOwner::Flights) ==
          store::flights_floor_sectors(flight::log_seconds_per_sector(flight::kLogSlotsPerSector)));

    const CaptureWalk walk = walk_sectors(rig, session);
    CHECK(walk.gaps >= 1);
    CHECK(walk.biggest_gap == flight::kLogSlotsPerSector);
    CHECK(walk.boots_after_gap >= 1);
    CHECK(walk.configs_after_gap >= 1);
}

TEST_CASE("capture: a second capture counts its own records and not the first one's") {
    SmallPoolRig rig{66};
    rig.fill_flights_to_the_floor();
    rig.setup();

    uint32_t t = 1000;
    rig.recorder.arm();
    rig.capture.tick(t);
    t += 50;
    feed_gently(rig, t, 200);
    rig.recorder.disarm();
    rig.capture.tick(t);
    t += 50;
    const uint32_t first = rig.capture.records_written();
    REQUIRE(first > 100);
    REQUIRE_FALSE(rig.capture.capturing());

    rig.recorder.arm();
    rig.capture.tick(t);
    t += 50;
    REQUIRE(rig.capture.capturing());
    feed_gently(rig, t, 20);
    CHECK(rig.capture.records_written() > 0);
    CHECK(rig.capture.records_written() < first);
}

// A pass that spends the whole window on flash is a pass that misses the dwell it owed.
TEST_CASE("capture: one pass writes at most the drain ceiling, however deep the queue is") {
    SmallPoolRig rig;
    rig.setup();
    uint32_t t = 1000;
    rig.recorder.arm();
    rig.capture.tick(t);
    REQUIRE(rig.capture.capturing());

    for (int i = 0; i < diag::Recorder::kCapacity; i++)
        REQUIRE(rig.recorder.record(bench_record(Rig::kUtcBase)));
    const int queued = rig.recorder.queued();
    rig.capture.tick(t += 50);
    CHECK(rig.recorder.queued() ==
          queued - static_cast<int>(go::CaptureService::kDrainCeilingRecords));
}

namespace {

constexpr uint32_t kRecordsPerPage =
    platform::host::FlashRegion::kPageBytes / static_cast<uint32_t>(diag::kRecordBytes);

uint32_t frontier_slot(SmallPoolRig& rig) {
    return rig.pool.slots_per_sector() - rig.capture_store.slots_left();
}

uint32_t frontier_sector(SmallPoolRig& rig) {
    uint32_t sector = 0;
    uint32_t sequence = 0;
    REQUIRE(rig.pool.allocator().frontier(store::SectorOwner::Diagnostics, sector, sequence));
    return sector;
}

SmallPoolRig& armed(SmallPoolRig& rig, uint32_t& t) {
    rig.setup();
    rig.recorder.arm();
    rig.capture.tick(t);
    REQUIRE(rig.capture.capturing());
    return rig;
}

void drain_to_slot(SmallPoolRig& rig, uint32_t& t, uint32_t slot) {
    while (frontier_slot(rig) < slot) {
        REQUIRE(rig.recorder.record(bench_record(Rig::kUtcBase)));
        rig.capture.tick(t += 50);
    }
    REQUIRE(frontier_slot(rig) == slot);
}

diag::Record numbered(uint32_t i) { return bench_record(Rig::kUtcBase + 1000 + i); }

void queue_numbered(SmallPoolRig& rig, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) REQUIRE(rig.recorder.record(numbered(i)));
}

void check_numbered(SmallPoolRig& rig, uint32_t sector, uint32_t slot, uint32_t from,
                    uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint8_t want[diag::kRecordBytes];
        diag::encode_record(numbered(from + i), want);
        uint8_t got[diag::kRecordBytes];
        REQUIRE(rig.pool.read_slot(sector, slot + i, got));
        CHECK(std::memcmp(got, want, sizeof(want)) == 0);
    }
}

void check_erased(SmallPoolRig& rig, uint32_t sector, uint32_t slot, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint8_t got[diag::kRecordBytes];
        REQUIRE(rig.pool.read_slot(sector, slot + i, got));
        CHECK(store::erased(got, sizeof(got)));
    }
}

}  // namespace

// Build 895: one 24 B program per record held the loop ~2.5 ms each, ~40 ms in the uplink dwell.
TEST_CASE("capture: a drained second costs one flash write per NOR page, not one per record") {
    SmallPoolRig rig;
    uint32_t t = 1000;
    armed(rig, t);
    // Slot 10 is the first of page 1: the 16 B label and slots 0-9 fill page 0 exactly.
    drain_to_slot(rig, t, kRecordsPerPage);
    const uint32_t sector = frontier_sector(rig);
    const uint32_t writes = rig.flash.writes;
    const uint32_t programs = rig.flash.programs;

    const uint32_t second = diag::Recorder::kPeriodicRecordsPerSecond;
    queue_numbered(rig, second);
    rig.capture.tick(t += 50);

    CHECK(rig.recorder.queued() == 0);
    CHECK(rig.flash.writes - writes == (second + kRecordsPerPage - 1) / kRecordsPerPage);
    // Slot 20 straddles pages 1 and 2, so the second write is two page programs.
    CHECK(rig.flash.programs - programs == 3);
    check_numbered(rig, sector, kRecordsPerPage, 0, second);
}

TEST_CASE("capture: a write stops at the sector's end, and the next sector opens with its claim") {
    SmallPoolRig rig;
    uint32_t t = 1000;
    armed(rig, t);
    const uint32_t slots = rig.pool.slots_per_sector();
    drain_to_slot(rig, t, slots - 5);
    const uint32_t first = frontier_sector(rig);
    const uint32_t writes = rig.flash.writes;

    queue_numbered(rig, 10);
    rig.capture.tick(t += 50);

    CHECK(rig.recorder.queued() == 0);
    const uint32_t second = frontier_sector(rig);
    REQUIRE(second != first);
    // The old sector's last five, the new label, the claim's first record, then its next four.
    CHECK(rig.flash.writes - writes == 4);
    check_numbered(rig, first, slots - 5, 0, 5);
    check_numbered(rig, second, 0, 5, 5);
    check_erased(rig, second, 5, 1);
}

// The window closes between two writes of one drain: the second write's records stay in the ring.
TEST_CASE("capture: a window that closes mid-drain commits only the records the flash took") {
    SmallPoolRig rig;
    uint32_t t = 1000;
    armed(rig, t);
    drain_to_slot(rig, t, kRecordsPerPage);
    const uint32_t sector = frontier_sector(rig);
    const uint32_t written = rig.capture.records_written();
    queue_numbered(rig, 15);

    // Room for one page program and not for the two the straddling write needs after it.
    constexpr int kPhase = timing::kUplinkRxEnd - timing::kJitterGuardMs -
                           static_cast<int>(store::kSlotWriteCostMs) - 1;
    t = 20'000 + kPhase;
    timing::ClockState anchored{};
    anchored.utc_valid = true;
    anchored.pps_locked = true;
    rig.state.rf.plan = timing::Scheduler::plan(kPhase, anchored);
    rig.state.rf.dwell = timing::DwellPhase{t, kPhase, true, false};
    rig.clock.set_millis(t);
    rig.capture.tick(t);

    CHECK(rig.recorder.queued() == 5);
    CHECK(rig.capture.records_written() == written + kRecordsPerPage);
    check_numbered(rig, sector, kRecordsPerPage, 0, kRecordsPerPage);
    check_erased(rig, sector, 2 * kRecordsPerPage, 5);

    rig.state.rf.dwell = timing::DwellPhase{};
    rig.capture.tick(t += 1000);
    CHECK(rig.recorder.queued() == 0);
    check_numbered(rig, sector, kRecordsPerPage, 0, 15);
}

TEST_CASE("capture: nothing reaches the flash in the slot own-ship may be keying the PA in") {
    SmallPoolRig rig;
    rig.setup();
    uint32_t t = 1000;
    rig.recorder.arm();
    rig.capture.tick(t);
    REQUIRE(rig.capture.capturing());

    rig.state.rf.plan.tx_allowed = true;
    for (int i = 0; i < 10; i++) REQUIRE(rig.recorder.record(bench_record(Rig::kUtcBase)));
    const uint32_t writes = rig.flash.writes;
    rig.capture.tick(t += 50);
    CHECK(rig.flash.writes == writes);
    CHECK(rig.recorder.queued() == 10);

    rig.state.rf.plan.tx_allowed = false;
    rig.capture.tick(t += 50);
    CHECK(rig.flash.writes > writes);
    CHECK(rig.recorder.queued() == 0);
}

TEST_CASE("log: a store this build has no service for is refused by name, never in silence") {
    SmallPoolRig rig;
    rig.setup();
    go::FlightLogService log{rig.context, rig.flights, nullptr, rig.config};

    events::RxFrame frame{};
    frame.endpoint = events::Endpoint::Log;
    const char* json = "{\"cmd\":\"list\",\"log\":\"diagnostics\"}";
    frame.session_id = rig.link.session_id();
    frame.len = static_cast<uint16_t>(std::strlen(json));
    std::memcpy(frame.data.data(), json, frame.len);
    rig.bus.log_rx.push(frame);
    log.tick(0);

    REQUIRE_FALSE(rig.link.sent.empty());
    const std::string answer = rig.link.sent.back().bytes;
    CHECK(answer.find("diagnostics") != std::string::npos);
    CHECK(answer.find("no_diagnostics") != std::string::npos);
}
