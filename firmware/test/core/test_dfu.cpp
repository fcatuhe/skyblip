// The record a device writes before the swap, and what the image that boots next makes of it.
#include <cstring>
#include <initializer_list>
#include <string>

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

RunningImage running(const ports::ImageVersion& version) {
    return RunningImage{version, {}, false};
}

RunningImage running(const ports::ImageVersion& version, const ports::ImageHash& hash) {
    return RunningImage{version, hash, true};
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
    CHECK(outcome(after, running(before.to, before.to_hash)) == Outcome::Landed);
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
    CHECK(outcome(r, running(r.to, r.to_hash)) == Outcome::Landed);
    CHECK(outcome(r, running(r.from, r.from_hash)) == Outcome::Reverted);
    CHECK(outcome(r, running(ports::ImageVersion{0, 3, 0, 1}, ports::ImageHash{})) ==
          Outcome::Unrelated);
}

TEST_CASE("dfu: a running image that cannot read its own hash is classified by version") {
    const UpdateRecord r = attempt();
    CHECK(outcome(r, running(r.to)) == Outcome::Landed);
    CHECK(outcome(r, running(r.from)) == Outcome::Reverted);
    CHECK(outcome(r, running(ports::ImageVersion{0, 2, 0, 16})) == Outcome::Unrelated);
}

TEST_CASE("dfu: two builds of one release are two images") {
    const UpdateRecord r = attempt();
    ports::ImageVersion other_build = r.to;
    other_build.build++;
    CHECK(r.to != other_build);
    CHECK(outcome(r, running(other_build)) == Outcome::Unrelated);
}

// The full image of the running version, reverted: by version alone it would read as landed.
TEST_CASE("dfu: an image of the same version is told apart by its hash") {
    UpdateRecord r = attempt();
    r.to = r.from;
    CHECK(outcome(r, running(r.from, r.from_hash)) == Outcome::Reverted);
    CHECK(outcome(r, running(r.to, r.to_hash)) == Outcome::Landed);
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
