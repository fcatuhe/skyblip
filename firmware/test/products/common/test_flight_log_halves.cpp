// The flight log at 2 Hz: each record is its whole second's solution, read back off the flash.
#include <cstdint>
#include <map>
#include <vector>

#include "core/flight/log_record.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "test/support/half_second_feed.h"
#include "test/support/log_record_write.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// Every flight record on the part, as a tablet decodes it.
std::vector<flight::LogRecord> logged(Rig& rig) {
    std::vector<flight::LogRecord> out;
    platform::host::FlashRegion& flash = rig.platform.log_flash();
    std::vector<uint8_t> raw(flash.sector_bytes());
    for (uint32_t sector = 0; sector < flash.sector_count(); sector++) {
        REQUIRE(is_ok(flash.read(sector * flash.sector_bytes(), raw.data(), flash.sector_bytes())));
        store::SectorHeader header{};
        if (store::decode_sector_header(raw.data(), header) != Status::Ok) continue;
        if (header.owner != store::SectorOwner::Flights) continue;
        for (uint32_t slot = 0;
             flight::log_record_offset(slot) + flight::kLogRecordBytes <= raw.size(); slot++) {
            flight::LogRecord record{};
            if (flight::decode_log_record(raw.data() + flight::log_record_offset(slot),
                                          header.session_id, record) == Status::Ok)
                out.push_back(record);
        }
    }
    return out;
}

}  // namespace

// The pinned case: a record dated by its second held the solution solved 500 ms after it.
TEST_CASE(
    "flight log: a record holds its whole second's position, and a second with no .000 none") {
    Rig rig{kBarometerByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    constexpr int32_t kFlyingMmS = 30000;
    std::map<uint32_t, int32_t> whole_latitude_1e7;
    for (uint32_t second = 1; second <= 90; second++) {
        for (uint32_t half = 0; half < 2; half++) {
            if (half == 0 && second % 10 == 7) continue;
            solve(rig, second, half, kFlyingMmS, updates);
            if (half == 0) whole_latitude_1e7[kBaseUtcS + second] = rig.state().own.latitude_1e7;
            const uint32_t landed_ms = second * 1000 + half * kHalfSecondFeedMs + kFeedLatencyMs;
            rig.run(landed_ms + 50, landed_ms + kHalfSecondFeedMs - 50);
        }
    }

    const std::vector<flight::LogRecord> records = logged(rig);
    REQUIRE(records.size() >= 40);
    std::map<uint32_t, int> per_second;
    for (const flight::LogRecord& record : records) per_second[record.utc_s]++;
    for (const auto& second : per_second) {
        CAPTURE(second.first);
        CHECK(second.second == 1);
    }
    for (const flight::LogRecord& record : records) {
        CAPTURE(record.utc_s);
        REQUIRE(whole_latitude_1e7.count(record.utc_s) == 1);
        flight::LogRecord expected{};
        expected.latitude_1e7 = whole_latitude_1e7[record.utc_s];
        uint8_t raw[flight::kLogRecordBytes];
        flight::encode_log_record(expected, record.utc_s, raw);
        flight::LogRecord read_back{};
        REQUIRE(flight::decode_log_record(raw, record.utc_s, read_back) == Status::Ok);
        CHECK(record.latitude_1e7 == read_back.latitude_1e7);
    }
}
