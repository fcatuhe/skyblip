// Each board's switch-off byte follows what its own factory bootloader does with a held button.
#include "boards/lilygo/t_echo_plus/pins.h"
#include "boards/seeed/meshtracker_x1/pins.h"
#include "core/power/shutdown.h"
#include "doctest/doctest.h"

using namespace skyblip;

TEST_CASE("boot magic: a T-Echo switch-off skips the DFU its wake press would enter" *
          doctest::test_suite("main")) {
    CHECK(power::boot_magic_for_system_off(boards::t_echo_plus::kFactoryDfuButton, false,
                                           power::Switch::Button) == 0x6d);
    CHECK(power::boot_magic_for_system_off(boards::t_echo_plus::kFactoryDfuButton, true,
                                           power::Switch::Button) == 0x57);
}

// 0x6d made the X1's bootloader skip the 3 s hold that opens its UF2 drive.
TEST_CASE("boot magic: an X1 switch-off leaves its bootloader's 3 s hold to UF2 in reach" *
          doctest::test_suite("main")) {
    CHECK(power::boot_magic_for_system_off(boards::meshtracker_x1::kFactoryDfuButton, false,
                                           power::Switch::Button) == 0x00);
    CHECK(power::boot_magic_for_system_off(boards::meshtracker_x1::kFactoryDfuButton, true,
                                           power::Switch::Button) == 0x57);
}

// Under cable mode the T-Echo's bootloader must let a cable wake through, and its button is left
// unarmed.
TEST_CASE("boot magic: a T-Echo in cable mode switches off for the cable to wake it" *
          doctest::test_suite("main")) {
    CHECK(power::boot_magic_for_system_off(boards::t_echo_plus::kFactoryDfuButton, false,
                                           power::Switch::Cable) == 0x00);
    CHECK_FALSE(power::button_wakes(boards::t_echo_plus::kFactoryDfuButton, power::Switch::Cable));
    CHECK(power::button_wakes(boards::t_echo_plus::kFactoryDfuButton, power::Switch::Button));
    CHECK(power::button_wakes(boards::meshtracker_x1::kFactoryDfuButton, power::Switch::Cable));
}
