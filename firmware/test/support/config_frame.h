// Harness, not a test: a request as a companion app writes it, named in JSON and sent as CBOR.
#ifndef SKYBLIP_TEST_SUPPORT_CONFIG_FRAME_H
#define SKYBLIP_TEST_SUPPORT_CONFIG_FRAME_H

#include <cstdint>
#include <string>

#include "core/events/link.h"
#include "core/identity/activation.h"
#include "simulator/world/link_text.h"

namespace skyblip {

inline const identity::Activation kTestActivation{0x1234, 0x3F2A};

inline constexpr const char* kSmallMtuRefusal =
    "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"small_mtu\"}";

inline events::RxFrame link_frame(events::Endpoint endpoint, uint16_t session_id,
                                  const char* json) {
    events::RxFrame frame{};
    frame.session_id = session_id;
    frame.endpoint = endpoint;
    const int bytes = simulator::link_text::frame_from_text(json, frame.data.data(),
                                                            static_cast<int>(frame.data.size()));
    if (bytes == 0) __builtin_trap();
    frame.bytes = static_cast<uint16_t>(bytes);
    return frame;
}

inline events::RxFrame config_frame(const char* json) {
    return link_frame(events::Endpoint::Config, 1, json);
}

inline std::string link_text(const std::string& frame) {
    return simulator::link_text::text_from_frame(frame);
}

}  // namespace skyblip

#endif
