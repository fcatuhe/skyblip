// The framing every product's settings are stored in: a version byte, the payload, and a CRC over
// both.
#include <cstdint>
#include <cstring>

#include "core/settings/blob.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

struct Payload {
    uint32_t addr;
    uint8_t volume;
};

}  // namespace

TEST_CASE("settings blob: a sealed payload opens as itself") {
    const Payload written{0x5BCAFE, 3};
    uint8_t blob[settings::blob_bytes(sizeof(Payload))]{};
    settings::seal(7, &written, sizeof(Payload), blob, sizeof(blob));

    CHECK(settings::blob_version(blob) == 7);

    Payload read{};
    REQUIRE(settings::open(blob, sizeof(blob), sizeof(Payload), &read) == Status::Ok);
    CHECK(read.addr == written.addr);
    CHECK(read.volume == written.volume);
}

// A flash sector that lost a bit reads as a device that configured itself.
TEST_CASE("settings blob: a flipped bit anywhere is refused, payload or version") {
    const Payload written{0x223344, 5};
    uint8_t blob[settings::blob_bytes(sizeof(Payload))]{};
    settings::seal(4, &written, sizeof(Payload), blob, sizeof(blob));

    for (size_t byte = 0; byte < sizeof(blob); byte++) {
        uint8_t corrupted[sizeof(blob)];
        std::memcpy(corrupted, blob, sizeof(blob));
        corrupted[byte] ^= 0x01;
        Payload read{};
        CHECK(settings::open(corrupted, sizeof(corrupted), sizeof(Payload), &read) == Status::Crc);
    }
}

TEST_CASE("settings blob: a blob shorter than its payload is refused, not read past") {
    const Payload written{1, 1};
    uint8_t blob[settings::blob_bytes(sizeof(Payload))]{};
    settings::seal(1, &written, sizeof(Payload), blob, sizeof(blob));

    Payload read{};
    CHECK(settings::open(blob, sizeof(blob) - 1, sizeof(Payload), &read) == Status::Crc);

    uint8_t small[4]{};
    settings::seal(1, &written, sizeof(Payload), small, sizeof(small));
    CHECK(small[0] == 0);
}

// A layout this build cannot read is a later image's, or a sector that lost a bit, and only one is
// kept.
TEST_CASE("settings blob: the framing is checked whole without knowing the payload's layout") {
    const Payload written{0x5BCAFE, 3};
    uint8_t blob[settings::blob_bytes(sizeof(Payload))]{};
    settings::seal(200, &written, sizeof(Payload), blob, sizeof(blob));
    CHECK(settings::sealed(blob, sizeof(blob)));

    blob[2] ^= 0x01;
    CHECK_FALSE(settings::sealed(blob, sizeof(blob)));
    CHECK_FALSE(settings::sealed(blob, settings::kBlobOverhead - 1));
}

TEST_CASE("settings blob: the fallback is named for the link") {
    CHECK(std::strcmp(settings::to_string(settings::Fallback::None), "none") == 0);
    CHECK(std::strcmp(settings::to_string(settings::Fallback::Prior), "prior") == 0);
    CHECK(std::strcmp(settings::to_string(settings::Fallback::Defaults), "defaults") == 0);
}
