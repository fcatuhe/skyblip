// The record a device writes before the swap, and what the image that boots next makes of it.
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/dfu/image.h"
#include "core/dfu/smp_policy.h"
#include "core/dfu/update.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::dfu;

namespace {
UpdateRecord attempt() {
    UpdateRecord record;
    record.from = ports::ImageVersion{0, 1, 0, 12};
    record.to = ports::ImageVersion{0, 2, 0, 15};
    record.from_hash = ports::ImageHash{{0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18}};
    record.to_hash = ports::ImageHash{{0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28}};
    record.hashed = true;
    return record;
}

SlotImage image(const ports::ImageVersion& version) { return SlotImage{version, {}, false}; }

SlotImage image(const ports::ImageVersion& version, const ports::ImageHash& hash) {
    return SlotImage{version, hash, true};
}
}  // namespace

TEST_CASE("dfu: the update record survives the blob byte for byte") {
    const UpdateRecord before = attempt();
    uint8_t blob[kUpdateRecordBytes];
    REQUIRE(to_blob(before, blob, sizeof(blob)) == kUpdateRecordBytes);
    UpdateRecord after;
    REQUIRE(from_blob(blob, sizeof(blob), after));
    CHECK(after.from == before.from);
    CHECK(after.to == before.to);
    CHECK(after.from_hash == before.from_hash);
    CHECK(after.to_hash == before.to_hash);
    CHECK(after.hashed);
}

// The record an image from before the hashes wrote, read by the image that boots after the swap.
TEST_CASE("dfu: a record of versions alone is still read, and classified by version") {
    UpdateRecord before = attempt();
    before.hashed = false;
    uint8_t blob[kUpdateRecordBytes];
    REQUIRE(to_blob(before, blob, sizeof(blob)) == kVersionsOnlyRecordBytes);
    UpdateRecord after;
    REQUIRE(from_blob(blob, kVersionsOnlyRecordBytes, after));
    CHECK(after.from == before.from);
    CHECK(after.to == before.to);
    CHECK_FALSE(after.hashed);
    CHECK(outcome(after, image(before.to, before.to_hash), std::nullopt) == Outcome::Landed);
}

TEST_CASE("dfu: a blob of the wrong length is refused rather than half read") {
    uint8_t blob[kUpdateRecordBytes] = {0};
    UpdateRecord out;
    CHECK_FALSE(from_blob(blob, kUpdateRecordBytes - 1, out));
    CHECK_FALSE(from_blob(blob, kVersionsOnlyRecordBytes + 1, out));
    CHECK_FALSE(from_blob(blob, 0, out));
    uint8_t small[kUpdateRecordBytes - 1];
    CHECK(to_blob(attempt(), small, sizeof(small)) == 0);
}

TEST_CASE("dfu: the image that boots after the swap is classified by what it is running") {
    const UpdateRecord record = attempt();
    const SlotImage to = image(record.to, record.to_hash);
    CHECK(outcome(record, to, std::nullopt) == Outcome::Landed);
    CHECK(outcome(record, image(record.from, record.from_hash), to) == Outcome::Reverted);
    CHECK(outcome(record, image(ports::ImageVersion{0, 3, 0, 1}, ports::ImageHash{}), to) ==
          Outcome::Unrelated);
}

TEST_CASE("dfu: a running image that cannot read its own hash is classified by version") {
    const UpdateRecord record = attempt();
    CHECK(outcome(record, image(record.to), std::nullopt) == Outcome::Landed);
    CHECK(outcome(record, image(record.from), image(record.to)) == Outcome::Reverted);
    CHECK(outcome(record, image(ports::ImageVersion{0, 2, 0, 16}), std::nullopt) ==
          Outcome::Unrelated);
}

TEST_CASE("dfu: an old image with the new one gone from slot 1 was refused, never run") {
    const UpdateRecord record = attempt();
    CHECK(outcome(record, image(record.from, record.from_hash), std::nullopt) == Outcome::Refused);
    CHECK(outcome(record, image(record.from, record.from_hash),
                  image(record.from, record.from_hash)) == Outcome::Refused);
}

TEST_CASE("dfu: two builds of one release are two images") {
    const UpdateRecord record = attempt();
    ports::ImageVersion other_build = record.to;
    other_build.build++;
    CHECK(record.to != other_build);
    CHECK(outcome(record, image(other_build), std::nullopt) == Outcome::Unrelated);
}

// The full image of the running version, reverted: by version alone it would read as landed.
TEST_CASE("dfu: an image of the same version is told apart by its hash") {
    UpdateRecord record = attempt();
    record.to = record.from;
    CHECK(outcome(record, image(record.from, record.from_hash), image(record.to, record.to_hash)) ==
          Outcome::Reverted);
    CHECK(outcome(record, image(record.from, record.from_hash), std::nullopt) == Outcome::Refused);
    CHECK(outcome(record, image(record.to, record.to_hash), std::nullopt) == Outcome::Landed);
}

TEST_CASE("dfu: versions order by major, minor, revision, then build") {
    CHECK(compare({0, 2, 0, 1}, {0, 1, 9, 5000}) > 0);
    CHECK(compare({1, 0, 0, 0}, {0, 255, 65535, 4294967295u}) > 0);
    CHECK(compare({0, 1, 0, 12}, {0, 1, 0, 13}) < 0);
    CHECK(compare({0, 1, 0, 12}, {0, 1, 0, 12}) == 0);
}

TEST_CASE("dfu: a release unit refuses only an older image, a development unit none") {
    const ports::ImageVersion running{0, 2, 0, 15};
    CHECK(std::string(version_refusal(running, {0, 2, 0, 14}, false)) == "older");
    CHECK(version_refusal(running, running, false) == nullptr);
    CHECK(version_refusal(running, {0, 2, 0, 16}, false) == nullptr);
    CHECK(version_refusal(running, {0, 1, 0, 900}, true) == nullptr);
}

TEST_CASE("dfu: a version reads as imgtool stamps it, and the widest one fits the cap") {
    char text[kVersionTextBytes];
    CHECK(format_version(ports::ImageVersion{0, 1, 0, 12}, text, sizeof(text)) == 8);
    CHECK(std::string(text) == "0.1.0+12");
    const int widest = format_version(kWidestVersion, text, sizeof(text));
    CHECK(std::string(text) == "9.99.99+99999");
    CHECK(widest == static_cast<int>(kVersionTextBytes) - 1);

    char tight[8];
    CHECK(format_version(ports::ImageVersion{0, 1, 0, 12}, tight, sizeof(tight)) == 0);
    CHECK(tight[0] == 0);
}

// An image from someone else's build can carry any version, and the frame sized for ours must hold.
TEST_CASE("dfu: a version wider than the widest we sign is written as nothing") {
    char text[kVersionTextBytes];
    for (const ports::ImageVersion& version :
         {ports::ImageVersion{10, 0, 0, 1}, ports::ImageVersion{0, 100, 0, 1},
          ports::ImageVersion{0, 0, 100, 1}, ports::ImageVersion{0, 0, 0, 100000},
          ports::ImageVersion{255, 255, 65535, 4294967295u}}) {
        text[0] = 'x';
        CHECK(format_version(version, text, sizeof(text)) == 0);
        CHECK(text[0] == 0);
    }
}

TEST_CASE("dfu: a version reads back from the text format_version writes, and nothing looser") {
    char text[kVersionTextBytes];
    for (const ports::ImageVersion& version : {ports::ImageVersion{0, 2, 0, 15}, kWidestVersion}) {
        format_version(version, text, sizeof(text));
        ports::ImageVersion read;
        REQUIRE(parse_version(text, read));
        CHECK(read == version);
    }

    ports::ImageVersion untouched{9, 9, 9, 9};
    for (const char* bad : {"", "0.2.0", "0.2.0+", "0.2.0.15", "0.2.0+15x", "10.0.0+1", "0.100.0+1",
                            "0.0.100+1", "0.0.0+100000", "-1.2.0+15", "0..0+1"}) {
        CHECK_FALSE(parse_version(bad, untouched));
    }
    CHECK(untouched == ports::ImageVersion{9, 9, 9, 9});
}

// What imgtool wrote for skyblip-go 0.1.0+892, cut down to the fields the device reads.
namespace {
std::vector<uint8_t> header_of(ports::ImageVersion version) {
    std::vector<uint8_t> header(kImageHeaderBytes, 0);
    const uint8_t magic[] = {0x3d, 0xb8, 0xf3, 0x96};
    std::memcpy(header.data(), magic, sizeof(magic));
    header[8] = 0x00;
    header[9] = 0x02;
    header[12] = 0xd0;
    header[13] = 0x6d;
    header[14] = 0x07;
    header[20] = version.major;
    header[21] = version.minor;
    header[22] = static_cast<uint8_t>(version.revision);
    header[23] = static_cast<uint8_t>(version.revision >> 8);
    for (int i = 0; i < 4; i++) header[24 + i] = static_cast<uint8_t>(version.build >> (8 * i));
    return header;
}

void put_tlv(std::vector<uint8_t>& out, uint16_t type, uint8_t fill, uint16_t value_bytes) {
    out.push_back(static_cast<uint8_t>(type));
    out.push_back(static_cast<uint8_t>(type >> 8));
    out.push_back(static_cast<uint8_t>(value_bytes));
    out.push_back(static_cast<uint8_t>(value_bytes >> 8));
    out.insert(out.end(), value_bytes, fill);
}

std::vector<uint8_t> tlv_area(uint16_t magic, const std::vector<uint8_t>& entries) {
    std::vector<uint8_t> area = {static_cast<uint8_t>(magic), static_cast<uint8_t>(magic >> 8)};
    const size_t total = entries.size() + 4;
    area.push_back(static_cast<uint8_t>(total));
    area.push_back(static_cast<uint8_t>(total >> 8));
    area.insert(area.end(), entries.begin(), entries.end());
    return area;
}

std::vector<uint8_t> signed_tlvs(uint8_t key_byte) {
    std::vector<uint8_t> entries;
    put_tlv(entries, 0x10, 0xaa, 32);
    put_tlv(entries, 0x01, key_byte, 32);
    put_tlv(entries, 0x22, 0xbb, 72);
    return tlv_area(0x6907, entries);
}
}  // namespace

TEST_CASE("dfu: the header gives the version and where the TLVs start") {
    const std::vector<uint8_t> header = header_of({0, 1, 0, 892});
    ImageHeader out;
    REQUIRE(read_header(header.data(), header.size(), out));
    CHECK(out.version == ports::ImageVersion{0, 1, 0, 892});
    // 0x200 of header and 0x76dd0 of body: imgtool dumpinfo puts the TLV area at 0x76fd0.
    CHECK(out.tlv_offset() == 0x76fd0u);
}

TEST_CASE("dfu: a short or foreign header is not read as an image") {
    std::vector<uint8_t> header = header_of({0, 1, 0, 892});
    ImageHeader out;
    CHECK_FALSE(read_header(header.data(), header.size() - 1, out));
    header[0] ^= 0xff;
    CHECK_FALSE(read_header(header.data(), header.size(), out));
    CHECK_FALSE(read_header(nullptr, 0, out));
}

TEST_CASE("dfu: the key hash is found among the TLVs, behind a protected area too") {
    ports::SigningKeyHash key{};
    const std::vector<uint8_t> plain = signed_tlvs(0x21);
    REQUIRE(find_key_hash(plain.data(), plain.size(), key));
    CHECK(key[0] == 0x21);
    CHECK(key[31] == 0x21);

    std::vector<uint8_t> counter;
    put_tlv(counter, 0x50, 0x01, 4);
    std::vector<uint8_t> both = tlv_area(0x6908, counter);
    const std::vector<uint8_t> after = signed_tlvs(0x42);
    both.insert(both.end(), after.begin(), after.end());
    REQUIRE(find_key_hash(both.data(), both.size(), key));
    CHECK(key[0] == 0x42);
}

TEST_CASE("dfu: no key hash, a cut area or a wrong length finds none") {
    ports::SigningKeyHash key{};
    std::vector<uint8_t> entries;
    put_tlv(entries, 0x10, 0xaa, 32);
    const std::vector<uint8_t> unsigned_area = tlv_area(0x6907, entries);
    CHECK_FALSE(find_key_hash(unsigned_area.data(), unsigned_area.size(), key));

    const std::vector<uint8_t> whole = signed_tlvs(0x21);
    CHECK_FALSE(find_key_hash(whole.data(), 60, key));

    std::vector<uint8_t> short_key;
    put_tlv(short_key, 0x01, 0x21, 16);
    const std::vector<uint8_t> odd = tlv_area(0x6907, short_key);
    CHECK_FALSE(find_key_hash(odd.data(), odd.size(), key));
}

// The SMP permission matrix off the silicon; the Zephyr hook pins the numbers with static_assert.
namespace {
constexpr uint8_t kRead = static_cast<uint8_t>(SmpOperation::Read);
constexpr uint8_t kWrite = static_cast<uint8_t>(SmpOperation::Write);
constexpr uint16_t kOperatingSystem = static_cast<uint16_t>(SmpGroup::OperatingSystem);
constexpr uint16_t kImage = static_cast<uint16_t>(SmpGroup::Image);
}  // namespace

TEST_CASE("smp: every read passes, in or out of the upload window") {
    for (const bool window : {false, true}) {
        CHECK(smp_permitted({kOperatingSystem, kSmpOperatingSystemEcho, kRead}, window));
        CHECK(smp_permitted({kImage, kSmpImageState, kRead}, window));
        CHECK(smp_permitted({kImage, kSmpImageUpload, kRead}, window));
        CHECK(smp_permitted({7, 3, kRead}, window));
    }
}

TEST_CASE("smp: the upload is the one write the window opens") {
    CHECK_FALSE(smp_permitted({kImage, kSmpImageUpload, kWrite}, false));
    CHECK(smp_permitted({kImage, kSmpImageUpload, kWrite}, true));
}

TEST_CASE(
    "smp: marking pending, confirming, erasing and resetting are refused even inside the window") {
    for (const bool window : {false, true}) {
        CHECK_FALSE(smp_permitted({kImage, kSmpImageState, kWrite}, window));
        CHECK_FALSE(smp_permitted({kImage, kSmpImageErase, kWrite}, window));
        CHECK_FALSE(smp_permitted({kOperatingSystem, kSmpOperatingSystemReset, kWrite}, window));
        CHECK_FALSE(smp_permitted({7, 3, kWrite}, window));
    }
}

TEST_CASE(
    "smp: echo is a write a phone may always make, and a response opcode is never a request") {
    CHECK(smp_permitted({kOperatingSystem, kSmpOperatingSystemEcho, kWrite}, false));
    CHECK_FALSE(smp_permitted({kOperatingSystem, kSmpOperatingSystemEcho,
                               static_cast<uint8_t>(SmpOperation::WriteResponse)},
                              true));
    CHECK_FALSE(smp_permitted(
        {kImage, kSmpImageUpload, static_cast<uint8_t>(SmpOperation::ReadResponse)}, true));
}
