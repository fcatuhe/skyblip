// Each board's switch-off byte follows what its own factory bootloader does with a held button.
#include "boards/lilygo/t_echo_plus/pins.h"
#include "boards/seeed/meshtracker_x1/pins.h"
#include "core/power/shutdown.h"
#include "doctest/doctest.h"

using namespace skyblip;

TEST_CASE("boot magic: a T-Echo switch-off skips the DFU its wake press would enter") {
    CHECK(power::boot_magic_for_system_off(boards::t_echo_plus::kFactoryDfuButton, false) == 0x6d);
    CHECK(power::boot_magic_for_system_off(boards::t_echo_plus::kFactoryDfuButton, true) == 0x57);
}

// 0x6d made the X1's bootloader skip the 3 s hold that opens its UF2 drive.
TEST_CASE("boot magic: an X1 switch-off leaves its bootloader's 3 s hold to UF2 in reach") {
    CHECK(power::boot_magic_for_system_off(boards::meshtracker_x1::kFactoryDfuButton, false) ==
          0x00);
    CHECK(power::boot_magic_for_system_off(boards::meshtracker_x1::kFactoryDfuButton, true) ==
          0x57);
}
