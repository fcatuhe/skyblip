// The two rings on one partition: what a claim costs, what a refused write keeps.
#include <algorithm>
#include <cstring>
#include <string>

#include "core/diag/payload.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/services/record_store.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

constexpr uint32_t kFlightSession = 1785628800;
constexpr uint32_t kCaptureSession = 9000;
constexpr int kClaimCostMs = static_cast<int>(go::kSectorEraseCostMs + 2 * go::kSlotWriteCostMs);
// The last phase of the uplink dwell a claim still finishes a guard's width before its end.
constexpr int kLastClaimPhase = timing::kUplinkRxEnd - timing::kJitterGuardMs - kClaimCostMs;

// One sector of the partition answers every read Status::Down, the boot-scan fault.
class BlindSectorFlash : public platform::host::FlashRegion {
   public:
    BlindSectorFlash(uint32_t sectors, uint32_t blind) : FlashRegion(sectors), blind_(blind) {}

    Status read(uint32_t offset, uint8_t* buf, uint32_t len) override {
        if (offset / kSectorBytes == blind_) return Status::Down;
        return FlashRegion::read(offset, buf, len);
    }

   private:
    const uint32_t blind_;
};

struct StoreRig {
    platform::host::Clock clock;
    platform::host::Link link;
    ports::NullRoles null;
    ports::FlashRegion& flash;
    ports::Roles roles;
    bus::Bus bus{};
    bus::State state{};
    diag::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::RecordPool pool{context};
    go::RecordStore flights{pool, store::SectorOwner::Flights};
    go::RecordStore capture{pool, store::SectorOwner::Diagnostics};

    explicit StoreRig(ports::FlashRegion& region)
        : flash(region),
          roles{clock,    null.rf,          link,     null.display,         null.kv,
                region,   null.annunciator, null.dfu, null.die_temperature, null.indicator,
                null.gnss} {
        roles.capabilities = ports::Capability::Storage | ports::Capability::Link;
        link.raise_link(1);
    }

    void open() {
        REQUIRE(flights.open());
        REQUIRE(capture.open());
    }

    std::string listed(go::RecordStore& store, store::SectorOwner owner, uint32_t index) {
        comms::LogRequest request{};
        request.command = comms::LogCommand::List;
        request.store = owner;
        request.link_session = link.session_id();
        request.index_valid = true;
        request.index = index;
        request.understood = true;
        link.clear();
        store.serve(request);
        REQUIRE_FALSE(link.sent.empty());
        return link.sent.back().bytes;
    }

    // The radio's view of the second, published at the instant a pass began.
    void publish_dwell(uint32_t pass_ms, int phase_ms) {
        timing::ClockState anchored{};
        anchored.utc_valid = true;
        anchored.pps_locked = true;
        state.rf.plan = timing::Scheduler::plan(phase_ms, anchored);
        state.rf.dwell = timing::DwellPhase{pass_ms, phase_ms, true, false};
        clock.set_millis(pass_ms);
    }
};

bool says(const std::string& reply, const char* key_value) {
    return reply.find(key_value) != std::string::npos;
}

void write_flight_record(go::RecordStore& store, uint32_t utc, bool session_end) {
    flight::LogRecord record{};
    record.utc = utc;
    record.fix_valid = true;
    record.utc_valid = true;
    record.session_end = session_end;
    uint8_t raw[go::kStoreRecordBytes]{};
    flight::encode_log_record(record, store.base_session(), raw);
    REQUIRE(store.append(raw, 0) == go::Append::Ok);
}

void write_diag_record(go::RecordStore& store, diag::Type type, uint32_t at_s) {
    diag::Instant at{};
    at.at_s = at_s;
    at.utc_dated = true;
    const diag::Record record = type == diag::Type::End ? diag::record_of(diag::End{}, at)
                                                        : diag::record_of(diag::Gnss{}, at);
    uint8_t raw[go::kStoreRecordBytes]{};
    diag::encode_record(record, raw);
    REQUIRE(store.append(raw, 0) == go::Append::Ok);
}

go::Append append_one(go::RecordStore& store, uint32_t now_ms = 0) {
    uint8_t raw[go::kStoreRecordBytes]{};
    diag::encode_record(diag::record_of(diag::Gnss{}, diag::Instant{}), raw);
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
    header.record_bytes = static_cast<uint8_t>(go::kStoreRecordBytes);
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
        write_diag_record(rig.capture, diag::Type::Gnss, kFlightSession + i);
    write_diag_record(rig.capture, diag::Type::End, kFlightSession);
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
    write_diag_record(rig.capture, diag::Type::Gnss, kFlightSession);
    rig.capture.end_session();
    rig.capture.rebuild_index();
    CHECK(says(rig.listed(rig.capture, store::SectorOwner::Diagnostics, 0), "\"closed\":false"));

    rig.capture.begin_session(kCaptureSession);
    write_diag_record(rig.capture, diag::Type::End, kFlightSession + 1);
    rig.capture.end_session();
    rig.capture.rebuild_index();
    CHECK(says(rig.listed(rig.capture, store::SectorOwner::Diagnostics, 0), "\"closed\":true"));
}

// The record is in one place at a time: the ring or the flash, never neither.
TEST_CASE("record store: a write the part refused is a fault the caller can retry") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.capture.begin_session(kCaptureSession);
    write_diag_record(rig.capture, diag::Type::Gnss, kFlightSession);

    const uint32_t faults = rig.pool.faults();
    flash.cut_power_after(0);
    CHECK(append_one(rig.capture) == go::Append::Fault);
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
    CHECK(append_one(rig.capture) == go::Append::NoSector);
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

    write_diag_record(rig.capture, diag::Type::Gnss, kFlightSession);
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

// A service that ran long earlier in the pass left the store writing on a stale phase.
TEST_CASE("record store: a write is placed at the instant it starts, not when its pass began") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.capture.begin_session(kCaptureSession);
    const uint32_t erases = flash.erases;

    // Earlier services held the loop this long, inside the view's staleness bound.
    constexpr int kRanLongMs = 50;
    constexpr int kPassPhase = kLastClaimPhase - kRanLongMs;
    constexpr uint32_t kPassMs = 10'000 + kPassPhase;
    rig.publish_dwell(kPassMs, kPassPhase);
    rig.clock.set_millis(kPassMs + kRanLongMs + 1);
    CHECK(append_one(rig.capture, kPassMs) == go::Append::Deferred);
    CHECK(flash.erases == erases);

    rig.publish_dwell(kPassMs + 1000, kPassPhase);
    rig.clock.set_millis(kPassMs + 1000 + kRanLongMs);
    CHECK(append_one(rig.capture, kPassMs + 1000) == go::Append::Ok);
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
    rig.publish_dwell(kPassMs, kLastClaimPhase);
    CHECK(append_one(rig.flights, kPassMs) == go::Append::Ok);
    CHECK(append_one(rig.capture, kPassMs) == go::Append::Deferred);
    CHECK(rig.capture.sectors_owned() == 0);

    rig.publish_dwell(kPassMs + 1000, timing::kUplinkRxStart);
    CHECK(append_one(rig.capture, kPassMs + 1000) == go::Append::Ok);
}

namespace {

void numbered_record(uint32_t i, uint8_t* out) {
    diag::Instant at{};
    at.at_s = kFlightSession + i;
    at.utc_dated = true;
    diag::encode_record(diag::record_of(diag::Gnss{}, at), out);
}

void advance_to_slot(go::RecordStore& store, uint32_t slot) {
    while (store.session_records() < slot) REQUIRE(append_one(store) == go::Append::Ok);
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
    CHECK(rig.capture.run_slots() == go::kRunMostSlots);
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

    uint8_t raw[3 * go::kStoreRecordBytes]{};
    for (uint32_t i = 0; i < 3; i++) numbered_record(i, raw + i * go::kStoreRecordBytes);
    CHECK(rig.capture.append(raw, 3, 0) == go::Append::Fault);
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

    constexpr int kOneProgramLeft =
        timing::kUplinkRxEnd - timing::kJitterGuardMs - static_cast<int>(go::kSlotWriteCostMs);
    constexpr uint32_t kPassMs = 10'000;
    rig.publish_dwell(kPassMs, kOneProgramLeft);
    const uint32_t programs = flash.programs;
    CHECK(append_one(rig.capture, kPassMs) == go::Append::Deferred);

    rig.publish_dwell(kPassMs + 1000, kOneProgramLeft - static_cast<int>(go::kSlotWriteCostMs));
    CHECK(append_one(rig.capture, kPassMs + 1000) == go::Append::Ok);
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
    uint8_t raw[go::kRunMostSlots * go::kStoreRecordBytes]{};
    for (uint32_t i = 0; i < total; i++) {
        numbered_record(i, raw);
        REQUIRE(single.capture.append(raw, 0) == go::Append::Ok);
    }
    for (uint32_t i = 0; i < total;) {
        const uint32_t run = std::min(runs.capture.run_slots(), total - i);
        for (uint32_t k = 0; k < run; k++) numbered_record(i + k, raw + k * go::kStoreRecordBytes);
        REQUIRE(runs.capture.append(raw, run, 0) == go::Append::Ok);
        i += run;
    }

    CHECK(run_flash.bytes() == single_flash.bytes());
    CHECK(runs.capture.session_records() == total);
    CHECK(run_flash.writes * 8 < single_flash.writes);
}
