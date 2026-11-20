// Bravo asks its AG3335 for one solution a second, and every service reads one a second.
#include <cstdint>
#include <map>
#include <vector>

#include "core/flight/log_record.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

std::vector<flight::LogRecord> logged(BravoRig& rig) {
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

static_assert(bravo::kSolutionRate == ports::SolutionRate::Second,
              "Bravo transmits nothing, so it asks one solution a second");

TEST_CASE("bravo: one solution a second reaches own-ship, the EFB and the flight log") {
    BravoRig rig;
    rig.gnss_on_air = true;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    std::map<uint32_t, int> own_seconds;
    uint32_t last_utc_s = 0;
    for (uint32_t elapsed = 0; elapsed < 180000; elapsed += runtime::kServicePassCeilingMs) {
        rig.run_for(runtime::kServicePassCeilingMs);
        const model::Ownship& second = rig.state().own_second;
        if (!second.utc_valid || second.utc_s == last_utc_s) continue;
        last_utc_s = second.utc_s;
        own_seconds[second.utc_s]++;
    }

    REQUIRE(own_seconds.size() >= 150);
    CHECK(own_seconds.rbegin()->first - own_seconds.begin()->first + 1 == own_seconds.size());
    CHECK(rig.platform.gnss().solution_period_ms == 1000);
    CHECK(rig.count_of("$GPRMC") == static_cast<int>(own_seconds.size()));
    CHECK(rig.count_of("$GPGGA") == static_cast<int>(own_seconds.size()));
    const std::vector<flight::LogRecord> records = logged(rig);
    REQUIRE(records.size() == own_seconds.size());
    for (size_t at = 1; at < records.size(); at++) {
        CAPTURE(at);
        CHECK(records[at].utc_s == records[at - 1].utc_s + 1);
    }
}
