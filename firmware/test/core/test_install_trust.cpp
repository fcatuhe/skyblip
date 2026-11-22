// The record a .uf2 from Briefing carries, read as schemas/trust_record.v1.vectors.json writes it.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/comms/install_trust.h"
#include "core/util/crc32.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "hardware/platform/host/watchdog.h"
#include "simulator/world/hex.h"
#include "simulator/world/json.h"

using namespace skyblip;

namespace {

class Vectors {
   public:
    static constexpr const char* kPath = "../schemas/trust_record.v1.vectors.json";

    Vectors() {
        std::FILE* file = std::fopen(kPath, "rb");
        REQUIRE_MESSAGE(file != nullptr, "cannot open ", kPath);
        char chunk[512];
        size_t count = 0;
        while ((count = std::fread(chunk, 1, sizeof(chunk), file)) > 0) text_.append(chunk, count);
        std::fclose(file);
    }

    std::string text(const char* key) const {
        char value[512] = {0};
        REQUIRE_MESSAGE(reader().read_string(key, value, sizeof(value)), "no key ", key);
        return value;
    }

    uint32_t number(const char* key) const {
        long value = 0;
        REQUIRE_MESSAGE(reader().read_integer(key, value), "no key ", key);
        return static_cast<uint32_t>(value);
    }

    std::vector<uint8_t> bytes(const char* key) const {
        const std::string hex_text = text(key);
        std::vector<uint8_t> out(hex_text.size() / 2);
        REQUIRE(hex::read(hex_text.c_str(), ByteSpan(out.data(), out.size())));
        return out;
    }

   private:
    simulator::json::Reader reader() const {
        return simulator::json::Reader(text_.data(), static_cast<int>(text_.size()));
    }

    std::string text_;
};

void reseal(std::vector<uint8_t>& record) {
    const uint32_t crc = crc32(record.data(), 73);
    for (int i = 0; i < 4; i++)
        record[73 + static_cast<size_t>(i)] = static_cast<uint8_t>(crc >> (8 * i));
}

std::optional<comms::InstallTrustRecord> read(const std::vector<uint8_t>& record) {
    return comms::read_install_trust_record(ConstByteSpan(record.data(), record.size()));
}

void put_uint16(std::vector<uint8_t>& out, size_t at, uint16_t value) {
    out[at] = static_cast<uint8_t>(value);
    out[at + 1] = static_cast<uint8_t>(value >> 8);
}

void put_uint32(std::vector<uint8_t>& out, size_t at, uint32_t value) {
    for (int i = 0; i < 4; i++)
        out[at + static_cast<size_t>(i)] = static_cast<uint8_t>(value >> (8 * i));
}

std::vector<uint8_t> slot0(const Vectors& vectors, const std::vector<uint8_t>& record) {
    std::vector<uint8_t> slot(vectors.number("x1_slot0_bytes"), 0xFF);
    put_uint32(slot, 0, 0x96f3b83d);
    put_uint16(slot, 8, static_cast<uint16_t>(vectors.number("header_bytes")));
    put_uint16(slot, 10, static_cast<uint16_t>(vectors.number("protected_tlv_bytes")));
    put_uint32(slot, 12, vectors.number("image_bytes"));
    const size_t protected_at = vectors.number("header_bytes") + vectors.number("image_bytes");
    put_uint16(slot, protected_at, 0x6908);
    put_uint16(slot, protected_at + 2,
               static_cast<uint16_t>(vectors.number("protected_tlv_bytes")));
    const size_t unprotected_at = protected_at + vectors.number("protected_tlv_bytes");
    put_uint16(slot, unprotected_at, 0x6907);
    put_uint16(slot, unprotected_at + 2, static_cast<uint16_t>(vectors.number("tlv_total")));
    std::memcpy(slot.data() + vectors.number("page_offset"), record.data(), record.size());
    return slot;
}

struct Slots {
    platform::host::Watchdog watchdog;
    platform::host::Dfu dfu{watchdog};
};

}  // namespace

TEST_CASE("install trust: the shared vector reads as the browser's key and label") {
    const Vectors vectors;
    const std::vector<uint8_t> record = vectors.bytes("record");
    REQUIRE(record.size() == comms::kInstallTrustRecordBytes);
    const std::optional<comms::InstallTrustRecord> read_back = read(record);
    REQUIRE(read_back);
    CHECK(std::vector<uint8_t>(read_back->key.begin(), read_back->key.end()) ==
          vectors.bytes("key"));
    CHECK(std::string(read_back->label) == vectors.text("label"));
    CHECK_FALSE(read(vectors.bytes("record_with_a_bad_crc")));
}

TEST_CASE("install trust: the browser key in the vector is the one its seed derives") {
    const Vectors vectors;
    crypto::Ed25519::Seed seed{};
    const std::vector<uint8_t> seed_bytes = vectors.bytes("browser_seed");
    std::memcpy(seed.data(), seed_bytes.data(), seed.size());
    const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(seed);
    CHECK(std::vector<uint8_t>(key.begin(), key.end()) == vectors.bytes("key"));
}

// Each refusal is silent: the unit boots as it would with no record at all.
TEST_CASE("install trust: a record with the wrong magic, a long label or dirty padding is none") {
    const Vectors vectors;
    std::vector<uint8_t> magic = vectors.bytes("record");
    magic[15] = '2';
    reseal(magic);
    CHECK_FALSE(read(magic));

    std::vector<uint8_t> long_label = vectors.bytes("record");
    long_label[48] = 25;
    reseal(long_label);
    CHECK_FALSE(read(long_label));

    std::vector<uint8_t> padding = vectors.bytes("record");
    padding[72] = 'x';
    reseal(padding);
    CHECK_FALSE(read(padding));

    std::vector<uint8_t> broken = vectors.bytes("record");
    broken[49] = 0xC0;
    reseal(broken);
    CHECK_FALSE(read(broken));

    std::vector<uint8_t> nul = vectors.bytes("record");
    nul[50] = 0;
    reseal(nul);
    CHECK_FALSE(read(nul));

    std::vector<uint8_t> empty = vectors.bytes("record");
    empty[48] = 0;
    std::memset(empty.data() + 49, 0, 24);
    reseal(empty);
    const std::optional<comms::InstallTrustRecord> unlabelled = read(empty);
    REQUIRE(unlabelled);
    CHECK(std::string(unlabelled->label).empty());

    std::vector<uint8_t> cut_short = vectors.bytes("record");
    cut_short.pop_back();
    CHECK_FALSE(read(cut_short));
}

TEST_CASE(
    "install trust: the page is the first one past the image, and only clear of the trailer") {
    const Vectors vectors;
    CHECK(
        comms::install_trust_page(vectors.number("image_end"), vectors.number("x1_slot0_bytes")) ==
        vectors.number("page_offset"));
    CHECK(vectors.number("x1_slot0_start") + vectors.number("page_offset") ==
          vectors.number("x1_page_address"));
    CHECK(vectors.number("t_echo_slot0_start") + vectors.number("page_offset") ==
          vectors.number("t_echo_page_address"));
    CHECK(comms::install_trust_page(4096, 0xB5000) == 4096u);
    const uint32_t last = vectors.number("x1_last_image_end");
    CHECK(comms::install_trust_page(last, vectors.number("x1_slot0_bytes")) == last);
    CHECK_FALSE(comms::install_trust_page(vectors.number("x1_first_image_end_with_no_page"),
                                          vectors.number("x1_slot0_bytes")));
    CHECK_FALSE(comms::install_trust_page(0, 4096));
}

TEST_CASE("install trust: a record in slot0 adds its key once, and is erased only once it landed") {
    const Vectors vectors;
    Slots slots;
    slots.dfu.running_slot = slot0(vectors, vectors.bytes("record"));
    comms::TrustedBrowsers browsers;
    comms::InstallTrust trust;
    REQUIRE(trust.adopt(slots.dfu, browsers));
    REQUIRE(browsers.count() == 1);
    CHECK(std::string(browsers.at(0).label) == vectors.text("label"));
    CHECK_FALSE(trust.erase_due());
    CHECK_FALSE(trust.erase(slots.dfu));
    CHECK(slots.dfu.page_erases == 0);

    comms::InstallTrust next_boot;
    REQUIRE(next_boot.adopt(slots.dfu, browsers));
    CHECK(browsers.count() == 1);

    next_boot.landed();
    REQUIRE(next_boot.erase_due());
    CHECK(next_boot.erase(slots.dfu));
    CHECK(slots.dfu.page_erases == 1);
    CHECK(slots.dfu.running_slot[vectors.number("page_offset")] == 0xFF);
    CHECK_FALSE(next_boot.erase_due());
    CHECK_FALSE(comms::InstallTrust{}.adopt(slots.dfu, browsers));
}

TEST_CASE("install trust: a slot with no image, or a page that will not erase, keeps what it had") {
    const Vectors vectors;
    Slots slots;
    comms::TrustedBrowsers browsers;
    comms::InstallTrust trust;
    CHECK_FALSE(trust.adopt(slots.dfu, browsers));

    std::vector<uint8_t> headless = slot0(vectors, vectors.bytes("record"));
    headless[0] = 0;
    slots.dfu.running_slot = headless;
    CHECK_FALSE(trust.adopt(slots.dfu, browsers));
    CHECK(browsers.count() == 0);

    slots.dfu.running_slot = slot0(vectors, vectors.bytes("record"));
    REQUIRE(trust.adopt(slots.dfu, browsers));
    trust.landed();
    slots.dfu.page_erase_fails = true;
    CHECK_FALSE(trust.erase(slots.dfu));
    CHECK(trust.erase_due());
    slots.dfu.page_erase_fails = false;
    CHECK(trust.erase(slots.dfu));
}
