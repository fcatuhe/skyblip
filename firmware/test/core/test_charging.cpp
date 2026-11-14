// The charger runs on its own, so knowing and counting is all there is.
#include "core/power/charging.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::power;

TEST_CASE("charge: a cable in the cold and a cable in the sun are both named") {
    CHECK(charge_condition(true, true, 200) == ChargeCondition::Ok);
    CHECK(charge_condition(true, true, -100) == ChargeCondition::TooCold);
    // The parked-cockpit soak project/research/enclosure-and-mount-go.md designs against.
    CHECK(charge_condition(true, true, 724) == ChargeCondition::TooHot);

    CHECK(charge_condition(true, true, kChargeColdFloorDeciCelsius) == ChargeCondition::Ok);
    CHECK(charge_condition(true, true, kChargeColdFloorDeciCelsius - 1) ==
          ChargeCondition::TooCold);
    CHECK(charge_condition(true, true, kChargeHotCeilingDeciCelsius) == ChargeCondition::Ok);
    CHECK(charge_condition(true, true, kChargeHotCeilingDeciCelsius + 1) ==
          ChargeCondition::TooHot);
}

// Plating a cell below freezing is permanent, and the die sits above the air.
TEST_CASE("charge: the cold limit is a die reading, not an ambient one") {
    CHECK(kChargeColdFloorDeciCelsius >= 50);
    CHECK(charge_condition(true, true, 40) == ChargeCondition::TooCold);
}

// A cell nobody charges and a cell nobody measures are both silence, not alarm.
TEST_CASE("charge: nothing is claimed about a cell nobody is charging or measuring") {
    CHECK(charge_condition(false, true, 900) == ChargeCondition::Unknown);
    CHECK(charge_condition(false, true, -400) == ChargeCondition::Unknown);
    CHECK(charge_condition(true, false, 900) == ChargeCondition::Unknown);
    CHECK(charge_condition(false, false, 0) == ChargeCondition::Unknown);
}
