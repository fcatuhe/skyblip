#include "core/power/charging.h"

namespace skyblip::power {

ChargeCondition charge_condition(bool external_power, bool temperature_valid,
                                 int16_t temperature_dc) {
    if (!external_power || !temperature_valid) return ChargeCondition::Unknown;
    if (temperature_dc < kChargeColdFloorDeciCelsius) return ChargeCondition::TooCold;
    if (temperature_dc > kChargeHotCeilingDeciCelsius) return ChargeCondition::TooHot;
    return ChargeCondition::Ok;
}

}  // namespace skyblip::power
