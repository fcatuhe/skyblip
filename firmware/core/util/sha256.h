#ifndef SKYBLIP_CORE_UTIL_SHA256_H
#define SKYBLIP_CORE_UTIL_SHA256_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace skyblip {

class Sha256 {
   public:
    static constexpr size_t kDigestBytes = 32;
    using Digest = std::array<uint8_t, kDigestBytes>;

    Sha256() { reset(); }

    void reset();
    void update(const uint8_t* data, size_t len);
    Digest finish();

    static Digest of(const uint8_t* data, size_t len);

   private:
    static constexpr size_t kBlockBytes = 64;

    void compress(const uint8_t* block);

    uint32_t state_[8]{};
    uint8_t block_[kBlockBytes]{};
    size_t filled_{0};
    uint64_t total_bytes_{0};
};

}  // namespace skyblip

#endif
