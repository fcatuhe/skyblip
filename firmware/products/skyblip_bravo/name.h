#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_NAME_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_NAME_H

#include "core/identity/activation.h"

namespace skyblip::bravo {

constexpr const char* kProductName = "skyBlip Bravo";

// INFO: fc 12nov26 Bravo is never activated, so the FICR tag names it whatever activation says
int bluetooth_name(char* out, int capacity, const char* product, const identity::Activation&);

}  // namespace skyblip::bravo

#endif
