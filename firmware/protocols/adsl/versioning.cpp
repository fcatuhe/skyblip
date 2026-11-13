#include "protocols/adsl/versioning.h"

#include "protocols/adsl/packet.h"

namespace skyblip::adsl {

std::optional<uint8_t> status_max_version(const Packet& clear) {
    if (clear.payload_type != Packet::kPayloadTypeStatus) return std::nullopt;
    return static_cast<uint8_t>(clear.field(Packet::kMaxProtocolVersionField));
}

std::optional<uint8_t> traffic_max_version(const Packet& clear) {
    if (!clear.is_position()) return std::nullopt;
    return clear.protocol_version();
}

bool close_client(int32_t horizontal_m, int32_t vertical_m, int32_t closing_mm_s) {
    const int32_t vertical = vertical_m < 0 ? -vertical_m : vertical_m;
    if (vertical >= kCloseVerticalM) return false;
    if (horizontal_m < kCloseHorizontalM) return true;
    return closing_mm_s > 0 && int64_t{horizontal_m} * 1000 < int64_t{kCloseTcpaS} * closing_mm_s;
}

}
