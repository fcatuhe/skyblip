// What a unit checks in slot 1 before it asks MCUboot for a swap (#278).
#include <cstring>
#include <string>
#include <vector>

#include "core/dfu/image.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return out;
}

// imgtool signed 300 body bytes of (i * 7 + 3) & 0xff, with --pad-header, --vid and --cid.
std::vector<uint8_t> signed_image() {
    std::vector<uint8_t> image =
        from_hex("3db8f3960000000020002c002c01000000000000000100000700000000000000");
    for (int i = 0; i < 300; i++) image.push_back(static_cast<uint8_t>((i * 7 + 3) & 0xff));
    const std::vector<uint8_t> tlvs = from_hex(
        "08692c0074001000b6e3b0ae1d5b55ea8007e9e12850d63a7500100046d97790da405396a3547794"
        "56a0946507699600100020008b0aa2e6d5a67ed0034eaf150a99af385b78dbf93e5b6fd9d6af5dd0"
        "f21b7add010020003da9b792d5d27683a76e9d4ab377a115c08204c8e19d31d8b156c946fb06af1b"
        "220046003044022060f7a4a9f0f71ebd9df631a96cb996b9b2c1f68b1f34946bba7d91c731b542f5"
        "02202d990a1d54d9c272ae078ed430db15d152c6d59ea4112b0660faef6230e5abd7");
    image.insert(image.end(), tlvs.begin(), tlvs.end());
    return image;
}

class SlotImage : public dfu::ImageReader {
   public:
    explicit SlotImage(std::vector<uint8_t> bytes, size_t slot_bytes = 0x1000)
        : bytes_(std::move(bytes)) {
        bytes_.resize(slot_bytes, 0xff);
    }

    uint32_t bytes() const override { return static_cast<uint32_t>(bytes_.size()); }

    bool read(uint32_t offset, uint8_t* out, size_t bytes) override {
        if (offset == failing_offset) return false;
        if (offset + bytes > bytes_.size()) return false;
        std::memcpy(out, bytes_.data() + offset, bytes);
        return true;
    }

    std::vector<uint8_t> bytes_;
    uint32_t failing_offset{0xFFFFFFFFu};
};

}  // namespace

TEST_CASE("dfu check: an image imgtool signed matches its digest, protected TLVs included") {
    SlotImage slot(signed_image());
    CHECK(dfu::check_image(slot) == ports::StagedCheck::Matched);
}

TEST_CASE("dfu check: one byte flipped in the body or in the image class reads as damaged") {
    SlotImage body(signed_image());
    body.bytes_[0x20 + 150] ^= 0x01;
    CHECK(dfu::check_image(body) == ports::StagedCheck::Damaged);

    // The class is a protected TLV, which the digest covers and the signature checks.
    SlotImage protected_area(signed_image());
    protected_area.bytes_[0x14c + 30] ^= 0x01;
    CHECK(dfu::check_image(protected_area) == ports::StagedCheck::Damaged);
}

TEST_CASE("dfu check: an erased slot or a header past the slot is damaged, a failed read not") {
    SlotImage erased(std::vector<uint8_t>{});
    CHECK(dfu::check_image(erased) == ports::StagedCheck::Damaged);

    SlotImage cramped(signed_image(), 0x150);
    CHECK(dfu::check_image(cramped) == ports::StagedCheck::Damaged);

    SlotImage unreadable(signed_image());
    unreadable.failing_offset = 0;
    CHECK(dfu::check_image(unreadable) == ports::StagedCheck::Unreadable);

    SlotImage body_unreadable(signed_image());
    body_unreadable.failing_offset = 256;
    CHECK(dfu::check_image(body_unreadable) == ports::StagedCheck::Unreadable);
}

TEST_CASE("dfu check: an image with no SHA-256 TLV is undigested") {
    std::vector<uint8_t> image = signed_image();
    image[0x178 + 4] = 0x11;
    SlotImage slot(image);
    CHECK(dfu::check_image(slot) == ports::StagedCheck::Undigested);
}
