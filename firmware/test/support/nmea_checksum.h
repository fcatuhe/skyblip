// Harness, not a test: the checksum a sentence carries, recomputed from its body.
#ifndef SKYBLIP_TEST_SUPPORT_NMEA_CHECKSUM_H
#define SKYBLIP_TEST_SUPPORT_NMEA_CHECKSUM_H

#include <cstdint>
#include <string>

namespace skyblip {

inline bool checksum_ok(const std::string& s) {
    auto star = s.find('*');
    if (star == std::string::npos) return false;
    uint8_t cs = 0;
    for (size_t i = 1; i < star; i++) cs ^= static_cast<uint8_t>(s[i]);
    char hh[3] = {s[star + 1], s[star + 2], 0};
    return static_cast<uint8_t>(std::stoi(hh, nullptr, 16)) == cs;
}

}  // namespace skyblip

#endif
