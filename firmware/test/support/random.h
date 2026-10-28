// Harness, not a test: a deterministic xorshift, so a Monte-Carlo case fails the same way twice.
#ifndef SKYBLIP_TEST_SUPPORT_RANDOM_H
#define SKYBLIP_TEST_SUPPORT_RANDOM_H

#include <cstdint>

namespace skyblip {

class Random {
   public:
    explicit Random(uint32_t seed = 1) : state_(seed) {}

    uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    double uniform() { return (next() >> 8) * (1.0 / 16777216.0); }

   private:
    uint32_t state_;
};

}  // namespace skyblip

#endif
