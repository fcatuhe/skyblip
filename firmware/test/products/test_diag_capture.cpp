// The capture end to end: what arming writes, what the floor refuses, what reads back.
#include <cstring>
#include <string>

#include "core/diag/payload.h"
#include "core/events/link.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/services/capture.h"
#include "products/skyblip_go/services/flight_log.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"

using namespace skyblip;
using skyblip::reads_in;

namespace {

int base64_decode(const std::string& in, uint8_t* out, int cap) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    int n = 0;
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : in) {
        const int v = value(c);
        if (v < 0) continue;
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits < 8) continue;
        bits -= 8;
        if (n < cap) out[n++] = static_cast<uint8_t>((buffer >> bits) & 0xFF);
    }
    return n;
}

std::string field(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    const size_t at = json.find(needle);
    if (at == std::string::npos) return "";
    size_t start = at + needle.size();
    if (json[start] == '"') {
        const size_t end = json.find('"', start + 1);
        return json.substr(start + 1, end - start - 1);
    }
    const size_t end = json.find_first_of(",}", start);
    return json.substr(start, end - start);
}

std::string last_log(Rig& rig) {
    const auto& sent = rig.platform.link().sent;
    for (size_t i = sent.size(); i > 0; i--)
        if (sent[i - 1].endpoint == events::Endpoint::Log) return sent[i - 1].bytes;
    return "";
}

std::string ask(Rig& rig, uint32_t& t, const char* json) {
    rig.platform.link().clear();
    rig.send_log(json);
    rig.run(t, t + 200);
    t += 200;
    return last_log(rig);
}

void taxi(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 0, 300); }
void fly(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 50000, 800); }

struct Capture {
    uint32_t records{0};
    int chunks{0};
    bool eof{false};
    bool decoded{true};
    int gaps{0};
    int flights{0};
};

Capture offload_capture(Rig& rig, uint32_t& t, uint32_t session) {
    Capture walk{};
    uint32_t from = 0;
    while (!walk.eof && walk.chunks < 400) {
        char command[128];
        std::snprintf(command, sizeof(command),
                      "{\"cmd\":\"read\",\"log\":\"diagnostics\",\"session\":%u,\"from\":%u}",
                      session, from);
        const std::string chunk = ask(rig, t, command);
        if (chunk.find("\"cmd\":\"chunk\"") == std::string::npos) {
            walk.decoded = false;
            return walk;
        }
        if (field(chunk, "log") != "diagnostics") walk.decoded = false;

        const int n = std::atoi(field(chunk, "n").c_str());
        uint8_t raw[comms::kLogChunkRawBytes];
        const int bytes = base64_decode(field(chunk, "data"), raw, sizeof(raw));
        if (bytes != n * static_cast<int>(diag::kRecordBytes)) walk.decoded = false;
        for (int i = 0; i < n; i++) {
            diag::Record record{};
            if (diag::decode_record(raw + i * diag::kRecordBytes, record) != Status::Ok) {
                walk.decoded = false;
                return walk;
            }
            if (record.type == diag::Type::Gap) walk.gaps++;
            if (record.type == diag::Type::Flight) walk.flights++;
            walk.records++;
        }
        from += static_cast<uint32_t>(n);
        walk.eof = field(chunk, "eof") == "true";
        walk.chunks++;
    }
    return walk;
}

void open_capture_page(Rig& rig, uint32_t& t) {
    rig.show(t, go::Page::Capture);
    rig.run(t, t + 2000);
    t += 2000;
    REQUIRE(rig.product.screen().page() == go::Page::Capture);
}

void arm_from_the_page(Rig& rig, uint32_t& t) {
    open_capture_page(rig, t);
    rig.double_press(t);
    rig.run(t, t + 200);
    t += 200;
}

void arm_the_power_run(Rig& rig, uint32_t& t) {
    open_capture_page(rig, t);
    rig.tap(t);
    rig.run(t, t + 200);
    t += 200;
    rig.double_press(t);
    rig.run(t, t + 200);
    t += 200;
}

diag::Record bench_record(uint32_t at_s) {
    diag::Instant at{};
    at.at_s = at_s;
    at.utc_dated = true;
    diag::Gnss value{};
    value.sats = 9;
    value.fix_valid = true;
    return diag::record_of(value, at);
}

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

uint32_t feed(Rig& rig, uint32_t& t, uint32_t records) {
    uint32_t pushed = 0;
    while (pushed < records && rig.product.capture().capturing()) {
        for (int i = 0; i < diag::Recorder::kCapacity && pushed < records; i++)
            if (rig.product.diag().record(bench_record(Rig::kUtcBase + pushed))) pushed++;
        rig.run(t, t + 50);
        t += 50;
    }
    rig.run(t, t + 200);
    t += 200;
    return pushed;
}

}  // namespace

TEST_CASE("capture: the diagnostics page states the price, and one press does not arm it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);

    open_capture_page(rig, t);
    const bus::CaptureState& price = rig.state().capture;
    CHECK(price.available);
    CHECK(price.pool_sectors == platform::host::FlashRegion::kSectorCount);
    CHECK(price.price_sectors == platform::host::FlashRegion::kSectorCount);
    CHECK(price.keeps_s > 0);
    CHECK(reads_in(rig.product.screen().framebuffer(), "TAKES 330 OF 330 SECTORS", 0, 0, 200, 200));
    CHECK(reads_in(rig.product.screen().framebuffer(), "PRESS TWICE TO ARM", 0, 0, 200, 200));

    rig.press(t);
    rig.run(t, t + 2000);
    t += 2000;
    CHECK_FALSE(rig.product.diag().armed());

    rig.double_press(t);
    rig.run(t, t + 1200);
    t += 1200;
    CHECK(rig.product.diag().armed());
    CHECK(rig.product.diag().profile() == diag::Profile::Full);
    CHECK(rig.product.capture().capturing());
    CHECK(reads_in(rig.product.screen().framebuffer(), "STATE ARMED FULL", 0, 0, 200, 200));
    CHECK(reads_in(rig.product.screen().framebuffer(), "PRESS TWICE TO STOP", 0, 0, 200, 200));
}

TEST_CASE("capture: the page prices both captures, and the pad picks between them") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    open_capture_page(rig, t);

    const go::CaptureService& capture = rig.product.capture();
    CHECK(capture.keeps_s(diag::Profile::Full) > 0);
    CHECK(capture.keeps_s(diag::Profile::PowerRun) > 100 * capture.keeps_s(diag::Profile::Full));

    const go::Glass& glass = rig.product.screen().framebuffer();
    CHECK(reads_in(glass, "FULL", 0, 0, 200, 200, 1, /*ink=*/false));
    CHECK(reads_in(glass, "POWER RUN", 0, 0, 200, 200));

    rig.tap(t);
    rig.run(t, t + 1200);
    t += 1200;
    CHECK(rig.product.screen().page() == go::Page::Capture);
    CHECK(reads_in(glass, "POWER RUN", 0, 0, 200, 200, 1, /*ink=*/false));
    CHECK(reads_in(glass, "FULL", 0, 0, 200, 200));
}

TEST_CASE("capture: the double press arms the capture in focus, and no other") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_the_power_run(rig, t);

    CHECK(rig.product.diag().armed());
    CHECK(rig.product.diag().profile() == diag::Profile::PowerRun);
    CHECK(rig.product.capture().capturing());

    taxi(rig, t, 5);
    CHECK(reads_in(rig.product.screen().framebuffer(), "STATE ARMED POWER RUN", 0, 0, 200, 200));
    // A capture recording two subjects every 30 s outlasts one recording eleven a second.
    CHECK(rig.state().capture.keeps_s > 10 * rig.product.capture().keeps_s(diag::Profile::Full));
}

// The pad walks off the last capture rather than trapping the thumb on the page.
TEST_CASE("capture: a second tap hands the page back to the menu it was opened from") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    open_capture_page(rig, t);

    rig.tap(t);
    CHECK(rig.product.screen().page() == go::Page::Capture);
    rig.tap(t);
    CHECK(rig.product.screen().page() == go::page_after(go::Page::Capture));
}

TEST_CASE("capture: what an armed device recorded comes back through the offload") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_from_the_page(rig, t);
    REQUIRE(rig.product.diag().armed());
    const uint32_t session = rig.product.capture().session_id();
    CHECK(session >= Rig::kUtcBase);

    taxi(rig, t, 30);
    CHECK(rig.product.capture().records_written() > 10);
    CHECK(rig.product.capture().sectors_owned() * flight::kLogSlotsPerSector >=
          rig.product.capture().records_written());
    CHECK(rig.product.capture().stopped() == bus::CaptureStop::None);

    rig.double_press(t);
    rig.run(t, t + 1000);
    t += 1000;
    CHECK_FALSE(rig.product.diag().armed());
    CHECK_FALSE(rig.product.capture().capturing());
    const uint32_t written = rig.product.capture().records_written();

    const std::string count = ask(rig, t, "{\"cmd\":\"list\",\"log\":\"diagnostics\"}");
    CHECK(field(count, "log") == "diagnostics");
    CHECK(field(count, "sessions") == "1");

    const std::string listed =
        ask(rig, t, "{\"cmd\":\"list\",\"log\":\"diagnostics\",\"index\":0}");
    CHECK(field(listed, "session") == std::to_string(session));
    CHECK(field(listed, "records") == std::to_string(written));
    CHECK(field(listed, "closed") == "true");

    const Capture walk = offload_capture(rig, t, session);
    CHECK(walk.decoded);
    CHECK(walk.eof);
    CHECK(walk.records == written);
    CHECK(walk.flights > 10);
}

TEST_CASE("capture: a windowed read answers the chunks it was asked for, and stops at eof") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_from_the_page(rig, t);
    const uint32_t session = rig.product.capture().session_id();
    feed(rig, t, 60);
    rig.double_press(t);
    rig.run(t, t + 1000);
    t += 1000;
    const uint32_t written = rig.product.capture().records_written();
    REQUIRE(written > 40);

    rig.raise_link();
    const int per_chunk = comms::log_records_per_chunk(rig.platform.link().payload_bytes(),
                                                       comms::LogStore::Diagnostics);
    REQUIRE(per_chunk > 0);
    char command[128];
    std::snprintf(command, sizeof(command),
                  "{\"cmd\":\"read\",\"log\":\"diagnostics\",\"session\":%u,\"from\":0,"
                  "\"count\":3}",
                  session);
    rig.platform.link().clear();
    rig.send_log(command);
    rig.run(t, t + 200);
    t += 200;

    int chunks = 0;
    uint32_t records = 0;
    for (const auto& frame : rig.platform.link().sent) {
        if (frame.endpoint != events::Endpoint::Log) continue;
        chunks++;
        records += static_cast<uint32_t>(std::atoi(field(frame.bytes, "n").c_str()));
    }
    CHECK(chunks == 3);
    CHECK(records == static_cast<uint32_t>(3 * per_chunk));
    CHECK(rig.product.flight_log().link_drops() == 0);
}

TEST_CASE("capture: both stores are offloaded in one session, each under its own selector") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_from_the_page(rig, t);
    taxi(rig, t, 10);
    fly(rig, t, 60);
    taxi(rig, t, 40);
    const uint32_t flight = rig.product.flight_log().session_id();
    const uint32_t capture = rig.product.capture().session_id();
    REQUIRE(flight != capture);

    const std::string flights = ask(rig, t, "{\"cmd\":\"list\"}");
    CHECK(field(flights, "log") == "");
    CHECK(field(flights, "sessions") == "1");

    const std::string diagnostics = ask(rig, t, "{\"cmd\":\"list\",\"log\":\"diagnostics\"}");
    CHECK(field(diagnostics, "log") == "diagnostics");
    CHECK(field(diagnostics, "sessions") == "1");

    char command[128];
    std::snprintf(command, sizeof(command), "{\"cmd\":\"read\",\"session\":%u,\"from\":0}", flight);
    const std::string flight_chunk = ask(rig, t, command);
    CHECK(field(flight_chunk, "log") == "");
    CHECK(field(flight_chunk, "session") == std::to_string(flight));

    const Capture walk = offload_capture(rig, t, capture);
    CHECK(walk.decoded);
    CHECK(walk.eof);
    CHECK(walk.records > 0);
    CHECK(rig.product.flight_log().link_drops() == 0);
}

TEST_CASE("capture: a boot comes up disarmed even with a capture on the flash") {
    Rig flight;
    REQUIRE(flight.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(flight, t, 5);
    arm_the_power_run(flight, t);
    REQUIRE(flight.product.diag().profile() == diag::Profile::PowerRun);
    taxi(flight, t, 20);
    const uint32_t session = flight.product.capture().session_id();
    const uint32_t written = flight.product.capture().records_written();
    REQUIRE(written > 0);
    REQUIRE(flight.product.diag().armed());

    Rig rebooted;
    rebooted.platform.log_flash().restore(flight.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);
    uint32_t rt = 100;
    taxi(rebooted, rt, 20);

    CHECK_FALSE(rebooted.product.diag().armed());
    CHECK(rebooted.product.diag().profile() == diag::Profile::Full);
    CHECK_FALSE(rebooted.product.capture().capturing());
    CHECK(rebooted.product.capture().records_written() == 0);

    const std::string count = ask(rebooted, rt, "{\"cmd\":\"list\",\"log\":\"diagnostics\"}");
    CHECK(field(count, "sessions") == "1");
    const Capture walk = offload_capture(rebooted, rt, session);
    CHECK(walk.decoded);
    CHECK(walk.records == written);
}

TEST_CASE("capture: records the ring had to drop reach the flash as a gap") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_from_the_page(rig, t);
    const uint32_t session = rig.product.capture().session_id();

    uint32_t refused = 0;
    for (int i = 0; i < diag::Recorder::kCapacity * 2; i++)
        if (!rig.product.diag().record(bench_record(Rig::kUtcBase + static_cast<uint32_t>(i))))
            refused++;
    CHECK(refused > 0);
    CHECK(rig.product.capture().records_dropped() == refused);

    taxi(rig, t, 5);
    rig.double_press(t);
    rig.run(t, t + 1000);
    t += 1000;

    const Capture walk = offload_capture(rig, t, session);
    CHECK(walk.decoded);
    CHECK(walk.gaps == 1);
}

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

// The queue dies with the rails: the drain has to run after the radio is asleep.
TEST_CASE("capture: what is still queued reaches the flash on the way to power off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_from_the_page(rig, t);
    REQUIRE(rig.product.capture().capturing());
    const uint32_t session = rig.product.capture().session_id();

    // Stopped where own-ship may key the PA, so the ring still holds records.
    while (!rig.state().rf.plan.tx_allowed) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        t += 10;
    }
    for (int i = 0; i < 20; i++) rig.product.diag().record(bench_record(Rig::kUtcBase + i));
    rig.platform.clock().set_millis(t);
    rig.product.step(t);
    t += 10;
    const int queued = rig.product.diag().queued();
    REQUIRE(queued > 0);
    const uint32_t written = rig.product.capture().records_written();

    rig.product.shutdown().request(power::ShutdownReason::LinkRequest, t);
    rig.platform.clock().set_millis(t);
    rig.product.step(t);
    t += 10;
    REQUIRE(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);

    CHECK(rig.product.diag().queued() == 0);
    CHECK_FALSE(rig.product.capture().capturing());
    CHECK(rig.product.capture().records_written() >= written + static_cast<uint32_t>(queued));

    Rig rebooted;
    rebooted.platform.log_flash().restore(rig.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);
    uint32_t rt = 100;
    taxi(rebooted, rt, 5);
    const std::string listed =
        ask(rebooted, rt, "{\"cmd\":\"list\",\"log\":\"diagnostics\",\"index\":0}");
    CHECK(field(listed, "session") == std::to_string(session));
    CHECK(field(listed, "closed") == "true");
    CHECK(field(listed, "truncated") == "false");
}

TEST_CASE("capture: a device with no partition offers no capture to arm") {
    constexpr ports::Capabilities kNoStorage = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Storage));
    Rig rig{kNoStorage};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);

    open_capture_page(rig, t);
    CHECK_FALSE(rig.state().capture.available);
    CHECK(reads_in(rig.product.screen().framebuffer(), "NO FLASH FITTED", 0, 0, 200, 200));

    rig.double_press(t);
    rig.run(t, t + 1000);
    t += 1000;
    CHECK_FALSE(rig.product.diag().armed());
    CHECK_FALSE(rig.product.capture().capturing());
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
