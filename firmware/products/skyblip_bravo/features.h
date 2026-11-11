#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_FEATURES_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_FEATURES_H

#include <cstdint>

#include "products/common/features.h"

namespace skyblip::bravo {

constexpr common::Feature kFeatures =
    static_cast<common::Feature>(static_cast<uint32_t>(common::Feature::CompanionLink) |
                                 static_cast<uint32_t>(common::Feature::LampShowsLink));

}  // namespace skyblip::bravo

#endif
