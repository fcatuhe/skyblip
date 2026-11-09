#include "core/fec/repair.h"

#include <algorithm>

namespace skyblip::fec {

namespace {

// INFO: fc 09nov26 the search may visit every subset of the marked bits, doubling per bit
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

class Search {
   public:
    Search(LinearCheck check, const uint16_t* suspects, int suspect_count,
           bool may_flip_one_unmarked)
        : check_(check),
          suspects_(suspects),
          suspect_count_(suspect_count),
          may_flip_one_unmarked_(may_flip_one_unmarked) {}

    void visit(int index, uint32_t left, uint32_t subset, int flips) {
        if (flips > fewest_flips_ || fewest_flips_ == 1) return;
        if (index == suspect_count_) {
            settle(left, subset, flips);
            return;
        }
        visit(index + 1, left, subset, flips);
        visit(index + 1, left ^ check_.syndrome_of_bit[suspects_[index]], subset | (1u << index),
              flips + 1);
    }

    int apply(uint8_t* frame) const {
        if (fewest_flips_ == kNoRepair) return -1;
        for (int i = 0; i < suspect_count_; i++)
            if (chosen_ & (1u << i)) flip(frame, suspects_[i]);
        if (chosen_unmarked_ >= 0) flip(frame, static_cast<uint16_t>(chosen_unmarked_));
        return fewest_flips_;
    }

   private:
    void settle(uint32_t left, uint32_t subset, int flips) {
        if (left == 0) {
            if (flips < fewest_flips_ || (flips == fewest_flips_ && chosen_unmarked_ >= 0))
                choose(subset, -1, flips);
            return;
        }
        if (!may_flip_one_unmarked_ || flips + 1 >= fewest_flips_) return;
        const int unmarked = bit_with_syndrome(check_, left);
        if (unmarked >= 0) choose(subset, unmarked, flips + 1);
    }

    void choose(uint32_t subset, int unmarked, int flips) {
        fewest_flips_ = flips;
        chosen_ = subset;
        chosen_unmarked_ = unmarked;
    }

    LinearCheck check_;
    const uint16_t* suspects_;
    int suspect_count_;
    bool may_flip_one_unmarked_;
    int fewest_flips_ = kNoRepair;
    uint32_t chosen_ = 0;
    int chosen_unmarked_ = -1;
};

}

int repair(uint8_t* frame, const uint8_t* marked, uint32_t residue, LinearCheck check,
           int max_marked, bool may_flip_one_unmarked) {
    if (residue == 0) return 0;

    uint16_t suspects[kMostMarked];
    int suspect_count = 0;
    bool too_many = false;
    for (uint16_t bit = 0; bit < check.bits && !too_many; bit++) {
        if ((marked[bit >> 3] & mask_of(bit)) == 0) continue;
        too_many = suspect_count == std::min(max_marked, kMostMarked);
        if (!too_many) suspects[suspect_count++] = bit;
    }
    if (too_many) {
        if (!may_flip_one_unmarked) return -1;
        suspect_count = 0;
    }

    Search search(check, suspects, suspect_count, may_flip_one_unmarked);
    search.visit(0, residue, 0, 0);
    return search.apply(frame);
}

}
