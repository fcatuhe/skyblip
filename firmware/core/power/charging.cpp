#include "core/power/charging.h"

namespace skyblip::power {

const char* to_string(ChargeCondition condition) {
    switch (condition) {
        case ChargeCondition::Ok: return "OK";
        case ChargeCondition::TooCold: return "COLD";
        case ChargeCondition::TooHot: return "HOT";
        case ChargeCondition::Unknown: break;
    }
    return "UNKNOWN";
}

ChargeCondition charge_condition(bool external_power, bool temperature_valid,
                                 int16_t temperature_dc) {
    if (!external_power || !temperature_valid) return ChargeCondition::Unknown;
    if (temperature_dc < kChargeColdFloorDeciCelsius) return ChargeCondition::TooCold;
    if (temperature_dc > kChargeHotCeilingDeciCelsius) return ChargeCondition::TooHot;
    return ChargeCondition::Ok;
}

}  // namespace skyblip::power
