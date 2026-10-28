#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_NAME_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_NAME_H

#include "core/identity/activation.h"
#include "ports/capabilities.h"

namespace skyblip::go {

// INFO: fc 18oct26 fitted, not answering: a Plus whose hub waits for its image is still a Go+
constexpr ports::Capabilities kPlusParts =
    ports::Capability::Inclinometer | ports::Capability::Buzzer | ports::Capability::Haptic;

static_assert(sizeof("skyBlip Go+") - 1 <= identity::kProductWordBytes,
              "the name keeps its address");

constexpr const char* product_name(ports::Capabilities fitted) {
    return ports::has(fitted, kPlusParts) ? "skyBlip Go+" : "skyBlip Go";
}

}  // namespace skyblip::go

#endif
