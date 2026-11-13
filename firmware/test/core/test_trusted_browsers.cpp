// The list of browsers a unit trusts, and the blob it survives a power cycle in.
#include <cstring>
#include <string>

#include "core/comms/trusted_browsers.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {
crypto::Ed25519::PublicKey key_numbered(uint8_t number) {
    crypto::Ed25519::PublicKey key{};
    key.fill(number);
    return key;
}

BrowserId id_numbered(uint8_t number) {
    BrowserId id{};
    id.fill(number);
    return id;
}
}  // namespace

TEST_CASE("trusted browsers: a browser is stored once, under the id its key starts with") {
    TrustedBrowsers browsers;
    browsers.store(key_numbered(1), "Chrome on Android");
    browsers.store(key_numbered(1), "Chrome again");

    REQUIRE(browsers.count() == 1);
    CHECK(browsers.holds(key_numbered(1)));
    CHECK(browsers.at(0).id() == id_numbered(1));
    CHECK(std::string(browsers.at(0).label) == "Chrome on Android");
}

TEST_CASE("trusted browsers: a ninth browser replaces the one stored first") {
    TrustedBrowsers browsers;
    for (uint8_t number = 1; number <= TrustedBrowsers::kCapacity + 1; number++)
        browsers.store(key_numbered(number), "");

    CHECK(browsers.count() == TrustedBrowsers::kCapacity);
    CHECK_FALSE(browsers.holds(key_numbered(1)));
    CHECK(browsers.at(0).id() == id_numbered(2));
    CHECK(browsers.at(TrustedBrowsers::kCapacity - 1).id() == id_numbered(9));
}

TEST_CASE("trusted browsers: forgetting one keeps the others in their order") {
    TrustedBrowsers browsers;
    for (uint8_t number = 1; number <= 3; number++) browsers.store(key_numbered(number), "");

    CHECK(browsers.forget(id_numbered(2)));
    CHECK_FALSE(browsers.forget(id_numbered(2)));
    REQUIRE(browsers.count() == 2);
    CHECK(browsers.at(0).id() == id_numbered(1));
    CHECK(browsers.at(1).id() == id_numbered(3));
}

TEST_CASE("trusted browsers: the blob gives back the list it was written from") {
    TrustedBrowsers written;
    written.store(key_numbered(4), "Edge on Windows");
    written.store(key_numbered(5), "");
    uint8_t blob[TrustedBrowsers::kBlobBytes];
    REQUIRE(written.to_blob(blob, sizeof(blob)) == sizeof(blob));

    TrustedBrowsers read;
    REQUIRE(read.adopt(blob, sizeof(blob)));
    REQUIRE(read.count() == 2);
    CHECK(read.holds(key_numbered(4)));
    CHECK(std::string(read.at(0).label) == "Edge on Windows");
    CHECK(read.holds(key_numbered(5)));
}

TEST_CASE("trusted browsers: a torn, foreign or overfull blob is refused and leaves the list") {
    TrustedBrowsers written;
    written.store(key_numbered(6), "Chrome on macOS");
    uint8_t blob[TrustedBrowsers::kBlobBytes];
    REQUIRE(written.to_blob(blob, sizeof(blob)) == sizeof(blob));

    TrustedBrowsers read;
    read.store(key_numbered(7), "");
    uint8_t torn[sizeof(blob)];
    std::memcpy(torn, blob, sizeof(blob));
    torn[10] ^= 1;
    CHECK_FALSE(read.adopt(torn, sizeof(torn)));
    CHECK_FALSE(read.adopt(blob, sizeof(blob) - 1));

    uint8_t foreign[sizeof(blob)];
    std::memcpy(foreign, blob, sizeof(blob));
    foreign[0] = TrustedBrowsers::kBlobVersion + 1;
    CHECK_FALSE(read.adopt(foreign, sizeof(foreign)));

    uint8_t overfull[sizeof(blob)];
    uint8_t payload[TrustedBrowsers::kPayloadBytes]{};
    payload[0] = TrustedBrowsers::kCapacity + 1;
    settings::seal(TrustedBrowsers::kBlobVersion, payload, sizeof(payload), overfull,
                   sizeof(overfull));
    CHECK_FALSE(read.adopt(overfull, sizeof(overfull)));

    REQUIRE(read.count() == 1);
    CHECK(read.holds(key_numbered(7)));
}
