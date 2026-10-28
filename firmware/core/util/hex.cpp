#include "core/util/hex.h"

namespace skyblip::hex {

namespace {
constexpr char kDigits[] = "0123456789abcdef";

int nibble(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    return -1;
}
}  // namespace

size_t write(ConstByteSpan bytes, char* out) {
    for (size_t i = 0; i < bytes.size(); i++) {
        out[2 * i] = kDigits[bytes[i] >> 4];
        out[2 * i + 1] = kDigits[bytes[i] & 0x0F];
    }
    return 2 * bytes.size();
}

bool read(const char* text, ByteSpan out) {
    for (size_t i = 0; i < out.size(); i++) {
        const int high = nibble(text[2 * i]);
        if (high < 0) return false;
        const int low = nibble(text[2 * i + 1]);
        if (low < 0) return false;
        out[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return text[2 * out.size()] == 0;
}

}  // namespace skyblip::hex
