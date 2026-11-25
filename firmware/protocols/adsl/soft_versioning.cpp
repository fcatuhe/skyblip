#include "protocols/adsl/soft_versioning.h"

#include <initializer_list>

#include "protocols/adsl/packet.h"

namespace skyblip::adsl {

namespace {

// INFO: fc 13nov26 Issue 2 split broader Issue 1 classes, and a model plane has no Issue 1 code
uint8_t issue1_category(uint8_t category) {
    switch (category) {
        case 14: return 0;
        case 15: return 3;
        case 16:
        case 17: return 7;
        default: return category;
    }
}

}  // namespace

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

// INFO: fc 13nov26 0xFFFFFE is one step from a real 0xFFFFFF, well inside any accuracy claimed
bool issue1_form(Packet& packet, std::optional<uint32_t> vfom_cm) {
    if (!packet.is_position()) return false;
    if (packet.has_issue2_motion_marker()) return false;
    packet.aircraft_category = issue1_category(packet.aircraft_category);
    for (const wire::BitField field : {Packet::kLatitudeField, Packet::kLongitudeField})
        if (packet.field(field) == Packet::kIssue1PositionInvalidCode)
            packet.set_field(field, Packet::kIssue1PositionInvalidCode - 1);
    if (!packet.has_position()) {
        packet.set_field(Packet::kLatitudeField, Packet::kIssue1PositionInvalidCode);
        packet.set_field(Packet::kLongitudeField, Packet::kIssue1PositionInvalidCode);
    }
    if (vfom_cm && packet.vertical_accuracy != 0)
        packet.vertical_accuracy = Packet::issue1_vertical_accuracy_code(*vfom_cm);
    packet.set_protocol_version(Packet::kProtocolVersionIssue1);
    return true;
}

}
