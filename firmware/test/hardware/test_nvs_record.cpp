// Every key the products store lands on a record of its own, since NVS keeps only a 16-bit id.
#include <cstddef>
#include <cstdint>
#include <set>

#include "doctest/doctest.h"
#include "hardware/platform/nvs_record.h"
#include "products/common/services/config_link.h"

using namespace skyblip;
using common::ConfigLinkService;
using platform::nvs_record_id;

namespace {

constexpr const char* kStoredKeys[] = {
    ConfigLinkService::kSettingsKey, ConfigLinkService::kBrowsersKey,
    ConfigLinkService::kUpdateKey,   ConfigLinkService::kPriorSettingsKey,
    ConfigLinkService::kReportKey,
};

}  // namespace

TEST_CASE("nvs record: no two stored keys share a record id, and none takes NVS's own") {
    std::set<uint16_t> ids;
    for (const char* key : kStoredKeys) {
        INFO(key);
        const uint16_t id = nvs_record_id(key);
        CHECK(id != 0);
        CHECK(id != platform::kNvsReservedRecordId);
        CHECK(ids.insert(id).second);
    }
    CHECK(ids.size() == std::size(kStoredKeys));
}

// FNV-1a of "a" is 0xE40C292C, folded to its low 16 bits with the low bit forced.
TEST_CASE("nvs record: a key's id is its FNV-1a hash folded to 16 bits, never 0") {
    CHECK(nvs_record_id("a") == 0x292D);
}
