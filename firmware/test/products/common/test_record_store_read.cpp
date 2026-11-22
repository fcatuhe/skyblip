// A capture read back while its ring rotates: every record keeps the index it was listed under.
#include <cstdint>
#include <string>
#include <vector>

#include "core/diagnostics/payload.h"
#include "doctest/doctest.h"
#include "test/support/log_transfer.h"
#include "test/support/store_rig.h"

using namespace skyblip;

namespace {

constexpr uint32_t kCaptureSession = 9000;
constexpr uint32_t kFirstAtS = 1785628800;
constexpr store::SectorOwner kDiagnostics = store::SectorOwner::Diagnostics;

struct Chunk {
    uint32_t from{0};
    std::vector<uint32_t> numbers{};
    bool ends_session{false};
};

// Each record carries its write order in its instant, so a read says which record it got.
void write_numbered(StoreRig& rig, uint32_t& next, uint32_t count) {
    for (uint32_t i = 0; i < count; i++, next++) {
        diagnostics::Instant at{};
        at.at_s = kFirstAtS + next;
        at.utc_dated = true;
        uint8_t raw[common::kStoreRecordBytes]{};
        diagnostics::encode_record(diagnostics::record_of(diagnostics::Gnss{}, at), raw);
        REQUIRE(rig.capture.append(raw, 0) == common::Append::Ok);
    }
}

// Writes until the ring opens a sector by recycling the oldest one of this session.
void rotate_one_sector(StoreRig& rig, uint32_t& next) {
    const uint32_t lost = rig.pool.allocator().lost_sectors(kDiagnostics);
    while (rig.pool.allocator().lost_sectors(kDiagnostics) == lost) write_numbered(rig, next, 1);
}

uint32_t number_of(const std::string& text, const char* key) {
    return static_cast<uint32_t>(std::stoul(field(text, key)));
}

std::vector<Chunk> chunks_sent(const StoreRig& rig) {
    std::vector<Chunk> chunks;
    for (const auto& frame : rig.link.sent) {
        const std::string text = link_text(frame.bytes);
        if (field(text, "command") != "chunk") continue;
        Chunk chunk{number_of(text, "from"), {}, field(text, "ends_session") == "true"};
        uint8_t raw[comms::kLogChunkRawBytes];
        const int bytes = chunk_records(frame.bytes, raw, sizeof(raw));
        REQUIRE(bytes >= 0);
        for (int at = 0; at < bytes; at += static_cast<int>(common::kStoreRecordBytes)) {
            diagnostics::Record record{};
            REQUIRE(diagnostics::decode_record(raw + at, record) == Status::Ok);
            chunk.numbers.push_back(record.at_s - kFirstAtS);
        }
        chunks.push_back(chunk);
    }
    return chunks;
}

void ask_read(StoreRig& rig, uint32_t from) {
    comms::LogRequest request{};
    request.command = comms::LogCommand::Read;
    request.store = kDiagnostics;
    request.peer = events::Peer{rig.link.session_id(), rig.link.connection(rig.link.session_id())};
    request.session = kCaptureSession;
    request.from = from;
    request.count = comms::kMaxLogReadChunks;
    request.understood = true;
    rig.capture.serve(request);
}

std::vector<Chunk> read_window(StoreRig& rig, uint32_t from) {
    rig.link.clear();
    ask_read(rig, from);
    return chunks_sent(rig);
}

void check_each_record_at_its_index(const std::vector<Chunk>& chunks) {
    for (const Chunk& chunk : chunks)
        for (uint32_t i = 0; i < chunk.numbers.size(); i++)
            CHECK(chunk.numbers[i] == chunk.from + i);
}

std::string listed_capture(StoreRig& rig) {
    rig.capture.rebuild_index();
    return rig.listed(rig.capture, kDiagnostics, 0);
}

}  // namespace

// The trial of #293 fetched a capture as its ring rotated and lost a sector at each rotation.
TEST_CASE("record store: a fetch while the ring rotates reads every record it listed once") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.capture.begin_session(kCaptureSession);
    const uint32_t slots = rig.pool.slots_per_sector();
    uint32_t next = 0;
    write_numbered(rig, next, slots * 3 + 50);
    const uint32_t listed = number_of(listed_capture(rig), "records");
    REQUIRE(listed == slots * 3 + 50);

    uint32_t at = 0;
    uint32_t read = 0;
    bool rotated = false;
    bool ends_session = false;
    while (!ends_session && read <= listed) {
        const std::vector<Chunk> chunks = read_window(rig, at);
        REQUIRE_FALSE(chunks.empty());
        check_each_record_at_its_index(chunks);
        for (const Chunk& chunk : chunks) {
            CHECK(chunk.from == at);
            at = chunk.from + static_cast<uint32_t>(chunk.numbers.size());
            read += static_cast<uint32_t>(chunk.numbers.size());
            ends_session = chunk.ends_session;
        }
        if (!rotated && at > slots) {
            rotate_one_sector(rig, next);
            rotated = true;
        }
    }
    CHECK(rotated);
    CHECK(at == listed);
    CHECK(read == listed);
}

TEST_CASE("record store: a rotated session lists the index of the first record it still holds") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.capture.begin_session(kCaptureSession);
    const uint32_t slots = rig.pool.slots_per_sector();
    uint32_t next = 0;
    write_numbered(rig, next, slots * 3 + 50);
    rotate_one_sector(rig, next);

    const std::string listed = listed_capture(rig);
    CHECK(number_of(listed, "first") == slots);
    CHECK(number_of(listed, "records") == next - slots);
    CHECK(field(listed, "truncated") == "true");
}

TEST_CASE("record store: a read of records the ring recycled starts at the first one it holds") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.capture.begin_session(kCaptureSession);
    const uint32_t slots = rig.pool.slots_per_sector();
    uint32_t next = 0;
    write_numbered(rig, next, slots * 3 + 50);
    listed_capture(rig);
    rotate_one_sector(rig, next);

    const std::vector<Chunk> chunks = read_window(rig, 0);
    REQUIRE_FALSE(chunks.empty());
    CHECK(chunks.front().from == slots);
    CHECK(chunks.front().numbers.front() == slots);
    check_each_record_at_its_index(chunks);
}

// A window goes out over several passes, and the ring may recycle the sector it is reading.
TEST_CASE("record store: a window the ring rotates under skips only the records it recycled") {
    platform::host::FlashRegion flash{4};
    StoreRig rig{flash};
    rig.open();
    rig.capture.begin_session(kCaptureSession);
    const uint32_t slots = rig.pool.slots_per_sector();
    uint32_t next = 0;
    write_numbered(rig, next, slots * 3 + 50);
    listed_capture(rig);
    rig.link.clear();
    rig.link.serve();
    rig.link.hold_after(2);

    ask_read(rig, 0);
    REQUIRE(rig.capture.reading());
    rotate_one_sector(rig, next);
    while (rig.capture.reading()) {
        rig.link.serve();
        REQUIRE(is_ok(rig.pool.deliver_held(0)));
        rig.capture.continue_read();
    }
    rig.link.serve();
    REQUIRE(is_ok(rig.pool.deliver_held(0)));

    const std::vector<Chunk> chunks = chunks_sent(rig);
    REQUIRE(chunks.size() == comms::kMaxLogReadChunks);
    const uint32_t per_chunk = static_cast<uint32_t>(chunks.front().numbers.size());
    CHECK(chunks[2].from == 2 * per_chunk);
    CHECK(chunks[3].from == slots);
    CHECK(chunks.back().from == slots + 4 * per_chunk);
    check_each_record_at_its_index(chunks);
}
