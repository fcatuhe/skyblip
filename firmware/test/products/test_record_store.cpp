// The two rings on one partition: what a claim costs, what a refused write keeps.
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

go::Append append_one(go::RecordStore& store) {
    uint8_t raw[go::kStoreRecordBytes]{};
    diag::encode_record(diag::record_of(diag::Gnss{}, diag::Instant{}), raw);
    return store.append(raw, 0);
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
