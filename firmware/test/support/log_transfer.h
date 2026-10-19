// Harness, not a test: the tablet's side of a log transfer, its requests and what it decodes.
#ifndef SKYBLIP_TEST_SUPPORT_LOG_TRANSFER_H
#define SKYBLIP_TEST_SUPPORT_LOG_TRANSFER_H

#include <cstdint>
#include <cstdio>
#include <string>

#include "core/events/link.h"
#include "hardware/platform/host/link.h"
#include "test/support/product_rig.h"

namespace skyblip {

// The tablet's side of the transfer, and the only decoder in the tree: the
// device never speaks IGC, it hands over the raw records and the CRC that came
// off the flash with them.
inline int base64_decode(const std::string& in, uint8_t* out, int capacity) {
    auto value = [](char character) -> int {
        if (character >= 'A' && character <= 'Z') return character - 'A';
        if (character >= 'a' && character <= 'z') return character - 'a' + 26;
        if (character >= '0' && character <= '9') return character - '0' + 52;
        if (character == '+') return 62;
        if (character == '/') return 63;
        return -1;
    };
    int count = 0;
    uint32_t buffer = 0;
    int bits = 0;
    for (char character : in) {
        const int digit = value(character);
        if (digit < 0) continue;
        buffer = (buffer << 6) | static_cast<uint32_t>(digit);
        bits += 6;
        if (bits < 8) continue;
        bits -= 8;
        if (count < capacity) out[count++] = static_cast<uint8_t>((buffer >> bits) & 0xFF);
    }
    return count;
}

inline std::string field(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    const size_t at = json.find(needle);
    if (at == std::string::npos) return "";
    size_t start = at + needle.size();
    if (json[start] == '"') {
        const size_t end = json.find('"', start + 1);
        return json.substr(start + 1, end - start - 1);
    }
    const size_t end = json.find_first_of(",}", start);
    return json.substr(start, end - start);
}

inline const platform::host::Link::Frame* last_log_frame(Rig& rig) {
    for (size_t i = rig.platform.link().sent.size(); i > 0; i--) {
        const auto& frame = rig.platform.link().sent[i - 1];
        if (frame.endpoint == events::Endpoint::Log) return &frame;
    }
    return nullptr;
}

// The tablet's two-step list: how many flights, then one line each.
inline std::string list_session(Rig& rig, uint32_t& now_ms, uint32_t index) {
    char command[64];
    std::snprintf(command, sizeof(command), "{\"command\":\"list\",\"index\":%u}", index);
    rig.platform.link().clear();
    rig.send_log(command);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    const platform::host::Link::Frame* frame = last_log_frame(rig);
    return frame == nullptr ? "" : frame->bytes;
}

inline std::string list_count(Rig& rig, uint32_t& now_ms) {
    rig.platform.link().clear();
    rig.send_log("{\"command\":\"list\"}");
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    const platform::host::Link::Frame* frame = last_log_frame(rig);
    return frame == nullptr ? "" : frame->bytes;
}

}  // namespace skyblip

#endif
