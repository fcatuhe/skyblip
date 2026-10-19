// core/fec/reed_solomon.h: Reed-Solomon RS(255,223) over GF(2^8).
#ifndef SKYBLIP_CORE_FEC_REED_SOLOMON_H
#define SKYBLIP_CORE_FEC_REED_SOLOMON_H

#include <cstddef>
#include <cstdint>

namespace skyblip::fec {

class ReedSolomon255 {
   public:
    static constexpr int kCodewordBytes = 255;
    static constexpr int kDataBytes = 223;
    static constexpr int kParityBytes = kCodewordBytes - kDataBytes;
    static constexpr int kMaxErrors = kParityBytes / 2;

    ReedSolomon255();

    void encode(const uint8_t data[kDataBytes], uint8_t parity[kParityBytes]) const;

    int decode(uint8_t codeword[kCodewordBytes]) const;

    bool syndromes_zero(const uint8_t codeword[kCodewordBytes]) const;

   private:
    uint8_t antilogarithm_[512];
    uint8_t logarithm_[256];
    uint8_t generator_[kParityBytes + 1];

    uint8_t multiply(uint8_t left, uint8_t right) const {
        if (left == 0 || right == 0) return 0;
        return antilogarithm_[logarithm_[left] + logarithm_[right]];
    }
    uint8_t inverse(uint8_t left) const { return antilogarithm_[255 - logarithm_[left]]; }
    void compute_syndromes(const uint8_t* codeword, uint8_t* syndromes, bool& all_zero) const;
};

}

#endif
