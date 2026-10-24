#include "core/identity/key_slots.h"

namespace skyblip::identity {

namespace {
// INFO: fc 18oct26 a seed with an erased word would read back torn, so it is drawn again
constexpr int kDrawsPerSlot = 4;

size_t at(int slot, int word) {
    return static_cast<size_t>(slot) * KeySlots::kWordsPerSlot + static_cast<size_t>(word);
}

uint32_t word_of(const crypto::Ed25519::Seed& seed, int i) {
    const size_t first = 4 * static_cast<size_t>(i);
    return static_cast<uint32_t>(seed[first]) | (static_cast<uint32_t>(seed[first + 1]) << 8) |
           (static_cast<uint32_t>(seed[first + 2]) << 16) |
           (static_cast<uint32_t>(seed[first + 3]) << 24);
}
}  // namespace

KeySlots::Fill KeySlots::fill(const Words& words, int slot) {
    int erased = 0;
    for (int i = 0; i < kWordsPerSlot; i++)
        if (words[at(slot, i)] == kErasedWord) erased++;
    if (erased == kWordsPerSlot) return Fill::Blank;
    return erased == 0 ? Fill::Complete : Fill::Torn;
}

std::optional<int> KeySlots::current(const Words& words) {
    for (int slot = kSlots - 1; slot >= 0; slot--)
        if (fill(words, slot) == Fill::Complete) return slot;
    return std::nullopt;
}

std::optional<int> KeySlots::next_blank(const Words& words) {
    int slot = kSlots;
    while (slot > 0 && fill(words, slot - 1) == Fill::Blank) slot--;
    if (slot == kSlots) return std::nullopt;
    return slot;
}

crypto::Ed25519::Seed KeySlots::seed_in(const Words& words, int slot) {
    crypto::Ed25519::Seed seed{};
    for (int i = 0; i < kWordsPerSlot; i++) {
        const uint32_t word = words[at(slot, i)];
        for (size_t byte = 0; byte < 4; byte++)
            seed[4 * static_cast<size_t>(i) + byte] = static_cast<uint8_t>(word >> (8 * byte));
    }
    return seed;
}

bool KeySlots::storable(const crypto::Ed25519::Seed& seed) {
    for (int i = 0; i < kWordsPerSlot; i++)
        if (word_of(seed, i) == kErasedWord) return false;
    return true;
}

std::optional<crypto::Ed25519::Seed> KeySlots::seed(const Memory& memory) {
    Words words{};
    if (!memory.read(words)) return std::nullopt;
    const std::optional<int> slot = current(words);
    if (!slot) return std::nullopt;
    return seed_in(words, *slot);
}

std::optional<int> KeySlots::provision(Memory& memory) {
    Words words{};
    if (!memory.read(words)) return std::nullopt;
    if (const std::optional<int> slot = current(words)) return slot;
    int tried = -1;
    for (std::optional<int> slot = next_blank(words); slot && *slot > tried;
         slot = next_blank(words)) {
        tried = *slot;
        crypto::Ed25519::Seed seed{};
        bool drawn = false;
        for (int draw = 0; draw < kDrawsPerSlot && !drawn; draw++)
            drawn = memory.draw(seed) && storable(seed);
        if (!drawn) return std::nullopt;
        for (int i = 0; i < kWordsPerSlot; i++)
            memory.write(*slot * kWordsPerSlot + i, word_of(seed, i));
        if (!memory.read(words)) return std::nullopt;
        if (fill(words, *slot) == Fill::Complete && seed_in(words, *slot) == seed) return slot;
    }
    return std::nullopt;
}

}  // namespace skyblip::identity
