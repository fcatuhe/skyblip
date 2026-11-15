// The keys a unit reads off its own MCUboot decide which images it installs over the air.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/dfu/bootloader_keys.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return out;
}

const std::vector<uint8_t> kDevelopmentPubDer = from_hex(
    "3059301306072a8648ce3d020106082a8648ce3d030107034200044dc5f5e98c0c0daf12064a310cae38bcdb11"
    "f8532c45c797e598d3e419b8b179ea44a1c9ef66d1f1c459be49bd4c8fda630def98ea01b52b6c214903299db4"
    "5d");
const std::vector<uint8_t> kProductionPubDer = from_hex(
    "3059301306072a8648ce3d020106082a8648ce3d0301070342000403c94ae6e02b057712f0db4fac4a96fe77e1"
    "a47dd0b9f2290f6eaab2af6ffd4c0941c6c07a7cd8ef54de5ecbf9c55610e53e94c70705f4b1eeafbb4a2088ab"
    "d2");

class Bootloader : public dfu::ImageReader {
   public:
    explicit Bootloader(size_t size) : bytes_(size) {
        for (size_t i = 0; i < size; i++) bytes_[i] = static_cast<uint8_t>((i * 13 + 5) & 0xff);
    }

    void embed(const std::vector<uint8_t>& key, size_t at) {
        std::memcpy(bytes_.data() + at, key.data(), key.size());
    }

    uint32_t bytes() const override { return static_cast<uint32_t>(bytes_.size()); }

    bool read(uint32_t offset, uint8_t* out, size_t bytes) override {
        if (offset == failing_offset || offset + bytes > bytes_.size()) return false;
        std::memcpy(out, bytes_.data() + offset, bytes);
        return true;
    }

    std::vector<uint8_t> bytes_;
    uint32_t failing_offset{0xFFFFFFFFu};
};

std::string id_of(const ports::SigningKeyHash& key) {
    char text[9];
    std::snprintf(text, sizeof(text), "%02x%02x%02x%02x", key[0], key[1], key[2], key[3]);
    return text;
}

}  // namespace

TEST_CASE("bootloader keys: a development bootloader names the development and production keys") {
    // 212602f2 and 88a4bef5 are the KEYHASH prefixes CI's images carry, imgtool dumpinfo.
    Bootloader bootloader(0x3000);
    bootloader.embed(kDevelopmentPubDer, 0x0f80);
    bootloader.embed(kProductionPubDer, 0x2fa5);
    ports::BootloaderKeys keys;
    REQUIRE(dfu::find_bootloader_keys(bootloader, keys));
    REQUIRE(keys.count == 2);
    CHECK(id_of(keys.keys[0]) == "212602f2");
    CHECK(id_of(keys.keys[1]) == "88a4bef5");
}

TEST_CASE("bootloader keys: a key across a read boundary, or at the very end, is found once") {
    Bootloader bootloader(0x1000);
    bootloader.embed(kProductionPubDer, 0x100 - 40);
    bootloader.embed(kProductionPubDer, 0x1000 - kProductionPubDer.size());
    ports::BootloaderKeys keys;
    REQUIRE(dfu::find_bootloader_keys(bootloader, keys));
    CHECK(keys.count == 1);
    CHECK(id_of(keys.keys[0]) == "88a4bef5");
}

TEST_CASE("bootloader keys: no key or a failed read is no answer, never an empty trust list") {
    Bootloader keyless(0x1000);
    ports::BootloaderKeys keys;
    CHECK_FALSE(dfu::find_bootloader_keys(keyless, keys));

    Bootloader unreadable(0x1000);
    unreadable.embed(kDevelopmentPubDer, 0x10);
    unreadable.failing_offset = 0x200;
    CHECK_FALSE(dfu::find_bootloader_keys(unreadable, keys));
}
