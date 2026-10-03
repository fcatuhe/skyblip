// The sensor hub's image on the external flash: written once by a full image, read by a slim one.
#include <vector>

#include "core/store/sector.h"
#include "core/util/sha256.h"
#include "doctest/doctest.h"
#include "hardware/parts/bhi260/image_store.h"
#include "hardware/platform/host/flash_region.h"

using namespace skyblip;

namespace {

using Holding = parts::Bhi260ImageStore::Holding;
using Flash = platform::host::FlashRegion;

constexpr uint32_t kSectors = 32;

std::vector<uint8_t> image_of(size_t bytes) {
    std::vector<uint8_t> image(bytes);
    for (size_t i = 0; i < bytes; i++) image[i] = static_cast<uint8_t>(i * 7 + 3);
    image[0] = 0x2B;
    image[1] = 0x66;
    return image;
}

Sha256::Digest digest_of(const std::vector<uint8_t>& image) {
    return Sha256::of(image.data(), image.size());
}

int write_whole(parts::Bhi260ImageStore& store, const std::vector<uint8_t>& image) {
    REQUIRE(store.begin_write(ConstByteSpan(image.data(), image.size()), digest_of(image)));
    int steps = 0;
    while (store.writing() && steps < 100000) {
        store.step();
        steps++;
    }
    return steps;
}

}  // namespace

TEST_CASE("bhi260 store: an erased partition holds nothing") {
    Flash flash{kSectors};
    parts::Bhi260ImageStore store{flash};
    store.check();
    CHECK(store.holding() == Holding::Blank);
    CHECK(store.size() == 0);
}

TEST_CASE("bhi260 store: an image written is held, read back byte for byte, and survives a boot") {
    Flash flash{kSectors};
    const std::vector<uint8_t> image = image_of(103676);
    parts::Bhi260ImageStore store{flash};
    store.check();
    write_whole(store, image);

    CHECK(store.holding() == Holding::Held);
    CHECK(store.holds(digest_of(image)));
    CHECK(store.size() == image.size());
    std::vector<uint8_t> back(image.size());
    for (uint32_t at = 0; at < image.size(); at += 240) {
        const uint16_t take =
            static_cast<uint16_t>(image.size() - at < 240 ? image.size() - at : 240);
        REQUIRE(store.read(at, back.data() + at, take));
    }
    CHECK(back == image);

    parts::Bhi260ImageStore next_boot{flash};
    next_boot.check();
    CHECK(next_boot.holds(digest_of(image)));
}

// 103,676 bytes behind a 256-byte header is 26 sectors, 405 pages, the header, and 102 verify
// reads.
TEST_CASE("bhi260 store: the write is paced, an erase or a page a step, and priced before each") {
    Flash flash{kSectors};
    const std::vector<uint8_t> image = image_of(103676);
    parts::Bhi260ImageStore store{flash};
    REQUIRE(store.begin_write(ConstByteSpan(image.data(), image.size()), digest_of(image)));

    int erases = 0, programs = 0, reads = 0;
    while (store.writing()) {
        const uint32_t cost = store.next_cost_ms();
        if (cost == skyblip::store::kSectorEraseCostMs)
            erases++;
        else if (cost == skyblip::store::kPageWriteCostMs)
            programs++;
        else
            reads++;
        store.step();
    }
    CHECK(erases == 26);
    CHECK(programs == 405 + 1);
    CHECK(reads == 102);
    CHECK(flash.erases == 26);
}

TEST_CASE("bhi260 store: the header goes last, so a write cut short reads as nothing held") {
    Flash flash{kSectors};
    const std::vector<uint8_t> image = image_of(20000);
    parts::Bhi260ImageStore store{flash};
    REQUIRE(store.begin_write(ConstByteSpan(image.data(), image.size()), digest_of(image)));
    for (int i = 0; i < 20; i++) store.step();
    REQUIRE(store.writing());

    parts::Bhi260ImageStore after_the_cut{flash};
    after_the_cut.check();
    CHECK(after_the_cut.holding() == Holding::Blank);
}

TEST_CASE("bhi260 store: a rewrite erases the old header before anything else") {
    Flash flash{kSectors};
    parts::Bhi260ImageStore store{flash};
    write_whole(store, image_of(5000));
    REQUIRE(store.holding() == Holding::Held);

    const std::vector<uint8_t> newer = image_of(9000);
    REQUIRE(store.begin_write(ConstByteSpan(newer.data(), newer.size()), digest_of(newer)));
    store.step();

    parts::Bhi260ImageStore after_the_cut{flash};
    after_the_cut.check();
    CHECK(after_the_cut.holding() == Holding::Blank);
}

TEST_CASE("bhi260 store: a payload that lost a bit is corrupt, and never read") {
    Flash flash{kSectors};
    const std::vector<uint8_t> image = image_of(5000);
    parts::Bhi260ImageStore store{flash};
    write_whole(store, image);
    REQUIRE(store.holding() == Holding::Held);

    std::vector<uint8_t> bytes = flash.bytes();
    bytes[parts::Bhi260ImageStore::kHeaderBytes + 1234] ^= 0x04;
    flash.restore(bytes);

    parts::Bhi260ImageStore next_boot{flash};
    next_boot.check();
    CHECK(next_boot.holding() == Holding::Corrupt);
    CHECK_FALSE(next_boot.holds(digest_of(image)));
    uint8_t out[4];
    CHECK_FALSE(next_boot.read(0, out, sizeof(out)));
}

TEST_CASE("bhi260 store: a header that is not ours is corrupt, not an image") {
    Flash flash{kSectors};
    const uint8_t rubbish[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    REQUIRE(flash.write(0, rubbish, sizeof(rubbish)) == Status::Ok);
    parts::Bhi260ImageStore store{flash};
    store.check();
    CHECK(store.holding() == Holding::Corrupt);
}

TEST_CASE("bhi260 store: a flash that does not answer is unreadable, for checks and writes") {
    Flash flash{kSectors};
    flash.set_present(false);
    parts::Bhi260ImageStore store{flash};
    store.check();
    CHECK(store.holding() == Holding::Unreadable);

    const std::vector<uint8_t> image = image_of(5000);
    CHECK_FALSE(store.begin_write(ConstByteSpan(image.data(), image.size()), digest_of(image)));
    CHECK(store.holding() == Holding::Unreadable);
}

TEST_CASE("bhi260 store: a power cut in the middle of the write stops it, and says so") {
    Flash flash{kSectors};
    const std::vector<uint8_t> image = image_of(5000);
    parts::Bhi260ImageStore store{flash};
    REQUIRE(store.begin_write(ConstByteSpan(image.data(), image.size()), digest_of(image)));
    flash.cut_power_after(100);
    while (store.writing()) store.step();
    CHECK(store.holding() == Holding::Unreadable);
}

TEST_CASE("bhi260 store: an image larger than the partition is refused before anything is erased") {
    Flash flash{2};
    parts::Bhi260ImageStore store{flash};
    const std::vector<uint8_t> image = image_of(2 * Flash::kSectorBytes);
    CHECK_FALSE(store.begin_write(ConstByteSpan(image.data(), image.size()), digest_of(image)));
    CHECK(flash.erases == 0);
    CHECK(store.capacity() == 2 * Flash::kSectorBytes - parts::Bhi260ImageStore::kHeaderBytes);
}

TEST_CASE("bhi260 store: a digest the payload does not hash to is written, and found corrupt") {
    Flash flash{kSectors};
    const std::vector<uint8_t> image = image_of(5000);
    Sha256::Digest wrong = digest_of(image);
    wrong[0] ^= 0xFF;
    parts::Bhi260ImageStore store{flash};
    REQUIRE(store.begin_write(ConstByteSpan(image.data(), image.size()), wrong));
    while (store.writing()) store.step();
    CHECK(store.holding() == Holding::Corrupt);
}
