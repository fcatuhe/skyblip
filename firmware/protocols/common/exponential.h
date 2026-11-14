#ifndef SKYBLIP_PROTOCOLS_COMMON_EXPONENTIAL_H
#define SKYBLIP_PROTOCOLS_COMMON_EXPONENTIAL_H

#include <cstdint>

namespace skyblip::wire {

// INFO: fc 09nov26 ADS-L G.1.6: [sign] exponent base names 2^exponent * (2^N + base) - 2^N
struct Exponential {
    uint8_t base_bits;
    uint8_t exponent_bits;
    bool is_signed;

    constexpr uint32_t sign_bit() const { return 1u << (base_bits + exponent_bits); }
    constexpr uint32_t largest_magnitude_code() const { return sign_bit() - 1; }

    constexpr uint32_t encode(int32_t value) const {
        if (value < 0 && !is_signed) return 0;
        const uint32_t magnitude =
            value < 0 ? 0u - static_cast<uint32_t>(value) : static_cast<uint32_t>(value);
        const uint32_t one = 1u << base_bits;
        uint32_t exponent = (1u << exponent_bits) - 1;
        while (exponent > 0 && magnitude < floor_of(exponent)) exponent--;
        uint32_t base = (magnitude - floor_of(exponent)) >> exponent;
        if (base >= one) base = one - 1;
        return (value < 0 ? sign_bit() : 0u) | (exponent << base_bits) | base;
    }

    constexpr int32_t decode(uint32_t code) const { return signed_by(code, lowest_of(code)); }

    constexpr int32_t decode_middle(uint32_t code) const {
        return signed_by(code, lowest_of(code) + (step_of(code) >> 1));
    }

   private:
    constexpr uint32_t floor_of(uint32_t exponent) const {
        return ((1u << base_bits) << exponent) - (1u << base_bits);
    }
    constexpr uint32_t exponent_of(uint32_t code) const {
        return (code >> base_bits) & ((1u << exponent_bits) - 1);
    }
    constexpr uint32_t step_of(uint32_t code) const { return 1u << exponent_of(code); }
    constexpr uint32_t lowest_of(uint32_t code) const {
        return floor_of(exponent_of(code)) + (code & ((1u << base_bits) - 1)) * step_of(code);
    }
    constexpr int32_t signed_by(uint32_t code, uint32_t magnitude) const {
        const int32_t value = static_cast<int32_t>(magnitude);
        return is_signed && (code & sign_bit()) ? -value : value;
    }
};

}

#endif
