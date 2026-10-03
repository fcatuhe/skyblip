#ifndef SKYBLIP_HARDWARE_PARTS_BHI260_IMAGE_STORE_H
#define SKYBLIP_HARDWARE_PARTS_BHI260_IMAGE_STORE_H

#include <cstdint>

#include "core/util/sha256.h"
#include "core/util/span.h"
#include "hardware/parts/bhi260/bhi260.h"
#include "ports/flash_region.h"

namespace skyblip::parts {

class Bhi260ImageStore : public Bhi260::Image {
   public:
    enum class Holding : uint8_t { Unread, Blank, Corrupt, Unreadable, Writing, Held };

    // INFO: fc 03oct26 "SKBH" in a hex dump of the partition
    static constexpr uint32_t kMagic = 0x48424B53;
    static constexpr uint16_t kLayout = 1;
    static constexpr uint32_t kHeaderBytes = 256;
    static constexpr uint32_t kVerifyChunkBytes = 1024;

    explicit Bhi260ImageStore(ports::FlashRegion& flash) : flash_(flash) {}

    void check();

    bool begin_write(ConstByteSpan image, const Sha256::Digest& digest);
    bool writing() const { return holding_ == Holding::Writing; }
    uint32_t next_cost_ms() const;
    void step();

    Holding holding() const { return holding_; }
    bool holds(const Sha256::Digest& digest) const {
        return holding_ == Holding::Held && digest_ == digest;
    }
    const Sha256::Digest& digest() const { return digest_; }
    uint32_t capacity() const;

    uint32_t size() const override { return holding_ == Holding::Held ? length_ : 0; }
    bool read(uint32_t offset, uint8_t* out, uint16_t len) override;

   private:
    enum class Phase : uint8_t { Erase, Program, Commit, Verify };

    struct Header {
        uint32_t magic{0};
        uint16_t layout{0};
        uint32_t length{0};
        Sha256::Digest digest{};
    };
    static constexpr uint32_t kHeaderUsedBytes = 12 + Sha256::kDigestBytes;

    static void encode(const Header& header, uint8_t* out);
    static Header decode(const uint8_t* raw);

    Holding verify_payload(uint32_t length, const Sha256::Digest& expected);
    void step_erase();
    void step_program();
    void step_commit();
    void step_verify();
    void stop(Holding why);

    ports::FlashRegion& flash_;
    Holding holding_{Holding::Unread};
    Sha256::Digest digest_{};
    uint32_t length_{0};

    ConstByteSpan source_{};
    Phase phase_{Phase::Erase};
    uint32_t next_sector_{0};
    uint32_t sectors_{0};
    uint32_t done_{0};
    Sha256 verifying_{};
    uint8_t chunk_[kVerifyChunkBytes]{};
};

}  // namespace skyblip::parts

#endif
