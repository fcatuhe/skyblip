// SHA-256 against the FIPS 180-4 examples: what pins the sensor hub's image to the build.
#include <cstring>
#include <string>

#include "core/util/sha256.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

std::string hex(const Sha256::Digest& digest) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (uint8_t byte : digest) {
        out += kHex[byte >> 4];
        out += kHex[byte & 0x0F];
    }
    return out;
}

std::string hash_of(const char* text) {
    return hex(Sha256::of(reinterpret_cast<const uint8_t*>(text), std::strlen(text)));
}

}  // namespace

TEST_CASE("sha256: the FIPS 180-4 one-block and two-block examples") {
    CHECK(hash_of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hash_of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hash_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

// 55 bytes is the longest message whose length still fits the first block, 56 the shortest that
// does not.
TEST_CASE("sha256: the padding boundary of one block") {
    CHECK(hash_of(std::string(55, 'a').c_str()) ==
          "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(hash_of(std::string(56, 'a').c_str()) ==
          "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
}

TEST_CASE("sha256: a message streamed in uneven chunks hashes as the whole") {
    std::string million(1000000, 'a');
    Sha256 sha;
    size_t at = 0;
    size_t chunk = 1;
    while (at < million.size()) {
        const size_t take = chunk < million.size() - at ? chunk : million.size() - at;
        sha.update(reinterpret_cast<const uint8_t*>(million.data() + at), take);
        at += take;
        chunk = chunk * 3 % 997 + 1;
    }
    CHECK(hex(sha.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256: finishing starts the next message from scratch") {
    Sha256 sha;
    sha.update(reinterpret_cast<const uint8_t*>("abc"), 3);
    (void)sha.finish();
    sha.update(reinterpret_cast<const uint8_t*>("abc"), 3);
    CHECK(hex(sha.finish()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
