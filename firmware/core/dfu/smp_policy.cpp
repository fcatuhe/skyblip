#include "core/dfu/smp_policy.h"

namespace skyblip::dfu {

bool smp_permitted(const SmpCommand& command, bool upload_allowed) {
    if (command.operation == static_cast<uint8_t>(SmpOperation::Read)) return true;
    if (command.operation != static_cast<uint8_t>(SmpOperation::Write)) return false;
    if (command.group == static_cast<uint16_t>(SmpGroup::OperatingSystem))
        return command.id == kSmpOperatingSystemEcho;
    if (command.group == static_cast<uint16_t>(SmpGroup::Image))
        return command.id == kSmpImageUpload && upload_allowed;
    return false;
}

}  // namespace skyblip::dfu
