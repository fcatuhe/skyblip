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
    UpdateRecord r;
    r.from = ports::ImageVersion{0, 1, 0, 12};
    r.to = ports::ImageVersion{0, 2, 0, 15};
    r.from_hash = ports::ImageHash{{0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18}};
    r.to_hash = ports::ImageHash{{0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28}};
    r.hashed = true;
    return r;
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
    const UpdateRecord r = attempt();
    const SlotImage to = image(r.to, r.to_hash);
    CHECK(outcome(r, to, std::nullopt) == Outcome::Landed);
    CHECK(outcome(r, image(r.from, r.from_hash), to) == Outcome::Reverted);
    CHECK(outcome(r, image(ports::ImageVersion{0, 3, 0, 1}, ports::ImageHash{}), to) ==
          Outcome::Unrelated);
}

TEST_CASE("dfu: a running image that cannot read its own hash is classified by version") {
    const UpdateRecord r = attempt();
    CHECK(outcome(r, image(r.to), std::nullopt) == Outcome::Landed);
    CHECK(outcome(r, image(r.from), image(r.to)) == Outcome::Reverted);
    CHECK(outcome(r, image(ports::ImageVersion{0, 2, 0, 16}), std::nullopt) == Outcome::Unrelated);
}

TEST_CASE("dfu: an old image with the new one gone from slot 1 was refused, never run") {
    const UpdateRecord r = attempt();
    CHECK(outcome(r, image(r.from, r.from_hash), std::nullopt) == Outcome::Refused);
    CHECK(outcome(r, image(r.from, r.from_hash), image(r.from, r.from_hash)) == Outcome::Refused);
}

TEST_CASE("dfu: two builds of one release are two images") {
    const UpdateRecord r = attempt();
    ports::ImageVersion other_build = r.to;
    other_build.build++;
    CHECK(r.to != other_build);
    CHECK(outcome(r, image(other_build), std::nullopt) == Outcome::Unrelated);
}

// The full image of the running version, reverted: by version alone it would read as landed.
TEST_CASE("dfu: an image of the same version is told apart by its hash") {
    UpdateRecord r = attempt();
    r.to = r.from;
    CHECK(outcome(r, image(r.from, r.from_hash), image(r.to, r.to_hash)) == Outcome::Reverted);
    CHECK(outcome(r, image(r.from, r.from_hash), std::nullopt) == Outcome::Refused);
    CHECK(outcome(r, image(r.to, r.to_hash), std::nullopt) == Outcome::Landed);
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
    char text[kVersionTextCap];
    CHECK(format_version(ports::ImageVersion{0, 1, 0, 12}, text, sizeof(text)) == 8);
    CHECK(std::string(text) == "0.1.0+12");
    const int widest =
        format_version(ports::ImageVersion{255, 255, 65535, 4294967295u}, text, sizeof(text));
    CHECK(std::string(text) == "255.255.65535+4294967295");
    CHECK(widest == static_cast<int>(kVersionTextCap) - 1);

    char tight[8];
    CHECK(format_version(ports::ImageVersion{0, 1, 0, 12}, tight, sizeof(tight)) == 0);
    CHECK(tight[0] == 0);
}

TEST_CASE("dfu: a version reads back from the text format_version writes, and nothing looser") {
    char text[kVersionTextCap];
    for (const ports::ImageVersion& v :
         {ports::ImageVersion{0, 2, 0, 15}, ports::ImageVersion{255, 255, 65535, 4294967295u}}) {
        format_version(v, text, sizeof(text));
        ports::ImageVersion read;
        REQUIRE(parse_version(text, read));
        CHECK(read == v);
    }

    ports::ImageVersion untouched{9, 9, 9, 9};
    for (const char* bad : {"", "0.2.0", "0.2.0+", "0.2.0.15", "0.2.0+15x", "256.0.0+1",
                            "0.0.65536+1", "0.0.0+4294967296", "-1.2.0+15", "0..0+1"}) {
        CHECK_FALSE(parse_version(bad, untouched));
    }
    CHECK(untouched == ports::ImageVersion{9, 9, 9, 9});
}

TEST_CASE("dfu: every image state has a name a phone can switch on") {
    CHECK(std::string(to_string(ImageState::Confirmed)) == "confirmed");
    CHECK(std::string(to_string(ImageState::Probation)) == "probation");
    CHECK(std::string(to_string(ImageState::Reverted)) == "reverted");
    CHECK(std::string(to_string(ImageState::Refused)) == "refused");
}

// What imgtool wrote for skyblip-go 0.1.0+892, cut down to the fields the device reads.
namespace {
std::vector<uint8_t> header_of(ports::ImageVersion v) {
    std::vector<uint8_t> h(kImageHeaderBytes, 0);
    const uint8_t magic[] = {0x3d, 0xb8, 0xf3, 0x96};
    std::memcpy(h.data(), magic, sizeof(magic));
    h[8] = 0x00;
    h[9] = 0x02;
    h[12] = 0xd0;
    h[13] = 0x6d;
    h[14] = 0x07;
    h[20] = v.major;
    h[21] = v.minor;
    h[22] = static_cast<uint8_t>(v.revision);
    h[23] = static_cast<uint8_t>(v.revision >> 8);
    for (int i = 0; i < 4; i++) h[24 + i] = static_cast<uint8_t>(v.build >> (8 * i));
    return h;
}

void put_tlv(std::vector<uint8_t>& out, uint16_t type, uint8_t fill, uint16_t len) {
    out.push_back(static_cast<uint8_t>(type));
    out.push_back(static_cast<uint8_t>(type >> 8));
    out.push_back(static_cast<uint8_t>(len));
    out.push_back(static_cast<uint8_t>(len >> 8));
    out.insert(out.end(), len, fill);
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
    const std::vector<uint8_t> h = header_of({0, 1, 0, 892});
    ImageHeader out;
    REQUIRE(read_header(h.data(), h.size(), out));
    CHECK(out.version == ports::ImageVersion{0, 1, 0, 892});
    // 0x200 of header and 0x76dd0 of body: imgtool dumpinfo puts the TLV area at 0x76fd0.
    CHECK(out.tlv_offset() == 0x76fd0u);
}

TEST_CASE("dfu: a short or foreign header is not read as an image") {
    std::vector<uint8_t> h = header_of({0, 1, 0, 892});
    ImageHeader out;
    CHECK_FALSE(read_header(h.data(), h.size() - 1, out));
    h[0] ^= 0xff;
    CHECK_FALSE(read_header(h.data(), h.size(), out));
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

TEST_CASE("dfu: a key is named by its first four bytes, the way a page compares it") {
    ports::SigningKeyHash key{};
    key[0] = 0x21;
    key[1] = 0x26;
    key[2] = 0x02;
    key[3] = 0xf2;
    CHECK(key_prefix(key) == 0x212602f2u);
}

// The SMP permission matrix off the silicon; the Zephyr hook pins the numbers with static_assert.
namespace {
constexpr uint8_t kRead = static_cast<uint8_t>(SmpOp::Read);
constexpr uint8_t kWrite = static_cast<uint8_t>(SmpOp::Write);
constexpr uint16_t kOs = static_cast<uint16_t>(SmpGroup::Os);
constexpr uint16_t kImage = static_cast<uint16_t>(SmpGroup::Image);
}  // namespace

TEST_CASE("smp: every read passes, in or out of the upload window") {
    for (const bool window : {false, true}) {
        CHECK(smp_permitted({kOs, kSmpOsEcho, kRead}, window));
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
        CHECK_FALSE(smp_permitted({kOs, kSmpOsReset, kWrite}, window));
        CHECK_FALSE(smp_permitted({7, 3, kWrite}, window));
    }
}

TEST_CASE(
    "smp: echo is a write a phone may always make, and a response opcode is never a request") {
    CHECK(smp_permitted({kOs, kSmpOsEcho, kWrite}, false));
    CHECK_FALSE(smp_permitted({kOs, kSmpOsEcho, static_cast<uint8_t>(SmpOp::WriteResponse)}, true));
    CHECK_FALSE(
        smp_permitted({kImage, kSmpImageUpload, static_cast<uint8_t>(SmpOp::ReadResponse)}, true));
}

TEST_CASE("dfu: the hub's image is reported as a word, or as the digest a page compares with") {
    char text[kHubImageTextCap];
    HubImageReport report{};
    format_hub_image(report, text, sizeof(text));
    CHECK(std::string(text) == "none");
    report.holding = HubImage::Missing;
    format_hub_image(report, text, sizeof(text));
    CHECK(std::string(text) == "missing");
    report.holding = HubImage::Writing;
    format_hub_image(report, text, sizeof(text));
    CHECK(std::string(text) == "writing");

    report.holding = HubImage::Held;
    const uint8_t digest[] = {0x31, 0x8d, 0xef, 0x51, 0x1f, 0xd8, 0xeb, 0x76};
    std::memcpy(report.digest, digest, sizeof(digest));
    CHECK(format_hub_image(report, text, sizeof(text)) == 16);
    CHECK(std::string(text) == "318def511fd8eb76");

    char tight[kHubImageTextCap - 1];
    CHECK(format_hub_image(report, tight, sizeof(tight)) == 0);
    CHECK(tight[0] == 0);
}
