// The upload hook's product check: a Go never takes a Bravo image, nor a Bravo a Go one.
#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

#include "core/dfu/image.h"
#include "core/dfu/product_check.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::dfu;

namespace {

constexpr ImageClass kGo{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                         0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
constexpr ImageClass kBravo{0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                            0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22};
constexpr size_t kBodyBytes = 300;

void put_uint16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void put_tlv(std::vector<uint8_t>& out, uint16_t type, const uint8_t* value, uint16_t bytes) {
    put_uint16(out, type);
    put_uint16(out, bytes);
    out.insert(out.end(), value, value + bytes);
}

void put_tlv(std::vector<uint8_t>& out, uint16_t type, uint8_t fill, uint16_t bytes) {
    const std::vector<uint8_t> value(bytes, fill);
    put_tlv(out, type, value.data(), bytes);
}

std::vector<uint8_t> area(uint16_t magic, const std::vector<uint8_t>& entries) {
    std::vector<uint8_t> out;
    put_uint16(out, magic);
    put_uint16(out, static_cast<uint16_t>(entries.size() + 4));
    out.insert(out.end(), entries.begin(), entries.end());
    return out;
}

// As imgtool lays out a Go image: vendor, class, then the two hub TLVs of sysbuild.cmake.
std::vector<uint8_t> protected_area(const std::optional<ImageClass>& image_class) {
    std::vector<uint8_t> entries;
    put_tlv(entries, 0x74, 0x5a, 16);
    if (image_class) put_tlv(entries, 0x75, image_class->data(), 16);
    put_tlv(entries, 0xa0, 0x9f, 32);
    put_tlv(entries, 0xa1, 0x01, 1);
    return area(0x6908, entries);
}

std::vector<uint8_t> image(const std::optional<ImageClass>& image_class,
                           bool protected_tlvs = true) {
    const std::vector<uint8_t> guarded =
        protected_tlvs ? protected_area(image_class) : std::vector<uint8_t>{};
    std::vector<uint8_t> out(kImageHeaderBytes, 0);
    const uint8_t magic[] = {0x3d, 0xb8, 0xf3, 0x96};
    std::memcpy(out.data(), magic, sizeof(magic));
    out[8] = kImageHeaderBytes;
    out[10] = static_cast<uint8_t>(guarded.size());
    out[11] = static_cast<uint8_t>(guarded.size() >> 8);
    out[12] = static_cast<uint8_t>(kBodyBytes);
    out[13] = static_cast<uint8_t>(kBodyBytes >> 8);
    out.insert(out.end(), kBodyBytes, 0xc3);
    out.insert(out.end(), guarded.begin(), guarded.end());
    std::vector<uint8_t> signature;
    put_tlv(signature, 0x10, 0xaa, 32);
    put_tlv(signature, 0x01, 0xbb, 32);
    put_tlv(signature, 0x22, 0xcc, 72);
    const std::vector<uint8_t> unprotected = area(0x6907, signature);
    out.insert(out.end(), unprotected.begin(), unprotected.end());
    return out;
}

// Each chunk's verdict, in upload order, as the hook sees them before each write.
std::vector<bool> upload(ProductCheck& check, const std::vector<uint8_t>& bytes, size_t chunk_bytes,
                         const std::optional<ImageClass>& running) {
    std::vector<bool> verdicts;
    for (size_t offset = 0; offset < bytes.size(); offset += chunk_bytes) {
        const size_t taken = std::min(chunk_bytes, bytes.size() - offset);
        verdicts.push_back(offset == 0 ? check.begin(bytes.data(), taken, bytes.size(), running)
                                       : check.admits(offset, bytes.data() + offset, taken));
    }
    return verdicts;
}

size_t first_refused(const std::vector<bool>& verdicts) {
    return static_cast<size_t>(std::find(verdicts.begin(), verdicts.end(), false) -
                               verdicts.begin());
}

}  // namespace

TEST_CASE("dfu: the image class is read from the protected TLVs, and only from there") {
    ImageClass found{};
    const std::vector<uint8_t> guarded = protected_area(kBravo);
    REQUIRE(find_image_class(guarded.data(), guarded.size(), found));
    CHECK(found == kBravo);

    const std::vector<uint8_t> classless = protected_area(std::nullopt);
    CHECK_FALSE(find_image_class(classless.data(), classless.size(), found));

    std::vector<uint8_t> entries;
    put_tlv(entries, 0x75, kGo.data(), 16);
    const std::vector<uint8_t> unprotected = area(0x6907, entries);
    CHECK_FALSE(find_image_class(unprotected.data(), unprotected.size(), found));

    std::vector<uint8_t> narrow;
    put_tlv(narrow, 0x75, 0x11, 8);
    const std::vector<uint8_t> short_class = area(0x6908, narrow);
    CHECK_FALSE(find_image_class(short_class.data(), short_class.size(), found));
    CHECK_FALSE(find_image_class(guarded.data(), guarded.size() - 1, found));
}

TEST_CASE("dfu: an image of the running product passes every chunk") {
    ProductCheck check;
    const std::vector<bool> verdicts = upload(check, image(kGo), 64, kGo);
    CHECK(first_refused(verdicts) == verdicts.size());
}

TEST_CASE("dfu: an image of another product is refused on the chunk that completes its class") {
    ProductCheck check;
    const std::vector<uint8_t> bravo = image(kBravo);
    const std::vector<bool> verdicts = upload(check, bravo, 64, kGo);
    // The protected area ends at 32 + 300 + 85 = 417 bytes, inside the seventh 64-byte chunk.
    CHECK(first_refused(verdicts) == 6);
    CHECK(std::count(verdicts.begin(), verdicts.end(), true) == 6);
    CHECK_FALSE(check.admits(448, bravo.data() + 448, 64));
}

TEST_CASE("dfu: a class split across two chunks is read whole") {
    for (const size_t chunk_bytes : {size_t{33}, size_t{350}, size_t{400}, size_t{416}}) {
        ProductCheck go;
        const std::vector<bool> admitted = upload(go, image(kGo), chunk_bytes, kGo);
        CHECK(first_refused(admitted) == admitted.size());
        ProductCheck bravo;
        const std::vector<bool> refused = upload(bravo, image(kBravo), chunk_bytes, kGo);
        CHECK(first_refused(refused) < refused.size());
    }
}

TEST_CASE(
    "dfu: an image that names no product is refused, at once when it signed no protected TLVs") {
    ProductCheck check;
    CHECK(first_refused(upload(check, image(std::nullopt), 64, kGo)) == 6);
    CHECK(first_refused(upload(check, image(kGo, false), 64, kGo)) == 0);
}

TEST_CASE("dfu: a unit that cannot read its own class takes no image over the air") {
    ProductCheck check;
    CHECK(first_refused(upload(check, image(kGo), 64, std::nullopt)) == 0);
}

TEST_CASE("dfu: a protected area wider than the check holds is refused, not read short") {
    std::vector<uint8_t> wide = image(kGo);
    wide[10] = static_cast<uint8_t>(ProductCheck::kProtectedAreaBytes + 1);
    wide[11] = 0;
    ProductCheck check;
    CHECK_FALSE(check.begin(wide.data(), 64, wide.size(), kGo));
}

// img_mgmt never holds the declared length to the header, and leaves the slot past it as it was.
TEST_CASE("dfu: an upload declared to end before the image's class is refused at its first chunk") {
    const std::vector<uint8_t> bravo = image(kBravo);
    const size_t area_end = kImageHeaderBytes + kBodyBytes + protected_area(kBravo).size();
    ProductCheck short_of_it;
    CHECK_FALSE(short_of_it.begin(bravo.data(), 64, area_end - 1, kGo));
    CHECK_FALSE(short_of_it.admits(64, bravo.data() + 64, 64));
    ProductCheck to_its_end;
    CHECK(to_its_end.begin(bravo.data(), 64, area_end, kGo));
}

TEST_CASE("dfu: a chunk with no first chunk before it is refused") {
    ProductCheck check;
    const std::vector<uint8_t> go = image(kGo);
    CHECK_FALSE(check.admits(384, go.data() + 384, 64));
}

TEST_CASE("dfu: a refusal holds until a new upload starts at offset 0") {
    ProductCheck check;
    const std::vector<uint8_t> bravo = image(kBravo);
    REQUIRE(first_refused(upload(check, bravo, 64, kGo)) == 6);
    CHECK_FALSE(check.admits(384, bravo.data() + 384, 64));
    const std::vector<bool> verdicts = upload(check, image(kGo), 64, kGo);
    CHECK(first_refused(verdicts) == verdicts.size());
}
