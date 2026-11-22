// Harness, not a test: the action a confirmed prompt or a landed upload left for the product.
#ifndef SKYBLIP_TEST_SUPPORT_CONFIRMED_H
#define SKYBLIP_TEST_SUPPORT_CONFIRMED_H

#include <optional>

#include "products/common/services/config_service.h"

namespace skyblip {

inline std::optional<common::Action> taken(common::ConfigService& config) {
    const std::optional<common::Confirmed> confirmed = config.take_confirmed();
    if (!confirmed) return std::nullopt;
    return confirmed->action;
}

}  // namespace skyblip

#endif
