// Harness, not a test: the diagnostics corpus as a laptop reads it back off the rig's flash.
#ifndef SKYBLIP_TEST_SUPPORT_DIAG_CORPUS_H
#define SKYBLIP_TEST_SUPPORT_DIAG_CORPUS_H

#include <vector>

#include "core/diag/payload.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

namespace skyblip {

// Every slot of every sector the diagnostics ring owns, in the order the sectors sit on the part.
inline std::vector<diag::Record> captured(Rig& rig) {
    std::vector<diag::Record> out;
    platform::host::FlashRegion& flash = rig.platform.log_flash();
    std::vector<uint8_t> raw(flash.sector_bytes());
    for (uint32_t sector = 0; sector < flash.sector_count(); sector++) {
        REQUIRE(is_ok(flash.read(sector * flash.sector_bytes(), raw.data(), flash.sector_bytes())));
        store::SectorHeader header{};
        if (store::decode_sector_header(raw.data(), header) != Status::Ok) continue;
        if (header.owner != store::SectorOwner::Diagnostics) continue;
        for (uint32_t at = store::kSectorHeaderBytes; at + diag::kRecordBytes <= raw.size();
             at += diag::kRecordBytes) {
            diag::Record record{};
            if (diag::decode_record(raw.data() + at, record) == Status::Ok) out.push_back(record);
        }
    }
    return out;
}

}  // namespace skyblip

#endif
