#include "core/fec/reed_solomon.h"

namespace skyblip::fec {

ReedSolomon255::ReedSolomon255() {
    uint16_t element = 1;
    for (int i = 0; i < 255; i++) {
        antilogarithm_[i] = static_cast<uint8_t>(element);
        logarithm_[element] = static_cast<uint8_t>(i);
        element <<= 1;
        if (element & 0x100) element ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) antilogarithm_[i] = antilogarithm_[i - 255];
    logarithm_[0] = 0;

    generator_[0] = 1;
    for (int i = 0; i < kParityBytes; i++) {
        generator_[i + 1] = 0;
        uint8_t root = antilogarithm_[i];
        for (int j = i + 1; j > 0; j--) {
            generator_[j] = static_cast<uint8_t>(generator_[j - 1] ^ multiply(generator_[j], root));
        }
        generator_[0] = multiply(generator_[0], root);
    }
}

void ReedSolomon255::encode(const uint8_t data[kDataBytes], uint8_t parity[kParityBytes]) const {
    for (int i = 0; i < kParityBytes; i++) parity[i] = 0;
    for (int i = 0; i < kDataBytes; i++) {
        uint8_t feedback = static_cast<uint8_t>(data[i] ^ parity[0]);
        for (int j = 0; j < kParityBytes - 1; j++) {
            parity[j] = static_cast<uint8_t>(parity[j + 1] ^
                                             multiply(feedback, generator_[kParityBytes - 1 - j]));
        }
        parity[kParityBytes - 1] = multiply(feedback, generator_[0]);
    }
}

void ReedSolomon255::compute_syndromes(const uint8_t* codeword, uint8_t* syndromes,
                                       bool& all_zero) const {
    all_zero = true;
    for (int i = 0; i < kParityBytes; i++) {
        uint8_t syndrome = 0;
        uint8_t root = antilogarithm_[i];
        for (int j = 0; j < kCodewordBytes; j++)
            syndrome = static_cast<uint8_t>(codeword[j] ^ multiply(syndrome, root));
        syndromes[i] = syndrome;
        if (syndrome) all_zero = false;
    }
}

bool ReedSolomon255::syndromes_zero(const uint8_t codeword[kCodewordBytes]) const {
    uint8_t syndromes[kParityBytes];
    bool all_zero;
    compute_syndromes(codeword, syndromes, all_zero);
    return all_zero;
}

int ReedSolomon255::decode(uint8_t codeword[kCodewordBytes]) const {
    uint8_t syndromes[kParityBytes];
    bool all_zero;
    compute_syndromes(codeword, syndromes, all_zero);
    if (all_zero) return 0;

    uint8_t locator[kParityBytes + 1] = {1};
    uint8_t previous_locator[kParityBytes + 1] = {1};
    uint8_t temporary[kParityBytes + 1];
    int register_length = 0;
    int shift = 1;
    uint8_t last_discrepancy = 1;
    for (int step = 0; step < kParityBytes; step++) {
        uint8_t discrepancy = syndromes[step];
        for (int i = 1; i <= register_length; i++)
            discrepancy ^= multiply(locator[i], syndromes[step - i]);
        if (discrepancy == 0) {
            shift++;
        } else if (2 * register_length <= step) {
            for (int i = 0; i <= kParityBytes; i++) temporary[i] = locator[i];
            uint8_t scale = multiply(discrepancy, inverse(last_discrepancy));
            for (int i = 0; i + shift <= kParityBytes; i++)
                locator[i + shift] =
                    static_cast<uint8_t>(locator[i + shift] ^ multiply(scale, previous_locator[i]));
            register_length = step + 1 - register_length;
            for (int i = 0; i <= kParityBytes; i++) previous_locator[i] = temporary[i];
            last_discrepancy = discrepancy;
            shift = 1;
        } else {
            uint8_t scale = multiply(discrepancy, inverse(last_discrepancy));
            for (int i = 0; i + shift <= kParityBytes; i++)
                locator[i + shift] =
                    static_cast<uint8_t>(locator[i + shift] ^ multiply(scale, previous_locator[i]));
            shift++;
        }
    }

    int error_count = 0;
    for (int i = kParityBytes; i >= 0; i--) {
        if (locator[i]) {
            error_count = i;
            break;
        }
    }
    if (error_count == 0 || error_count > kMaxErrors) return -1;

    int error_positions[kMaxErrors];
    int found = 0;
    for (int i = 0; i < kCodewordBytes; i++) {
        uint8_t evaluated = 1;
        uint8_t inverse_power = antilogarithm_[(255 - i) % 255];
        uint8_t power = 1;
        for (int j = 1; j <= error_count; j++) {
            power = multiply(power, inverse_power);
            evaluated ^= multiply(locator[j], power);
        }
        if (evaluated == 0) {
            if (found >= kMaxErrors) return -1;
            error_positions[found++] = i;
        }
    }
    if (found != error_count) return -1;

    uint8_t evaluator[kParityBytes + 1] = {0};
    for (int i = 0; i < kParityBytes; i++) {
        for (int j = 0; j <= i; j++) evaluator[i] ^= multiply(locator[j], syndromes[i - j]);
    }
    for (int error = 0; error < found; error++) {
        int position = error_positions[error];
        uint8_t inverse_power = antilogarithm_[(255 - position) % 255];
        uint8_t evaluator_value = 0;
        uint8_t power = 1;
        for (int j = 0; j < kParityBytes; j++) {
            evaluator_value ^= multiply(evaluator[j], power);
            power = multiply(power, inverse_power);
        }
        uint8_t locator_derivative = 0;
        uint8_t odd_power = 1;
        for (int j = 1; j <= error_count; j += 2) {
            locator_derivative ^= multiply(locator[j], odd_power);
            odd_power = multiply(odd_power, multiply(inverse_power, inverse_power));
        }
        if (locator_derivative == 0) return -1;
        uint8_t position_power = antilogarithm_[position];
        uint8_t magnitude =
            multiply(position_power, multiply(evaluator_value, inverse(locator_derivative)));
        int byte_index = kCodewordBytes - 1 - position;
        if (byte_index < 0 || byte_index >= kCodewordBytes) return -1;
        codeword[byte_index] ^= magnitude;
    }

    if (!syndromes_zero(codeword)) return -1;
    return found;
}

}
