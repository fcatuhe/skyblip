// A shared service holds references to the product's settings, so none may be built from a
// temporary.
#include <cstdint>
#include <type_traits>

#include "products/common/services/alarm.h"
#include "products/common/services/ownship.h"
#include "products/common/services/power.h"

using namespace skyblip;

static_assert(std::is_constructible_v<common::OwnshipSettings, const uint8_t&>);
static_assert(!std::is_constructible_v<common::OwnshipSettings, uint8_t>);
static_assert(!std::is_constructible_v<common::OwnshipSettings, const uint8_t>);

static_assert(std::is_constructible_v<common::PowerSettings, const int16_t&>);
static_assert(!std::is_constructible_v<common::PowerSettings, int16_t>);
static_assert(!std::is_constructible_v<common::PowerSettings, const int16_t>);

static_assert(std::is_constructible_v<common::AlarmSettings, const common::Alarm&>);
static_assert(!std::is_constructible_v<common::AlarmSettings, common::Alarm>);
static_assert(!std::is_constructible_v<common::AlarmSettings, const common::Alarm>);
