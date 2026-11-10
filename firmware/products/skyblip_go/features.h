#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_FEATURES_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_FEATURES_H

#include <cstdint>

#include "products/common/features.h"

namespace skyblip::go {

constexpr common::Feature kFeatures =
    static_cast<common::Feature>(static_cast<uint32_t>(common::Feature::UplinkRx) |
                                 static_cast<uint32_t>(common::Feature::CompanionLink));

}  // namespace skyblip::go

#endif
