// The two rings on one partition: what a claim costs, what a refused write keeps.
#include <algorithm>
#include <cstring>
#include <string>

#include "core/diagnostics/payload.h"
#include "doctest/doctest.h"
#include "products/common/services/record_store.h"
#include "test/support/log_record_write.h"
#include "test/support/log_transfer.h"
#include "test/support/store_rig.h"
#include "test/support/timed_fix.h"

using namespace skyblip;

namespace {

constexpr uint32_t kFlightSession = kBaseUtcS;
constexpr uint32_t kCaptureSession = 9000;
constexpr int kClaimCostMs =
    static_cast<int>(store::kSectorEraseCostMs + 2 * store::kSlotWriteCostMs);
// The last phase of the uplink dwell a claim still finishes a retune before its end.
constexpr int kLastClaimPhaseMs =
    timing::dwell_of(timing::kUplinkWindowMs).end_ms - timing::kSwitchRetuneMs - kClaimCostMs;

// One sector of the partition answers every read Status::Down, the boot-scan fault.
class BlindSectorFlash : public platform::host::FlashRegion {
   public:
    BlindSectorFlash(uint32_t sectors, uint32_t blind) : FlashRegion(sectors), blind_(blind) {}

    Status read(uint32_t offset, uint8_t* buffer, uint32_t bytes) override {
        if (offset / kSectorBytes == blind_) return Status::Down;
        return FlashRegion::read(offset, buffer, bytes);
    }

   private:
    const uint32_t blind_;
};

bool says(const std::string& reply, const char* key_value) {
    return reply.find(key_value) != std::string::npos;
}

void write_flight_record(common::RecordStore& store, uint32_t utc_s, bool session_end) {
    flight::LogRecord record{};
    record.utc_s = utc_s;
    record.fix_valid = true;
    record.utc_valid = true;
    record.session_end = session_end;
    uint8_t raw[common::kStoreRecordBytes]{};
    flight::encode_log_record(record, store.base_session(), raw);
    REQUIRE(store.append(raw, 0) == common::Append::Ok);
}

void write_diagnostics_record(common::RecordStore& store, diagnostics::Type type, uint32_t at_s) {
    diagnostics::Instant at{};
    at.at_s = at_s;
    at.utc_dated = true;
    const diagnostics::Record record = type == diagnostics::Type::End
                                           ? diagnostics::record_of(diagnostics::End{}, at)
                                           : diagnostics::record_of(diagnostics::Gnss{}, at);
    uint8_t raw[common::kStoreRecordBytes]{};
    diagnostics::encode_record(record, raw);
    REQUIRE(store.append(raw, 0) == common::Append::Ok);
}

common::Append append_one(common::RecordStore& store, uint32_t now_ms = 0) {
    uint8_t raw[common::kStoreRecordBytes]{};
    diagnostics::encode_record(diagnostics::record_of(diagnostics::Gnss{}, diagnostics::Instant{}),
                               raw);
    return store.append(raw, now_ms);
}

void label(platform::host::FlashRegion& flash, uint32_t sector, const store::SectorHeader& header) {
    uint8_t raw[store::kSectorHeaderBytes];
    store::encode_sector_header(header, raw);
    REQUIRE(
        is_ok(flash.write(sector * platform::host::FlashRegion::kSectorBytes, raw, sizeof(raw))));
}

store::SectorHeader flights_label(uint32_t sequence) {
    store::SectorHeader header{};
    header.owner = store::SectorOwner::Flights;
    header.sequence = sequence;
    header.session_id = kFlightSession;
    header.record_bytes = static_cast<uint8_t>(common::kStoreRecordBytes);
    header.session_start = true;
    return header;
}

}  // namespace

// A flight whose opening sector was recycled under it is a suffix, and must never list as a flight.
TEST_CASE("record store: a flights session that lost its first sector is listed truncated") {
    platform::host::FlashRegion flash{3};
    StoreRig rig{flash};
    rig.open();

    rig.flights.begin_session(kFlightSession);
    const uint32_t slots = rig.pool.slots_per_sector();
    for (uint32_t i = 0; i < slots * 3; i++)
        write_flight_record(rig.flights, kFlightSession + i, false);
    write_flight_record(rig.flights, kFlightSession + slots * 3, true);
    rig.flights.end_session();
    rig.flights.rebuild_index();

    const std::string reply = rig.listed(rig.flights, store::SectorOwner::Flights, 0);
    CHECK(says(reply, "\"closed\":true"));
    CHECK(says(reply, "\"truncated\":true"));
}

TEST_CASE("record store: a session that still holds its first sector is listed whole") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();

    rig.flights.begin_session(kFlightSession);
    write_flight_record(rig.flights, kFlightSession, false);
    write_flight_record(rig.flights, kFlightSession + 4, true);
    rig.flights.end_session();
    rig.flights.rebuild_index();

    const std::string reply = rig.listed(rig.flights, store::SectorOwner::Flights, 0);
    CHECK(says(reply, "\"closed\":true"));
    CHECK(says(reply, "\"truncated\":false"));
}

TEST_CASE("record store: a capture that lost its first sector is listed truncated too") {
    platform::host::FlashRegion flash{3};
    StoreRig rig{flash};
    rig.open();

    rig.capture.begin_session(kCaptureSession);
    const uint32_t slots = rig.pool.slots_per_sector();
    for (uint32_t i = 0; i <= slots * 3; i++)
        write_diagnostics_record(rig.capture, diagnostics::Type::Gnss, kFlightSession + i);
    write_diagnostics_record(rig.capture, diagnostics::Type::End, kFlightSession);
    rig.capture.end_session();
    rig.capture.rebuild_index();

    CHECK(says(rig.listed(rig.capture, store::SectorOwner::Diagnostics, 0), "\"truncated\":true"));
    CHECK(rig.pool.allocator().lost_sectors(store::SectorOwner::Diagnostics) > 0);
}

// No CRC on a diagnostics slot: without the end marker a torn tail reads as telemetry.
TEST_CASE("record store: a capture is closed by its end marker and by nothing else") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();

    rig.capture.begin_session(kCaptureSession);
    write_diagnostics_record(rig.capture, diagnostics::Type::Gnss, kFlightSession);
    rig.capture.end_session();
    rig.capture.rebuild_index();
    CHECK(says(rig.listed(rig.capture, store::SectorOwner::Diagnostics, 0), "\"closed\":false"));

    rig.capture.begin_session(kCaptureSession);
    write_diagnostics_record(rig.capture, diagnostics::Type::End, kFlightSession + 1);
    rig.capture.end_session();
    rig.capture.rebuild_index();
    CHECK(says(rig.listed(rig.capture, store::SectorOwner::Diagnostics, 0), "\"closed\":true"));
}

// A session still being written read as power cut during the trial of #293 (#366).
TEST_CASE("record store: the session the ring is writing is listed recording until it ends") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();

    rig.capture.begin_session(kCaptureSession);
    write_diagnostics_record(rig.capture, diagnostics::Type::Gnss, kFlightSession);
    rig.capture.rebuild_index();
    const std::string writing = rig.listed(rig.capture, store::SectorOwner::Diagnostics, 0);
    CHECK(says(writing, "\"closed\":false"));
    CHECK(says(writing, "\"recording\":true"));

    rig.capture.end_session();
    rig.capture.rebuild_index();
    CHECK_FALSE(says(rig.listed(rig.capture, store::SectorOwner::Diagnostics, 0), "recording"));

    rig.capture.begin_session(kCaptureSession + 1);
    write_diagnostics_record(rig.capture, diagnostics::Type::Gnss, kFlightSession + 1);
    rig.capture.rebuild_index();
    CHECK_FALSE(says(rig.listed(rig.capture, store::SectorOwner::Diagnostics, 0), "recording"));
    CHECK(says(rig.listed(rig.capture, store::SectorOwner::Diagnostics, 1), "\"recording\":true"));
}

// The record is in one place at a time: the ring or the flash, never neither.
TEST_CASE("record store: a write the part refused is a fault the caller can retry") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.capture.begin_session(kCaptureSession);
    write_diagnostics_record(rig.capture, diagnostics::Type::Gnss, kFlightSession);

    const uint32_t faults = rig.pool.faults();
    flash.cut_power_after(0);
    CHECK(append_one(rig.capture) == common::Append::Fault);
    CHECK(rig.pool.faults() == faults + 1);
    CHECK(rig.capture.session_records() == 1);
}

TEST_CASE("record store: a refused sector is not a fault, it is the end of the capture") {
    platform::host::FlashRegion flash{1};
    StoreRig rig{flash};
    rig.open();
    rig.flights.begin_session(kFlightSession);
    write_flight_record(rig.flights, kFlightSession, false);

    rig.capture.begin_session(kCaptureSession);
    CHECK(append_one(rig.capture) == common::Append::NoSector);
    CHECK(rig.pool.faults() == 0);
}

TEST_CASE("record store: a session opens without touching the flash") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    const uint32_t writes = flash.writes;
    const uint32_t erases = flash.erases;

    rig.capture.begin_session(kCaptureSession);
    CHECK(flash.writes == writes);
    CHECK(flash.erases == erases);

    write_diagnostics_record(rig.capture, diagnostics::Type::Gnss, kFlightSession);
    CHECK(flash.erases > erases);
}

TEST_CASE("record store: a label this build cannot parse is quarantined, never reclaimed") {
    platform::host::FlashRegion flash{4};
    store::SectorHeader future = flights_label(1);
    future.version = static_cast<uint8_t>(store::kSectorVersion + 1);
    label(flash, 0, future);
    store::SectorHeader foreign = flights_label(2);
    foreign.record_bytes = 32;
    label(flash, 1, foreign);

    StoreRig rig{flash};
    rig.open();
    CHECK(rig.pool.allocator().quarantined() == 2);
    CHECK(rig.pool.free_sectors() == 2);

    rig.capture.begin_session(kCaptureSession);
    for (int i = 0; i < 4; i++) append_one(rig.capture);
    CHECK(rig.pool.allocator().quarantined(0));
    CHECK(rig.pool.allocator().quarantined(1));
    CHECK(rig.pool.allocator().owner_of(0) == store::SectorOwner::None);
    CHECK(flash.erases <= 2);
}

TEST_CASE("record store: a sector the part would not read is counted, not taken for free space") {
    BlindSectorFlash flash{4, 2};
    StoreRig rig{flash};
    rig.open();

    CHECK(rig.pool.unreadable_sectors() == 1);
    CHECK(rig.pool.allocator().quarantined(2));
    CHECK(rig.pool.free_sectors() == 3);
}

TEST_CASE("record store: an erase-all leaves a quarantined sector alone") {
    platform::host::FlashRegion flash{4};
    store::SectorHeader future = flights_label(1);
    future.version = static_cast<uint8_t>(store::kSectorVersion + 1);
    label(flash, 0, future);

    StoreRig rig{flash};
    rig.open();
    rig.flights.begin_erase();
    while (rig.flights.erasing()) rig.flights.step_erase(0);

    uint8_t raw[store::kSectorHeaderBytes];
    REQUIRE(is_ok(flash.read(0, raw, sizeof(raw))));
    CHECK_FALSE(store::erased(raw, sizeof(raw)));
}

// The pilot confirmed the erase while the app was still downloading a flight.
TEST_CASE("record store: a read the erase-all overtakes stops busy, before any erased record") {
    platform::host::FlashRegion flash{12};
    StoreRig rig{flash};
    rig.open();
    const uint32_t slots = rig.pool.slots_per_sector();
    rig.flights.begin_session(kFlightSession);
    for (uint32_t i = 0; i < slots * 2; i++)
        write_flight_record(rig.flights, kFlightSession + i, false);
    write_flight_record(rig.flights, kFlightSession + slots * 2, true);
    rig.flights.end_session();
    rig.flights.rebuild_index();
    rig.link.clear();
    rig.link.serve();
    rig.link.hold_after(2);

    comms::LogRequest request{};
    request.command = comms::LogCommand::Read;
    request.store = store::SectorOwner::Flights;
    request.peer = events::Peer{rig.link.session_id(), rig.link.connection(rig.link.session_id())};
    request.session = kFlightSession;
    request.count = comms::kMaxLogReadChunks;
    request.understood = true;
    rig.flights.serve(request);
    REQUIRE(rig.flights.reading());
    rig.flights.begin_erase();
    rig.flights.step_erase(0);
    REQUIRE(rig.flights.erasing());
    while (rig.flights.reading()) {
        rig.link.serve();
        REQUIRE(is_ok(rig.pool.deliver_held(0)));
        rig.flights.continue_read();
    }
    rig.link.serve();
    REQUIRE(is_ok(rig.pool.deliver_held(0)));

    int erased = 0;
    for (const auto& frame : rig.link.sent) {
        if (field(link_text(frame.bytes), "command") != "chunk") continue;
        uint8_t raw[comms::kLogChunkRawBytes];
        const int bytes = chunk_records(frame.bytes, raw, sizeof(raw));
        REQUIRE(bytes >= 0);
        for (int at = 0; at < bytes; at += static_cast<int>(common::kStoreRecordBytes))
            if (flight::log_slot_erased(raw + at, common::kStoreRecordBytes)) erased++;
    }
    CHECK(erased == 0);
    CHECK(field(link_text(rig.link.sent.back().bytes), "reason") == "busy");
}

// A service that ran long earlier in the pass left the store writing on a stale phase.
TEST_CASE("record store: a write is placed at the instant it starts, not when its pass began") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.capture.begin_session(kCaptureSession);
    const uint32_t erases = flash.erases;

    // Earlier services held the loop this long, inside the view's staleness bound.
    constexpr int kRanLongMs = 50;
    constexpr int kPassPhaseMs = kLastClaimPhaseMs - kRanLongMs;
    constexpr uint32_t kPassMs = 10'000 + kPassPhaseMs;
    rig.publish_dwell(kPassMs, kPassPhaseMs);
    rig.clock.set_milliseconds(kPassMs + kRanLongMs + 1);
    CHECK(append_one(rig.capture, kPassMs) == common::Append::Deferred);
    CHECK(flash.erases == erases);

    rig.publish_dwell(kPassMs + 1000, kPassPhaseMs);
    rig.clock.set_milliseconds(kPassMs + 1000 + kRanLongMs);
    CHECK(append_one(rig.capture, kPassMs + 1000) == common::Append::Ok);
    CHECK(flash.erases > erases);
}

// The two rings stall the same loop on the same bus, and each used to book the window for itself.
TEST_CASE("record store: both rings writing in one pass share the window that pass offers") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.flights.begin_session(kFlightSession);
    rig.capture.begin_session(kCaptureSession);

    constexpr uint32_t kPassMs = 10'000;
    rig.publish_dwell(kPassMs, kLastClaimPhaseMs);
    CHECK(append_one(rig.flights, kPassMs) == common::Append::Ok);
    CHECK(append_one(rig.capture, kPassMs) == common::Append::Deferred);
    CHECK(rig.capture.sectors_owned() == 0);

    rig.publish_dwell(kPassMs + 1000, timing::dwell_of(timing::kUplinkWindowMs).start_ms);
    CHECK(append_one(rig.capture, kPassMs + 1000) == common::Append::Ok);
}

namespace {

void numbered_record(uint32_t i, uint8_t* out) {
    diagnostics::Instant at{};
    at.at_s = kFlightSession + i;
    at.utc_dated = true;
    diagnostics::encode_record(diagnostics::record_of(diagnostics::Gnss{}, at), out);
}

void advance_to_slot(common::RecordStore& store, uint32_t slot) {
    while (store.session_records() < slot) REQUIRE(append_one(store) == common::Append::Ok);
}

}  // namespace

TEST_CASE(
    "record store: a write takes the records up to the end of the NOR page its first ends in") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.capture.begin_session(kCaptureSession);
    CHECK(rig.capture.run_slots() == 1);

    // Slot s spans bytes 16 + 24 s to 40 + 24 s of its sector, pages are 256 B.
    advance_to_slot(rig.capture, 1);
    CHECK(rig.capture.run_slots() == 9);
    advance_to_slot(rig.capture, 10);
    CHECK(rig.capture.run_slots() == 10);
    advance_to_slot(rig.capture, 20);
    CHECK(rig.capture.run_slots() == common::kMaxRunSlots);
    advance_to_slot(rig.capture, rig.pool.slots_per_sector() - 5);
    CHECK(rig.capture.run_slots() == 5);
}

TEST_CASE(
    "record store: a run longer than its page allows is refused before it reaches the flash") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.capture.begin_session(kCaptureSession);
    advance_to_slot(rig.capture, rig.pool.slots_per_sector() - 2);
    const uint32_t writes = flash.writes;
    const uint32_t records = rig.capture.session_records();

    uint8_t raw[3 * common::kStoreRecordBytes]{};
    for (uint32_t i = 0; i < 3; i++) numbered_record(i, raw + i * common::kStoreRecordBytes);
    CHECK(rig.capture.append(raw, 3, 0) == common::Append::Fault);
    CHECK(flash.writes == writes);
    CHECK(rig.capture.session_records() == records);
}

// The driver programs each page a write touches, so a record across a page edge is two programs.
TEST_CASE("record store: a write that spans a page edge is booked as two page programs") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.capture.begin_session(kCaptureSession);
    advance_to_slot(rig.capture, 20);

    constexpr int kOneProgramLeft = timing::dwell_of(timing::kUplinkWindowMs).end_ms -
                                    timing::kSwitchRetuneMs -
                                    static_cast<int>(store::kSlotWriteCostMs);
    constexpr uint32_t kPassMs = 10'000;
    rig.publish_dwell(kPassMs, kOneProgramLeft);
    const uint32_t programs = flash.programs;
    CHECK(append_one(rig.capture, kPassMs) == common::Append::Deferred);

    rig.publish_dwell(kPassMs + 1000, kOneProgramLeft - static_cast<int>(store::kSlotWriteCostMs));
    CHECK(append_one(rig.capture, kPassMs + 1000) == common::Append::Ok);
    CHECK(flash.programs - programs == 2);
}

// A log fetch reads the flash, so the runs must leave the same image the single writes did.
TEST_CASE("record store: records written in page runs leave the image one at a time leaves") {
    platform::host::FlashRegion single_flash{4};
    StoreRig single{single_flash};
    single.open();
    platform::host::FlashRegion run_flash{4};
    StoreRig runs{run_flash};
    runs.open();
    single.capture.begin_session(kCaptureSession);
    runs.capture.begin_session(kCaptureSession);

    const uint32_t total = single.pool.slots_per_sector() + 40;
    uint8_t raw[common::kMaxRunSlots * common::kStoreRecordBytes]{};
    for (uint32_t i = 0; i < total; i++) {
        numbered_record(i, raw);
        REQUIRE(single.capture.append(raw, 0) == common::Append::Ok);
    }
    for (uint32_t i = 0; i < total;) {
        const uint32_t run = std::min(runs.capture.run_slots(), total - i);
        for (uint32_t offset = 0; offset < run; offset++)
            numbered_record(i + offset, raw + offset * common::kStoreRecordBytes);
        REQUIRE(runs.capture.append(raw, run, 0) == common::Append::Ok);
        i += run;
    }

    CHECK(run_flash.bytes() == single_flash.bytes());
    CHECK(runs.capture.session_records() == total);
    CHECK(run_flash.writes * 8 < single_flash.writes);
}
