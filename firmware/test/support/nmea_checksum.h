// Harness, not a test: the checksum a sentence carries, recomputed from its body, or sealed on.
#ifndef SKYBLIP_TEST_SUPPORT_NMEA_CHECKSUM_H
#define SKYBLIP_TEST_SUPPORT_NMEA_CHECKSUM_H

#include <cstdint>
#include <cstdio>
#include <string>

namespace skyblip {

inline bool checksum_ok(const std::string& sentence) {
    const size_t star = sentence.find('*');
    if (star == std::string::npos || sentence.size() < star + 3) return false;
    uint8_t sum = 0;
    for (size_t i = 1; i < star; i++) sum ^= static_cast<uint8_t>(sentence[i]);
    const std::string hex = sentence.substr(star + 1, 2);
    return static_cast<uint8_t>(std::stoi(hex, nullptr, 16)) == sum;
}

// A body between '$' and '*', sealed into the sentence a receiver would send.
inline std::string checksummed(const std::string& body) {
    uint8_t sum = 0;
    for (const char character : body) sum ^= static_cast<uint8_t>(character);
    char tail[4];
    std::snprintf(tail, sizeof(tail), "*%02X", sum);
    return "$" + body + tail;
}

}  // namespace skyblip

#endif
