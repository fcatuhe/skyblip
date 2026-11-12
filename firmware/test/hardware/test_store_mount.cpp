// What the settings store does with what another firmware left in its sectors.
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/dfu/update.h"
#include "core/settings/blob.h"
#include "doctest/doctest.h"
#include "hardware/platform/store_mount.h"
#include "products/skyblip_bravo/settings.h"
#include "products/skyblip_go/settings.h"

using namespace skyblip;
using platform::MountAttempt;
using platform::StoreMount;

namespace {

constexpr size_t kSectorBytes = 4096;
constexpr int kSectors = 3;

struct Flash {
    Flash() { sectors.fill(std::vector<uint8_t>(kSectorBytes, 0xFF)); }

    bool read_word(int sector, uint32_t& word) {
        if (!readable) return false;
        std::memcpy(&word, sectors[sector].data(), sizeof(word));
        return true;
    }

    bool erase() {
        erases++;
        if (!erasable) return false;
        for (auto& sector : sectors) sector.assign(kSectorBytes, 0xFF);
        return true;
    }

    MountAttempt mount() {
        mounts++;
        if (fails_on_the_flash) return MountAttempt::Failed;
        if (refused_while_dirty && !erased()) return MountAttempt::RefusedContent;
        return refused ? MountAttempt::RefusedContent : MountAttempt::Mounted;
    }

    bool erased() const {
        for (const auto& sector : sectors)
            for (uint8_t byte : sector)
                if (byte != 0xFF) return false;
        return true;
    }

    void put_word(int sector, uint32_t word) {
        std::memcpy(sectors[sector].data(), &word, sizeof(word));
    }

    std::array<std::vector<uint8_t>, kSectors> sectors;
    bool readable{true};
    bool erasable{true};
    bool refused{false};
    bool refused_while_dirty{false};
    bool fails_on_the_flash{false};
    int erases{0};
    int mounts{0};
};

uint32_t first_word(const uint8_t* bytes) {
    uint32_t word = 0;
    std::memcpy(&word, bytes, sizeof(word));
    return word;
}

}  // namespace

TEST_CASE("store mount: an erased partition mounts as it is") {
    Flash flash;
    CHECK(platform::mount_store(flash, kSectors) == StoreMount::Mounted);
    CHECK(flash.erases == 0);
    CHECK(flash.mounts == 1);
}

// A ring magic opens on 'W' then 'R', and our values open on a blob version or a major and minor.
TEST_CASE("store mount: every value this firmware stores opens on a word no ring page opens on") {
    uint8_t blob[64] = {};
    const uint8_t payload[8] = {0x57, 0x52, 0x4E, 0x33, 0x57, 0x52, 0x4E, 0x33};
    for (const uint8_t version : {bravo::kBlobVersion, go::kBlobVersion}) {
        settings::seal(version, payload, sizeof(payload), blob, sizeof(blob));
        CHECK_FALSE(platform::is_warm_node_store_page(first_word(blob)));
    }
    dfu::UpdateRecord record{};
    record.from = {1, 4, 0x524E, 0};
    record.to = {1, 5, 0, 0};
    uint8_t update[dfu::kUpdateRecordBytes] = {};
    REQUIRE(dfu::to_blob(record, update, sizeof(update)) > 0);
    CHECK_FALSE(platform::is_warm_node_store_page(first_word(update)));
}

TEST_CASE("store mount: our own records mount untouched") {
    Flash flash;
    uint8_t blob[16] = {};
    const uint8_t payload[4] = {0x57, 0x52, 0x4E, 0x33};
    settings::seal(bravo::kBlobVersion, payload, sizeof(payload), blob, sizeof(blob));
    std::memcpy(flash.sectors[0].data(), blob, sizeof(blob));
    CHECK(platform::mount_store(flash, kSectors) == StoreMount::Mounted);
    CHECK(flash.erases == 0);
    CHECK_FALSE(flash.erased());
}

// Meshtastic 2.8.0 and later keep a WarmNodeStore ring on our sectors, and NVS can mount over it.
TEST_CASE("store mount: a Meshtastic WarmNodeStore page is erased before NVS reads it") {
    for (const uint32_t magic : {0x474E5257u, 0x324E5257u, 0x334E5257u}) {
        for (int sector = 0; sector < kSectors; sector++) {
            Flash flash;
            flash.put_word(sector, magic);
            CHECK(platform::mount_store(flash, kSectors) == StoreMount::ClearedForeignRing);
            CHECK(flash.erases == 1);
            CHECK(flash.erased());
            CHECK(flash.mounts == 1);
        }
    }
}

TEST_CASE("store mount: a ring erased but still refused reports down without a second erase") {
    Flash flash;
    flash.put_word(2, platform::kWarmRingMagicV3);
    flash.refused = true;
    CHECK(platform::mount_store(flash, kSectors) == StoreMount::Down);
    CHECK(flash.erases == 1);
    CHECK(flash.mounts == 1);
}

TEST_CASE("store mount: a failed erase reports down without mounting the foreign data") {
    Flash flash;
    flash.put_word(0, platform::kWarmRingMagicV3);
    flash.erasable = false;
    CHECK(platform::mount_store(flash, kSectors) == StoreMount::Down);
    CHECK(flash.mounts == 0);
}

TEST_CASE("store mount: a partition NVS refuses for what it holds is erased and mounted again") {
    Flash flash;
    flash.put_word(1, 0x12345678);
    flash.refused_while_dirty = true;
    CHECK(platform::mount_store(flash, kSectors) == StoreMount::ClearedRefused);
    CHECK(flash.erases == 1);
    CHECK(flash.mounts == 2);
}

TEST_CASE("store mount: a store that NVS refuses even erased reports down") {
    Flash flash;
    flash.refused = true;
    CHECK(platform::mount_store(flash, kSectors) == StoreMount::Down);
    CHECK(flash.erases == 1);
    CHECK(flash.mounts == 2);
}

// An erase cures what the sectors hold, never a flash that fails or a store configured wrong.
TEST_CASE("store mount: a mount that fails on the flash erases nothing") {
    Flash flash;
    flash.put_word(0, 0x00020001);
    flash.fails_on_the_flash = true;
    CHECK(platform::mount_store(flash, kSectors) == StoreMount::Down);
    CHECK(flash.erases == 0);
    CHECK_FALSE(flash.erased());
}

TEST_CASE("store mount: a partition that cannot be read is left to the mount to judge") {
    Flash flash;
    flash.readable = false;
    CHECK(platform::mount_store(flash, kSectors) == StoreMount::Mounted);
    CHECK(flash.erases == 0);
}
