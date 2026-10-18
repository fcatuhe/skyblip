#ifndef SKYBLIP_CORE_IDENTITY_KEY_SLOTS_H
#define SKYBLIP_CORE_IDENTITY_KEY_SLOTS_H

#include <array>
#include <cstdint>
#include <optional>

#include "core/crypto/ed25519.h"

namespace skyblip::identity {

// INFO: fc 18oct26 UICR CUSTOMER: 32 words, four seeds of eight, erased words read 0xFFFFFFFF
class KeySlots {
   public:
    static constexpr int kSlots = 4;
    static constexpr int kWordsPerSlot = 8;
    static constexpr int kWords = kSlots * kWordsPerSlot;
    static constexpr uint32_t kErasedWord = 0xFFFFFFFFu;
    using Words = std::array<uint32_t, kWords>;

    enum class Fill : uint8_t { Blank, Torn, Complete };

    class Memory {
       public:
        virtual ~Memory() = default;
        virtual bool read(Words& out) const = 0;
        virtual void write(int word, uint32_t value) = 0;
        virtual bool draw(crypto::Ed25519::Seed& out) = 0;
    };

    static Fill fill(const Words& words, int slot);
    static std::optional<int> current(const Words& words);
    static std::optional<int> next_blank(const Words& words);
    static crypto::Ed25519::Seed seed_in(const Words& words, int slot);
    static bool storable(const crypto::Ed25519::Seed& seed);

    static std::optional<crypto::Ed25519::Seed> seed(const Memory& memory);
    // INFO: fc 18oct26 the current slot, or a fresh seed drawn into the next blank one, read back
    static std::optional<int> provision(Memory& memory);
};

}  // namespace skyblip::identity

#endif
