#include "core/fec/repair.h"

#include <algorithm>

#include "core/util/bitcount.h"

namespace skyblip::fec {

namespace {

// INFO: fc 09nov26 every subset of the marked bits is tried, so each one more doubles the work
constexpr int kMostMarked = 16;
constexpr int kNoRepair = 0x7FFF;

uint8_t mask_of(uint16_t bit) { return static_cast<uint8_t>(0x80u >> (bit & 7)); }

void flip(uint8_t* frame, uint16_t bit) {
    frame[bit >> 3] = static_cast<uint8_t>(frame[bit >> 3] ^ mask_of(bit));
}

int bit_with_syndrome(LinearCheck check, uint32_t syndrome) {
    const uint32_t* end = check.sorted_syndromes + check.bits;
    const uint32_t* found = std::lower_bound(check.sorted_syndromes, end, syndrome);
    if (found == end || *found != syndrome) return -1;
    return check.bit_of_sorted[found - check.sorted_syndromes];
}

}

int repair(uint8_t* frame, const uint8_t* marked, uint32_t residue, LinearCheck check,
           int max_marked, bool may_flip_one_unmarked) {
    if (residue == 0) return 0;

    uint16_t suspects[kMostMarked];
    int suspect_count = 0;
    for (uint16_t bit = 0; bit < check.bits; bit++) {
        if ((marked[bit >> 3] & mask_of(bit)) == 0) continue;
        if (suspect_count == max_marked || suspect_count == kMostMarked) return -1;
        suspects[suspect_count++] = bit;
    }

    int fewest_flips = kNoRepair;
    uint32_t chosen = 0;
    int chosen_unmarked = -1;
    for (uint32_t subset = 0; subset < (1u << suspect_count); subset++) {
        uint32_t left = residue;
        for (int i = 0; i < suspect_count; i++)
            if (subset & (1u << i)) left ^= check.syndrome_of_bit[suspects[i]];
        int flips = count_ones(subset);
        int unmarked = -1;
        if (left != 0) {
            if (!may_flip_one_unmarked || flips + 1 >= fewest_flips) continue;
            unmarked = bit_with_syndrome(check, left);
            if (unmarked < 0) continue;
            flips++;
        }
        const bool all_marked_tie = flips == fewest_flips && unmarked < 0 && chosen_unmarked >= 0;
        if (flips < fewest_flips || all_marked_tie) {
            fewest_flips = flips;
            chosen = subset;
            chosen_unmarked = unmarked;
        }
    }
    if (fewest_flips == kNoRepair) return -1;

    for (int i = 0; i < suspect_count; i++)
        if (chosen & (1u << i)) flip(frame, suspects[i]);
    if (chosen_unmarked >= 0) flip(frame, static_cast<uint16_t>(chosen_unmarked));
    return fewest_flips;
}

}
