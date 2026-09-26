// The receiver's UART, byte by byte, into the parser the L76K driver owns.
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/gnss/nmea.h"

using namespace skyblip;

namespace {

constexpr size_t kChecksumDigits = 2;

bool ends_sentence(char c) { return c == '\r' || c == '\n'; }

// A mutation that breaks the checksum stops at nmea_checksum_ok, so every sentence is re-summed.
void seal(std::string& stream) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    size_t start = std::string::npos;
    for (size_t i = 0; i < stream.size(); i++) {
        if (stream[i] == '$') {
            start = i;
        } else if (ends_sentence(stream[i])) {
            start = std::string::npos;
        } else if (stream[i] == '*' && start != std::string::npos &&
                   i + kChecksumDigits < stream.size()) {
            uint8_t sum = 0;
            for (size_t j = start + 1; j < i; j++) sum ^= static_cast<uint8_t>(stream[j]);
            stream[i + 1] = kHex[sum >> 4];
            stream[i + 2] = kHex[sum & 0x0F];
            start = std::string::npos;
        }
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string stream(reinterpret_cast<const char*>(data), size);
    seal(stream);
    gnss::NmeaParser parser;
    for (char c : stream) parser.feed(c);
    return 0;
}
