#ifndef SKYBLIP_CORE_FEC_REPAIR_H
#define SKYBLIP_CORE_FEC_REPAIR_H

#include <array>
#include <cstdint>

namespace skyblip::fec {

// INFO: fc 09nov26 bit n is byte n / 8 under mask 0x80 >> n % 8, the order the air carries
struct LinearCheck {
    const uint32_t* syndrome_of_bit;
    uint16_t bits;
    const uint32_t* sorted_syndromes;
    const uint16_t* bit_of_sorted;
};

template <uint16_t Bits>
struct Syndromes {
    std::array<uint32_t, Bits> of_bit{};
    std::array<uint32_t, Bits> sorted{};
    std::array<uint16_t, Bits> bit_of_sorted{};

    constexpr LinearCheck check() const {
        return LinearCheck{of_bit.data(), Bits, sorted.data(), bit_of_sorted.data()};
    }
};

template <uint16_t Bits, class SyndromeOf>
constexpr Syndromes<Bits> syndromes(SyndromeOf syndrome_of) {
    Syndromes<Bits> table{};
    for (uint16_t bit = 0; bit < Bits; bit++) {
        table.of_bit[bit] = syndrome_of(bit);
        uint16_t slot = bit;
        while (slot > 0 && table.sorted[slot - 1] > table.of_bit[bit]) {
            table.sorted[slot] = table.sorted[slot - 1];
            table.bit_of_sorted[slot] = table.bit_of_sorted[slot - 1];
            slot--;
        }
        table.sorted[slot] = table.of_bit[bit];
        table.bit_of_sorted[slot] = bit;
    }
    return table;
}

int repair(uint8_t* frame, const uint8_t* marked, uint32_t residue, LinearCheck check,
           int max_marked, bool may_flip_one_unmarked);

}

#endif
