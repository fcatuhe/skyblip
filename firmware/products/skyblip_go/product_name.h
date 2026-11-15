#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_NAME_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_NAME_H

#include "core/identity/activation.h"
#include "core/identity/variant.h"
#include "ports/capabilities.h"

namespace skyblip::go {

// INFO: fc 18oct26 fitted, not answering: a Plus whose hub waits for its image is still a Go+
constexpr ports::Capabilities kPlusParts =
    ports::Capability::Inclinometer | ports::Capability::Buzzer | ports::Capability::Haptic;

static_assert(sizeof("skyBlip Go+") - 1 <= identity::kProductWordBytes,
              "the name keeps its address");

constexpr identity::Variant variant_of(ports::Capabilities fitted) {
    return ports::has(fitted, kPlusParts) ? identity::Variant::Plus : identity::Variant::Standard;
}

constexpr const char* product_name(ports::Capabilities fitted) {
    return variant_of(fitted) == identity::Variant::Plus ? "skyBlip Go+" : "skyBlip Go";
}

}  // namespace skyblip::go

#endif
