// Harness, not a test: a JSON request as it arrives on the Config endpoint, from a known device.
#ifndef SKYBLIP_TEST_SUPPORT_CONFIG_FRAME_H
#define SKYBLIP_TEST_SUPPORT_CONFIG_FRAME_H

#include <cstdint>
#include <cstring>

#include "core/events/link.h"

namespace skyblip {

inline constexpr uint32_t kTestAddr = 0x123456;
inline events::RxFrame frame(const char* json) {
    events::RxFrame f{};
    f.session_id = 1;
    f.endpoint = events::Endpoint::Config;
    f.len = static_cast<uint16_t>(std::strlen(json));
    std::memcpy(f.data.data(), json, f.len);
    return f;
}

}  // namespace skyblip

#endif
