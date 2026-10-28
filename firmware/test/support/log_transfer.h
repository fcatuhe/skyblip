// Harness, not a test: the tablet's side of a log transfer, its requests and what it decodes.
#ifndef SKYBLIP_TEST_SUPPORT_LOG_TRANSFER_H
#define SKYBLIP_TEST_SUPPORT_LOG_TRANSFER_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/comms/link_registry.h"
#include "core/events/link.h"
#include "core/util/cbor.h"
#include "hardware/platform/host/link.h"
#include "test/support/product_rig.h"

namespace skyblip {

// The tablet's side: the device never speaks IGC, it hands over the raw records, CRC and all.
inline int chunk_records(const std::string& frame, uint8_t* out, int capacity) {
    const cbor::Reader reader(
        ConstByteSpan(reinterpret_cast<const uint8_t*>(frame.data()), frame.size()));
    for (int i = 0; i < reader.count(); i++) {
        cbor::Entry entry;
        reader.entry(i, entry);
        if (entry.key != link::chunk_key::kRecords || entry.type != cbor::Type::Bytes) continue;
        const int bytes = static_cast<int>(entry.content.size());
        if (bytes > capacity) return -1;
        std::memcpy(out, entry.content.data(), entry.content.size());
        return bytes;
    }
    return -1;
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
    return frame == nullptr ? "" : link_text(frame->bytes);
}

inline std::string list_count(Rig& rig, uint32_t& now_ms) {
    rig.platform.link().clear();
    rig.send_log("{\"command\":\"list\"}");
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    const platform::host::Link::Frame* frame = last_log_frame(rig);
    return frame == nullptr ? "" : link_text(frame->bytes);
}

}  // namespace skyblip

#endif
