// The air the simulated world gives a barometer, which the device's own code never computes.
#include "doctest/doctest.h"
#include "simulator/world/world.h"

using namespace skyblip;

TEST_CASE("world: the ISA temperature falls 6.5 C every kilometre from 15 C at the sea") {
    CHECK(simulator::isa_temperature_dc(0) == flight::kIsaSeaLevelDeciCelsius);
    CHECK(simulator::isa_temperature_dc(1000000) == 85);
    CHECK(simulator::isa_temperature_dc(3000000) == -45);
    // Below the sea the air is warmer, and a 500 m mine shaft is 3.25 C of it.
    CHECK(simulator::isa_temperature_dc(-500000) == 183);
}
