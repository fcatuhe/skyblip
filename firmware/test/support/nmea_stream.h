// Harness, not a test: the NMEA stream a paired tablet heard from a device flying on the rig.
#ifndef SKYBLIP_TEST_SUPPORT_NMEA_STREAM_H
#define SKYBLIP_TEST_SUPPORT_NMEA_STREAM_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/events/link.h"
#include "test/support/product_rig.h"

namespace {

using skyblip::Rig;

// Every byte the device put on the NMEA endpoint, in order: an EFB sees one
// stream, not a sequence of notifications.
inline std::string stream(Rig& rig) {
    std::string all;
    for (const auto& frame : rig.platform.link().sent)
        if (frame.endpoint == skyblip::events::Endpoint::Nmea) all += frame.bytes;
    return all;
}

inline std::vector<std::string> sentences(Rig& rig) {
    std::vector<std::string> out;
    const std::string all = stream(rig);
    size_t at = 0;
    while (true) {
        const size_t end = all.find("\r\n", at);
        if (end == std::string::npos) break;
        out.push_back(all.substr(at, end - at));
        at = end + 2;
    }
    return out;
}

inline std::vector<std::string> fields(const std::string& sentence) {
    std::vector<std::string> out;
    const std::string body = sentence.substr(0, sentence.find('*'));
    size_t at = 0;
    while (true) {
        const size_t comma = body.find(',', at);
        if (comma == std::string::npos) {
            out.push_back(body.substr(at));
            return out;
        }
        out.push_back(body.substr(at, comma - at));
        at = comma + 1;
    }
}

inline int count_of(Rig& rig, const char* kind) {
    int n = 0;
    for (const std::string& s : sentences(rig))
        if (s.rfind(kind, 0) == 0) n++;
    return n;
}

// Airborne, timed and moving: everything below needs a fix, because a relative
// position has no meaning without one.
inline void fly(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 25000, 900); }

}  // namespace

#endif
