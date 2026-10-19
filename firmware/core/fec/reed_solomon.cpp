#include "core/fec/reed_solomon.h"

namespace skyblip::fec {

ReedSolomon255::ReedSolomon255() {
    uint16_t x = 1;
    for (int i = 0; i < 255; i++) {
        exp_[i] = static_cast<uint8_t>(x);
        log_[x] = static_cast<uint8_t>(i);
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) exp_[i] = exp_[i - 255];
    log_[0] = 0;

    gen_[0] = 1;
    for (int i = 0; i < kParity; i++) {
        gen_[i + 1] = 0;
        uint8_t root = exp_[i];
        for (int j = i + 1; j > 0; j--) {
            gen_[j] = static_cast<uint8_t>(gen_[j - 1] ^ mul(gen_[j], root));
        }
        gen_[0] = mul(gen_[0], root);
    }
}

void ReedSolomon255::encode(const uint8_t data[kK], uint8_t parity[kParity]) const {
    for (int i = 0; i < kParity; i++) parity[i] = 0;
    for (int i = 0; i < kK; i++) {
        uint8_t feedback = static_cast<uint8_t>(data[i] ^ parity[0]);
        for (int j = 0; j < kParity - 1; j++) {
            parity[j] = static_cast<uint8_t>(parity[j + 1] ^ mul(feedback, gen_[kParity - 1 - j]));
        }
        parity[kParity - 1] = mul(feedback, gen_[0]);
    }
}

void ReedSolomon255::compute_syndromes(const uint8_t* codeword, uint8_t* syndromes,
                                       bool& all_zero) const {
    all_zero = true;
    for (int i = 0; i < kParity; i++) {
        uint8_t s = 0;
        uint8_t root = exp_[i];
        for (int j = 0; j < kN; j++) s = static_cast<uint8_t>(codeword[j] ^ mul(s, root));
        syndromes[i] = s;
        if (s) all_zero = false;
    }
}

bool ReedSolomon255::syndromes_zero(const uint8_t codeword[kN]) const {
    uint8_t syndromes[kParity];
    bool z;
    compute_syndromes(codeword, syndromes, z);
    return z;
}

int ReedSolomon255::decode(uint8_t codeword[kN]) const {
    uint8_t syndromes[kParity];
    bool all_zero;
    compute_syndromes(codeword, syndromes, all_zero);
    if (all_zero) return 0;

    uint8_t lambda[kParity + 1] = {1};
    uint8_t previous[kParity + 1] = {1};
    uint8_t temporary[kParity + 1];
    int l = 0;
    int m = 1;
    uint8_t b = 1;
    for (int n = 0; n < kParity; n++) {
        uint8_t delta = syndromes[n];
        for (int i = 1; i <= l; i++) delta ^= mul(lambda[i], syndromes[n - i]);
        if (delta == 0) {
            m++;
        } else if (2 * l <= n) {
            for (int i = 0; i <= kParity; i++) temporary[i] = lambda[i];
            uint8_t coef = mul(delta, inv(b));
            for (int i = 0; i + m <= kParity; i++)
                lambda[i + m] = static_cast<uint8_t>(lambda[i + m] ^ mul(coef, previous[i]));
            l = n + 1 - l;
            for (int i = 0; i <= kParity; i++) previous[i] = temporary[i];
            b = delta;
            m = 1;
        } else {
            uint8_t coef = mul(delta, inv(b));
            for (int i = 0; i + m <= kParity; i++)
                lambda[i + m] = static_cast<uint8_t>(lambda[i + m] ^ mul(coef, previous[i]));
            m++;
        }
    }

    int error_count = 0;
    for (int i = kParity; i >= 0; i--) {
        if (lambda[i]) {
            error_count = i;
            break;
        }
    }
    if (error_count == 0 || error_count > kMaxErrors) return -1;

    int error_positions[kMaxErrors];
    int found = 0;
    for (int i = 0; i < kN; i++) {
        uint8_t sum = 1;
        uint8_t xinv = exp_[(255 - i) % 255];
        uint8_t xp = 1;
        for (int j = 1; j <= error_count; j++) {
            xp = mul(xp, xinv);
            sum ^= mul(lambda[j], xp);
        }
        if (sum == 0) {
            if (found >= kMaxErrors) return -1;
            error_positions[found++] = i;
        }
    }
    if (found != error_count) return -1;

    uint8_t omega[kParity + 1] = {0};
    for (int i = 0; i < kParity; i++) {
        for (int j = 0; j <= i; j++) omega[i] ^= mul(lambda[j], syndromes[i - j]);
    }
    for (int k = 0; k < found; k++) {
        int position = error_positions[k];
        uint8_t xinv = exp_[(255 - position) % 255];
        uint8_t omega_v = 0;
        uint8_t xp = 1;
        for (int j = 0; j < kParity; j++) {
            omega_v ^= mul(omega[j], xp);
            xp = mul(xp, xinv);
        }
        uint8_t lambda_der = 0;
        uint8_t xq = 1;
        for (int j = 1; j <= error_count; j += 2) {
            lambda_der ^= mul(lambda[j], xq);
            xq = mul(xq, mul(xinv, xinv));
        }
        if (lambda_der == 0) return -1;
        uint8_t xk = exp_[position];
        uint8_t magnitude = mul(xk, mul(omega_v, inv(lambda_der)));
        int byte_index = kN - 1 - position;
        if (byte_index < 0 || byte_index >= kN) return -1;
        codeword[byte_index] ^= magnitude;
    }

    if (!syndromes_zero(codeword)) return -1;
    return found;
}

}
