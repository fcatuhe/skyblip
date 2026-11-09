#include "core/identity/key_slots.h"

#include <algorithm>

#include "core/util/sha256.h"

namespace skyblip::identity {

namespace {
// INFO: fc 18oct26 a draw with an erased word would read back torn, so it is drawn again
constexpr int kDrawsPerSlot = 4;
constexpr int kTagWord = KeySlots::kWordsPerSlot - 1;
constexpr const char* kTagLabel = "skyblip:slot:v1";
constexpr const char* kSeedLabel = "skyblip:seed:v1";

size_t at(int slot, int word) {
    return static_cast<size_t>(slot) * KeySlots::kWordsPerSlot + static_cast<size_t>(word);
}

Sha256::Digest labelled_digest(const char* label, const DeviceId& device_id,
                               const KeySlots::Material& material) {
    size_t label_bytes = 0;
    while (label[label_bytes] != 0) label_bytes++;
    Sha256 sha;
    sha.update(reinterpret_cast<const uint8_t*>(label), label_bytes);
    sha.update(device_id.data(), device_id.size());
    sha.update(material.data(), material.size());
    return sha.finish();
}

uint32_t word_at(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

KeySlots::Slot slot_in(const KeySlots::Words& words, int slot) {
    KeySlots::Slot out{};
    for (int i = 0; i < KeySlots::kWordsPerSlot; i++) out[i] = words[at(slot, i)];
    return out;
}

KeySlots::Material material_in(const KeySlots::Slot& slot) {
    KeySlots::Material material{};
    for (int i = 0; i < kTagWord; i++)
        for (size_t byte = 0; byte < 4; byte++)
            material[4 * static_cast<size_t>(i) + byte] =
                static_cast<uint8_t>(slot[i] >> (8 * byte));
    return material;
}

bool blank(const KeySlots::Words& words, int slot) {
    const KeySlots::Slot held = slot_in(words, slot);
    return std::all_of(held.begin(), held.end(),
                       [](uint32_t word) { return word == KeySlots::kErasedWord; });
}

bool all_one_byte(const KeySlots::Material& material) {
    return std::all_of(material.begin(), material.end(),
                       [&material](uint8_t byte) { return byte == material[0]; });
}
}  // namespace

KeySlots::Fill KeySlots::fill(const Words& words, int slot, const DeviceId& device_id) {
    const Slot held = slot_in(words, slot);
    int erased = 0;
    for (const uint32_t word : held)
        if (word == kErasedWord) erased++;
    if (erased == kWordsPerSlot) return Fill::Blank;
    if (erased > 0) return Fill::Torn;
    return held == slot_of(material_in(held), device_id) ? Fill::Complete : Fill::Torn;
}

std::optional<int> KeySlots::current(const Words& words, const DeviceId& device_id) {
    for (int slot = kSlots - 1; slot >= 0; slot--)
        if (fill(words, slot, device_id) == Fill::Complete) return slot;
    return std::nullopt;
}

std::optional<int> KeySlots::next_blank(const Words& words) {
    int slot = kSlots;
    while (slot > 0 && blank(words, slot - 1)) slot--;
    if (slot == kSlots) return std::nullopt;
    return slot;
}

KeySlots::Slot KeySlots::slot_of(const Material& material, const DeviceId& device_id) {
    Slot slot{};
    for (int i = 0; i < kTagWord; i++)
        slot[i] = word_at(material.data() + 4 * static_cast<size_t>(i));
    slot[kTagWord] = word_at(labelled_digest(kTagLabel, device_id, material).data());
    return slot;
}

crypto::Ed25519::Seed KeySlots::seed_of(const Material& material, const DeviceId& device_id) {
    return labelled_digest(kSeedLabel, device_id, material);
}

// INFO: fc 08nov26 a draw of one byte repeated is an RNG that failed or wrote nothing, never a key
bool KeySlots::storable(const Material& material, const DeviceId& device_id) {
    if (all_one_byte(material)) return false;
    const Slot slot = slot_of(material, device_id);
    return std::none_of(slot.begin(), slot.end(),
                        [](uint32_t word) { return word == kErasedWord; });
}

std::optional<crypto::Ed25519::Seed> KeySlots::seed(const Memory& memory,
                                                    const DeviceId& device_id) {
    Words words{};
    if (!memory.read(words)) return std::nullopt;
    const std::optional<int> slot = current(words, device_id);
    if (!slot) return std::nullopt;
    return seed_of(material_in(slot_in(words, *slot)), device_id);
}

std::optional<int> KeySlots::provision(Memory& memory, const DeviceId& device_id) {
    Words words{};
    if (!memory.read(words)) return std::nullopt;
    if (const std::optional<int> slot = current(words, device_id)) return slot;
    int tried = -1;
    for (std::optional<int> slot = next_blank(words); slot && *slot > tried;
         slot = next_blank(words)) {
        tried = *slot;
        Material material{};
        bool drawn = false;
        for (int draw = 0; draw < kDrawsPerSlot && !drawn; draw++) {
            material.fill(0);
            drawn = memory.draw(material) && storable(material, device_id);
        }
        if (!drawn) return std::nullopt;
        const Slot written = slot_of(material, device_id);
        for (int i = 0; i < kWordsPerSlot; i++) memory.write(*slot * kWordsPerSlot + i, written[i]);
        if (!memory.read(words)) return std::nullopt;
        if (slot_in(words, *slot) == written) return slot;
    }
    return std::nullopt;
}

}  // namespace skyblip::identity
