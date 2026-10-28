// Harness, not a test: a JSON request as it arrives on the Config endpoint, from a known device.
#ifndef SKYBLIP_TEST_SUPPORT_CONFIG_FRAME_H
#define SKYBLIP_TEST_SUPPORT_CONFIG_FRAME_H

#include <cstdint>
#include <cstring>

#include "core/events/link.h"
#include "core/identity/activation.h"

namespace skyblip {

inline const identity::Activation kTestActivation{0x1234, 0x3F2A};
inline events::RxFrame config_frame(const char* json) {
    events::RxFrame frame{};
    frame.session_id = 1;
    frame.endpoint = events::Endpoint::Config;
    frame.bytes = static_cast<uint16_t>(std::strlen(json));
    std::memcpy(frame.data.data(), json, frame.bytes);
    return frame;
}

}  // namespace skyblip

#endif
