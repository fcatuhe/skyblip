#ifndef SKYBLIP_CORE_IDENTITY_KEY_SLOTS_H
#define SKYBLIP_CORE_IDENTITY_KEY_SLOTS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/crypto/ed25519.h"
#include "core/identity/messages.h"

namespace skyblip::identity {

// INFO: fc 18oct26 UICR CUSTOMER: 32 words, four slots of eight, erased words read 0xFFFFFFFF
class KeySlots {
   public:
    static constexpr int kSlots = 4;
    static constexpr int kWordsPerSlot = 8;
    static constexpr int kWords = kSlots * kWordsPerSlot;
    static constexpr uint32_t kErasedWord = 0xFFFFFFFFu;
    static constexpr size_t kMaterialBytes = 28;
    using Words = std::array<uint32_t, kWords>;
    using Slot = std::array<uint32_t, kWordsPerSlot>;
    using Material = std::array<uint8_t, kMaterialBytes>;

    enum class Fill : uint8_t { Blank, Torn, Complete };

    class Memory {
       public:
        virtual ~Memory() = default;
        virtual bool read(Words& out) const = 0;
        virtual void write(int word, uint32_t value) = 0;
        virtual bool draw(Material& out) = 0;
    };

    // INFO: fc 08nov26 tag and seed hash the DEVICEID, so a slot copied to another chip reads torn
    static Fill fill(const Words& words, int slot, const DeviceId& device_id);
    static std::optional<int> current(const Words& words, const DeviceId& device_id);
    static std::optional<int> next_blank(const Words& words);
    static Slot slot_of(const Material& material, const DeviceId& device_id);
    static crypto::Ed25519::Seed seed_of(const Material& material, const DeviceId& device_id);
    static bool storable(const Material& material, const DeviceId& device_id);

    static std::optional<crypto::Ed25519::Seed> seed(const Memory& memory,
                                                     const DeviceId& device_id);
    // INFO: fc 18oct26 the current slot, or a fresh seed drawn into the next blank one, read back
    static std::optional<int> provision(Memory& memory, const DeviceId& device_id);
};

}  // namespace skyblip::identity

#endif
