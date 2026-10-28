#include "core/util/sha256.h"

#include <cstring>

namespace skyblip {

namespace {

constexpr uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr uint32_t kInitial[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

constexpr uint32_t rotate_right(uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

uint32_t get_u32_big_endian(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 |
           static_cast<uint32_t>(bytes[2]) << 8 | static_cast<uint32_t>(bytes[3]);
}

}  // namespace

void Sha256::reset() {
    std::memcpy(state_, kInitial, sizeof(state_));
    filled_ = 0;
    total_bytes_ = 0;
}

void Sha256::update(const uint8_t* data, size_t bytes) {
    total_bytes_ += bytes;
    while (bytes > 0) {
        const size_t take = bytes < kBlockBytes - filled_ ? bytes : kBlockBytes - filled_;
        std::memcpy(block_ + filled_, data, take);
        filled_ += take;
        data += take;
        bytes -= take;
        if (filled_ == kBlockBytes) {
            compress(block_);
            filled_ = 0;
        }
    }
}

Sha256::Digest Sha256::finish() {
    const uint64_t bits = total_bytes_ * 8;
    const uint8_t marker = 0x80;
    update(&marker, 1);
    const uint8_t zero = 0;
    while (filled_ != kBlockBytes - 8) update(&zero, 1);
    uint8_t encoded_bits[8];
    for (int i = 0; i < 8; i++) encoded_bits[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    update(encoded_bits, sizeof(encoded_bits));

    Digest digest{};
    for (size_t i = 0; i < 8; i++) {
        digest[4 * i] = static_cast<uint8_t>(state_[i] >> 24);
        digest[4 * i + 1] = static_cast<uint8_t>(state_[i] >> 16);
        digest[4 * i + 2] = static_cast<uint8_t>(state_[i] >> 8);
        digest[4 * i + 3] = static_cast<uint8_t>(state_[i]);
    }
    reset();
    return digest;
}

Sha256::Digest Sha256::of(const uint8_t* data, size_t bytes) {
    Sha256 hasher;
    hasher.update(data, bytes);
    return hasher.finish();
}

void Sha256::compress(const uint8_t* block) {
    uint32_t schedule[64];
    for (size_t i = 0; i < 16; i++) schedule[i] = get_u32_big_endian(block + 4 * i);
    for (size_t i = 16; i < 64; i++) {
        const uint32_t older = schedule[i - 15];
        const uint32_t recent = schedule[i - 2];
        const uint32_t older_mix = rotate_right(older, 7) ^ rotate_right(older, 18) ^ (older >> 3);
        const uint32_t recent_mix =
            rotate_right(recent, 17) ^ rotate_right(recent, 19) ^ (recent >> 10);
        schedule[i] = schedule[i - 16] + older_mix + schedule[i - 7] + recent_mix;
    }

    uint32_t working[8];
    std::memcpy(working, state_, sizeof(working));
    for (int round = 0; round < 64; round++) {
        const uint32_t first = working[0];
        const uint32_t fifth = working[4];
        const uint32_t fifth_mix =
            rotate_right(fifth, 6) ^ rotate_right(fifth, 11) ^ rotate_right(fifth, 25);
        const uint32_t choose = (fifth & working[5]) ^ (~fifth & working[6]);
        const uint32_t carried = working[7] + fifth_mix + choose + kRound[round] + schedule[round];
        const uint32_t first_mix =
            rotate_right(first, 2) ^ rotate_right(first, 13) ^ rotate_right(first, 22);
        const uint32_t majority =
            (first & working[1]) ^ (first & working[2]) ^ (working[1] & working[2]);
        for (int k = 7; k > 0; k--) working[k] = working[k - 1];
        working[4] += carried;
        working[0] = carried + first_mix + majority;
    }
    for (int k = 0; k < 8; k++) state_[k] += working[k];
}

}  // namespace skyblip
