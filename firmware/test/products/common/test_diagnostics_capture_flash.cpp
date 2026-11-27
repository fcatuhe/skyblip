// What a capture does to the flash: the sectors it takes from the flights ring down to the floor,
// what the floor refuses, the rotation, and when a pass may write at all.
#include <cstring>
#include <string>

#include "core/diagnostics/payload.h"
#include "core/events/link.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "products/common/services/capture.h"
#include "products/common/services/flight_log.h"
#include "test/support/capture_rig.h"
#include "test/support/config_frame.h"
#include "test/support/diagnostics_read.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"
#include "test/support/small_pool_rig.h"

using namespace skyblip;
using skyblip::reads_in;

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
    rig.now_ms = 100;
    taxi(rig, 5);
    arm_over_the_link(rig);
    taxi(rig, 5);
    const uint32_t captured = rig.product.capture().records_written();
    REQUIRE(captured > 0);

    fly(rig, 30);
    CHECK(rig.product.flight_log().recording());
    CHECK(rig.product.flight_log().records_written() > 0);
    CHECK(rig.product.flight_log().records_dropped() == 0);
    CHECK(rig.product.flight_log().sectors_owned() >= floor_sectors);
    CHECK(rig.product.capture().capturing());
    CHECK(rig.product.capture().records_written() > captured);
    CHECK(rig.product.capture().stopped() == bus::CaptureStop::None);
}

TEST_CASE("capture: the capture takes flights sectors down to the floor and no further" *
          doctest::test_suite("main")) {
    Rig rig;
    const uint32_t count = platform::host::FlashRegion::kSectorCount;
    const uint32_t floor_sectors =
        store::flights_floor_sectors(flight::log_seconds_per_sector(flight::kLogSlotsPerSector));
    for (uint32_t sector = 0; sector < count; sector++)
        label_sector(rig.platform.log_flash(), sector, store::SectorOwner::Flights, sector + 1,
                     4000 + sector / 4);
    REQUIRE(rig.setup() == Status::Ok);
    rig.now_ms = 100;
    taxi(rig, 2);
    arm_over_the_link(rig);
    CHECK(rig.state().capture.evicted_flights > 0);

    feed(rig, flight::kLogSlotsPerSector * 6);
    CHECK(rig.product.capture().capturing());
    CHECK(rig.product.capture().sectors_owned() >= 5);

    const uint32_t flights_left = count - rig.product.capture().sectors_owned();
    CHECK(flights_left >= floor_sectors);
}

TEST_CASE("capture: the floor refuses a sector, the refusal reaches the flash, the capture stops") {
    SmallPoolRig rig;
    const uint32_t floor_sectors =
        store::flights_floor_sectors(flight::log_seconds_per_sector(flight::kLogSlotsPerSector));
    REQUIRE(floor_sectors == SmallPoolRig::kSectors - 1);
    for (uint32_t sector = 0; sector < floor_sectors; sector++)
        rig.label(sector, store::SectorOwner::Flights, sector + 1, 4000);
    rig.setup();

    rig.now_ms = 1000;
    rig.recorder.arm();
    rig.capture.tick(rig.now_ms);
    rig.now_ms += 50;
    REQUIRE(rig.capture.capturing());
    REQUIRE(rig.capture.sectors_owned() == 1);

    rig.feed_until_stopped(flight::kLogSlotsPerSector * 3);
    CHECK_FALSE(rig.capture.capturing());
    CHECK_FALSE(rig.recorder.armed());
    CHECK(rig.capture.stopped() == bus::CaptureStop::NoSectors);
    CHECK(rig.pool.allocator().owned(store::SectorOwner::Flights) == floor_sectors);
    CHECK(rig.capture.sectors_owned() == 1);

    uint8_t raw[diagnostics::kRecordBytes];
    const uint32_t last = rig.capture.records_written() - 1;
    REQUIRE(rig.pool.read_slot(floor_sectors, last, raw));
    diagnostics::Record ended{};
    REQUIRE(diagnostics::decode_record(raw, ended) == Status::Ok);
    CHECK(ended.type == diagnostics::Type::End);

    REQUIRE(rig.pool.read_slot(floor_sectors, last - 1, raw));
    diagnostics::Record record{};
    REQUIRE(diagnostics::decode_record(raw, record) == Status::Ok);
    CHECK(record.type == diagnostics::Type::Gap);
    diagnostics::Gap gap{};
    REQUIRE(diagnostics::read(record, gap));
    CHECK(gap.dropped > 0);
}

// Rotation is the design; a rotation the corpus cannot see is the defect.
TEST_CASE("capture: a rolling capture writes the hole it made and names the build again") {
    SmallPoolRig rig{SmallPoolRig::kSectors + 1};
    rig.fill_flights_to_the_floor();
    rig.setup();

    rig.now_ms = 1000;
    rig.recorder.arm();
    rig.capture.tick(rig.now_ms);
    rig.now_ms += 50;
    REQUIRE(rig.capture.capturing());
    const uint32_t session = rig.capture.session_id();

    feed_gently(rig, flight::kLogSlotsPerSector * 3);
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
    SmallPoolRig rig{SmallPoolRig::kSectors + 1};
    rig.fill_flights_to_the_floor();
    rig.setup();

    rig.now_ms = 1000;
    rig.recorder.arm();
    rig.capture.tick(rig.now_ms);
    rig.now_ms += 50;
    feed_gently(rig, 200);
    rig.recorder.disarm();
    rig.capture.tick(rig.now_ms);
    rig.now_ms += 50;
    const uint32_t first = rig.capture.records_written();
    REQUIRE(first > 100);
    REQUIRE_FALSE(rig.capture.capturing());

    rig.recorder.arm();
    rig.capture.tick(rig.now_ms);
    rig.now_ms += 50;
    REQUIRE(rig.capture.capturing());
    feed_gently(rig, 20);
    CHECK(rig.capture.records_written() > 0);
    CHECK(rig.capture.records_written() < first);
}

namespace {

// Before the first fix a capture is dated in seconds since boot, as on bravo-d582 at 10 s.
constexpr uint32_t kArmedAtUptimeMs = 10'000;

void arm_before_a_fix(SmallPoolRig& rig) {
    rig.state.own.utc_valid = false;
    rig.setup();
    rig.now_ms = kArmedAtUptimeMs;
    rig.recorder.arm();
    rig.capture.tick(rig.now_ms);
    rig.now_ms += 50;
    REQUIRE(rig.capture.capturing());
}

void stop(SmallPoolRig& rig) {
    rig.recorder.disarm();
    rig.capture.tick(rig.now_ms);
    rig.now_ms += 50;
    REQUIRE_FALSE(rig.capture.capturing());
}

}  // namespace

// bravo-d582 build 2873 took an earlier boot's name and wrote a 170-record gap per recycled sector.
TEST_CASE("capture: a capture armed at the uptime of one from an earlier boot is its own session") {
    SmallPoolRig before{SmallPoolRig::kSectors + 4};
    before.fill_flights_to_the_floor();
    arm_before_a_fix(before);
    feed_gently(before, flight::kLogSlotsPerSector * 2);
    stop(before);
    const uint32_t earlier = before.capture.session_id();
    const uint32_t earlier_sectors = before.capture.sectors_owned();
    REQUIRE(earlier_sectors == 3);

    SmallPoolRig after{SmallPoolRig::kSectors + 4};
    after.flash.restore(before.flash.bytes());
    arm_before_a_fix(after);
    CHECK(after.capture.session_id() != earlier);

    feed_gently(after, flight::kLogSlotsPerSector * 3);
    CHECK(after.pool.allocator().recycled_sectors(store::SectorOwner::Diagnostics, earlier) ==
          earlier_sectors);
    CHECK(walk_sectors(after, after.capture.session_id()).gaps == 0);
    CHECK(after.capture.records_written() == flight::kLogSlotsPerSector * 3 + 2);
}

TEST_CASE("capture: a capture with a fix is named by its UTC second, a second one by the next") {
    SmallPoolRig rig;
    rig.setup();
    rig.now_ms = 1000;
    rig.recorder.arm();
    rig.capture.tick(rig.now_ms);
    CHECK(rig.capture.session_id() == kBaseUtcS);
    feed_gently(rig, 20);
    stop(rig);

    rig.recorder.arm();
    rig.capture.tick(rig.now_ms);
    CHECK(rig.capture.session_id() == kBaseUtcS + 1);
}

// A pass that spends the whole window on flash is a pass that misses the dwell it owed.
TEST_CASE("capture: one pass writes at most the drain ceiling, however deep the queue is") {
    SmallPoolRig rig;
    rig.setup();
    rig.now_ms = 1000;
    rig.recorder.arm();
    rig.capture.tick(rig.now_ms);
    REQUIRE(rig.capture.capturing());

    for (int i = 0; i < diagnostics::Recorder::kMaxRecords; i++)
        REQUIRE(rig.recorder.record(bench_record(kBaseUtcS)));
    const int queued = rig.recorder.queued();
    rig.capture.tick(rig.now_ms += 50);
    CHECK(rig.recorder.queued() ==
          queued - static_cast<int>(common::CaptureService::kDrainCeilingRecords));
}

namespace {

constexpr uint32_t kRecordsPerPage =
    platform::host::FlashRegion::kPageBytes / static_cast<uint32_t>(diagnostics::kRecordBytes);

uint32_t frontier_slot(SmallPoolRig& rig) {
    return rig.pool.slots_per_sector() - rig.capture_store.slots_left();
}

uint32_t frontier_sector(SmallPoolRig& rig) {
    uint32_t sector = 0;
    uint32_t sequence = 0;
    REQUIRE(rig.pool.allocator().frontier(store::SectorOwner::Diagnostics, sector, sequence));
    return sector;
}

SmallPoolRig& armed(SmallPoolRig& rig) {
    rig.setup();
    rig.recorder.arm();
    rig.capture.tick(rig.now_ms);
    REQUIRE(rig.capture.capturing());
    return rig;
}

void drain_to_slot(SmallPoolRig& rig, uint32_t slot) {
    while (frontier_slot(rig) < slot) {
        REQUIRE(rig.recorder.record(bench_record(kBaseUtcS)));
        rig.capture.tick(rig.now_ms += 50);
    }
    REQUIRE(frontier_slot(rig) == slot);
}

diagnostics::Record numbered(uint32_t i) { return bench_record(kBaseUtcS + 1000 + i); }

void queue_numbered(SmallPoolRig& rig, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) REQUIRE(rig.recorder.record(numbered(i)));
}

void check_numbered(SmallPoolRig& rig, uint32_t sector, uint32_t slot, uint32_t from,
                    uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint8_t want[diagnostics::kRecordBytes];
        diagnostics::encode_record(numbered(from + i), want);
        uint8_t got[diagnostics::kRecordBytes];
        REQUIRE(rig.pool.read_slot(sector, slot + i, got));
        CHECK(std::memcmp(got, want, sizeof(want)) == 0);
    }
}

void check_erased(SmallPoolRig& rig, uint32_t sector, uint32_t slot, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint8_t got[diagnostics::kRecordBytes];
        REQUIRE(rig.pool.read_slot(sector, slot + i, got));
        CHECK(store::erased(got, sizeof(got)));
    }
}

}  // namespace

// Build 895: one 24 B program per record held the loop ~2.5 ms each, ~40 ms in the uplink dwell.
TEST_CASE("capture: a drained second costs one flash write per NOR page, not one per record") {
    SmallPoolRig rig;
    rig.now_ms = 1000;
    armed(rig);
    // Slot 10 is the first of page 1: the 16 B label and slots 0-9 fill page 0 exactly.
    drain_to_slot(rig, kRecordsPerPage);
    const uint32_t sector = frontier_sector(rig);
    const uint32_t writes = rig.flash.writes;
    const uint32_t programs = rig.flash.programs;

    const uint32_t second = diagnostics::Recorder::kPeriodicRecordsPerSecond;
    queue_numbered(rig, second);
    rig.capture.tick(rig.now_ms += 50);

    CHECK(rig.recorder.queued() == 0);
    CHECK(rig.flash.writes - writes == (second + kRecordsPerPage - 1) / kRecordsPerPage);
    // Slot 20 straddles pages 1 and 2, so the second write is two page programs.
    CHECK(rig.flash.programs - programs == 3);
    check_numbered(rig, sector, kRecordsPerPage, 0, second);
}

TEST_CASE("capture: a write stops at the sector's end, and the next sector opens with its claim") {
    SmallPoolRig rig;
    rig.now_ms = 1000;
    armed(rig);
    const uint32_t slots = rig.pool.slots_per_sector();
    drain_to_slot(rig, slots - 5);
    const uint32_t first = frontier_sector(rig);
    const uint32_t writes = rig.flash.writes;

    queue_numbered(rig, 10);
    rig.capture.tick(rig.now_ms += 50);

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
    rig.now_ms = 1000;
    armed(rig);
    drain_to_slot(rig, kRecordsPerPage);
    const uint32_t sector = frontier_sector(rig);
    const uint32_t written = rig.capture.records_written();
    queue_numbered(rig, 15);

    // Room for one page program and not for the two the straddling write needs after it.
    constexpr int kPhaseMs = timing::dwell_of(timing::kUplinkWindowMs).end_ms -
                             timing::kSwitchRetuneMs - static_cast<int>(store::kSlotWriteCostMs) -
                             1;
    rig.now_ms = 20'000 + kPhaseMs;
    timing::ClockState anchored{};
    anchored.utc_valid = true;
    anchored.pps_locked = true;
    rig.state.rf.plan = timing::Scheduler::plan(kPhaseMs, anchored);
    rig.state.rf.dwell = timing::DwellPhase{rig.now_ms, kPhaseMs, true, false};
    rig.clock.set_milliseconds(rig.now_ms);
    rig.capture.tick(rig.now_ms);

    CHECK(rig.recorder.queued() == 5);
    CHECK(rig.capture.records_written() == written + kRecordsPerPage);
    check_numbered(rig, sector, kRecordsPerPage, 0, kRecordsPerPage);
    check_erased(rig, sector, 2 * kRecordsPerPage, 5);

    rig.state.rf.dwell = timing::DwellPhase{};
    rig.capture.tick(rig.now_ms += 1000);
    CHECK(rig.recorder.queued() == 0);
    check_numbered(rig, sector, kRecordsPerPage, 0, 15);
}

TEST_CASE("capture: nothing reaches the flash in the slot own-ship may be keying the PA in") {
    SmallPoolRig rig;
    rig.setup();
    rig.now_ms = 1000;
    rig.recorder.arm();
    rig.capture.tick(rig.now_ms);
    REQUIRE(rig.capture.capturing());

    rig.state.rf.plan.tx_allowed = true;
    for (int i = 0; i < 10; i++) REQUIRE(rig.recorder.record(bench_record(kBaseUtcS)));
    const uint32_t writes = rig.flash.writes;
    rig.capture.tick(rig.now_ms += 50);
    CHECK(rig.flash.writes == writes);
    CHECK(rig.recorder.queued() == 10);

    rig.state.rf.plan.tx_allowed = false;
    rig.capture.tick(rig.now_ms += 50);
    CHECK(rig.flash.writes > writes);
    CHECK(rig.recorder.queued() == 0);
}

TEST_CASE("log: a store this build has no service for is refused by name, never in silence") {
    SmallPoolRig rig;
    rig.setup();
    common::FlightLogService log{rig.context, rig.flights, nullptr, rig.config};

    events::RxFrame list = link_frame(events::Endpoint::Log, rig.link.session_id(),
                                      "{\"command\":\"list\",\"log\":\"diagnostics\"}");
    list.connection = rig.link.connection(list.session_id);
    rig.bus.log_rx.push(list);
    log.tick(0);

    REQUIRE_FALSE(rig.link.sent.empty());
    const std::string answer = link_text(rig.link.sent.back().bytes);
    CHECK(answer.find("diagnostics") != std::string::npos);
    CHECK(answer.find("no_diagnostics") != std::string::npos);
}
