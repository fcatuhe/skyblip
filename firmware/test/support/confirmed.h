// Harness, not a test: the action a confirmed prompt or a landed upload left for the product.
#ifndef SKYBLIP_TEST_SUPPORT_CONFIRMED_H
#define SKYBLIP_TEST_SUPPORT_CONFIRMED_H

#include <optional>

#include "core/comms/config.h"

namespace skyblip {

inline std::optional<comms::Action> taken(comms::ConfigService& config) {
    const std::optional<comms::Confirmed> confirmed = config.take_confirmed();
    if (!confirmed) return std::nullopt;
    return confirmed->action;
}

}  // namespace skyblip

#endif
