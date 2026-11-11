#ifndef SKYBLIP_PRODUCTS_COMMON_FEATURES_H
#define SKYBLIP_PRODUCTS_COMMON_FEATURES_H

#include <cstdint>

#include "ports/capabilities.h"

namespace skyblip::common {

enum class Feature : uint32_t {
    None = 0,
    UplinkRx = 1u << 0,
    CompanionLink = 1u << 1,
    LampShowsLink = 1u << 2,
};

// Named apart from ports::has so a service reading both cannot pick the wrong one.
constexpr bool has_feature(Feature declared, Feature one) {
    return (static_cast<uint32_t>(declared) & static_cast<uint32_t>(one)) != 0;
}

struct FeatureSpec {
    Feature feature;
    ports::Capabilities needs;
};

constexpr FeatureSpec kFeatureSpecs[] = {
    {Feature::UplinkRx, ports::Capability::Rf},
    {Feature::CompanionLink, ports::Capability::Link},
    {Feature::LampShowsLink, ports::Capability::Link | ports::Capability::Indicator},
};

constexpr Feature supported(Feature declared, ports::Capabilities capabilities) {
    uint32_t kept = static_cast<uint32_t>(declared);
    for (const FeatureSpec& spec : kFeatureSpecs)
        if (!ports::has(capabilities, spec.needs)) kept &= ~static_cast<uint32_t>(spec.feature);
    return static_cast<Feature>(kept);
}

}  // namespace skyblip::common

#endif
