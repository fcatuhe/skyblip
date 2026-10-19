// What the part asks for so that it survives: the current limit, DS 15, and the silent failures.
#include <initializer_list>

#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "test/support/sx1262_rig.h"

using namespace skyblip;
using namespace skyblip::parts;

TEST_CASE("sx1262: SetPaConfig widens the current limit to 140 mA and the driver takes it back") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    CHECK(chip.ocp == models::Sx1262::kOcpReset);

    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    CHECK(chip.ocp == sx::kOcpLimit);
    CHECK(chip.ocp < models::Sx1262::kOcpAfterPaConfigSx1262);
    CHECK(chip.ocp == 48);  // 120 mA in the register's own 2.5 mA steps
}

// Under what +14 dBm draws through this PA configuration, the chip backs the power off.
TEST_CASE("sx1262: the current limit clears the 90 mA that +14 dBm costs, under Semtech's 140") {
    CHECK(sx::kOcpLimitMa > 90);
    CHECK(sx::kOcpLimitMa < 140);
    CHECK(sx::kOcpLimit * 5 / 2 == sx::kOcpLimitMa);
}

// Every retune reconfigures, and every reconfiguration issues the SetPaConfig that widens it again.
TEST_CASE("sx1262: the current limit survives every dwell, not only the first") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    for (uint32_t hz : {868200000u, 868400000u, 868200000u}) {
        config.frequency_hz = hz;
        REQUIRE(radio.configure_radio(config) == Status::Ok);
        CAPTURE(hz);
        CHECK(chip.ocp == sx::kOcpLimit);
    }
}

// DS 15.2: the reset threshold costs 5 to 6 dB into any antenna that is wet, detuned or shadowed.
TEST_CASE("sx1262: the PA clamp is widened out of reset, and only its own four bits are touched") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    CHECK(chip.tx_clamp == models::Sx1262::kTxClampReset);
    REQUIRE(radio.begin() == Status::Ok);
    CHECK((chip.tx_clamp & sx::kTxClampWidenBits) == sx::kTxClampWidenBits);
    // Bits 7-5 and 0 are the part's own.
    CHECK((chip.tx_clamp & 0xE1) == (models::Sx1262::kTxClampReset & 0xE1));
}

// DS 15.1.2 asks for this bit on any (G)FSK configuration, and every burst here is GFSK.
TEST_CASE("sx1262: the GFSK modulation-quality bit is set before anything is transmitted") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    CHECK((chip.tx_modulation & sx::kTxModulationGfskBit) == 0);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    CHECK((chip.tx_modulation & sx::kTxModulationGfskBit) != 0);
    CHECK((chip.tx_modulation & 0x01) == (models::Sx1262::kTxModulationReset & 0x01));
}

TEST_CASE("sx1262: a dwell reprogrammed on the other band keeps the modulation-quality bit") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.frequency_hz = 868400000;
    config.gaussian_bt_e2 = 50;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK((chip.tx_modulation & sx::kTxModulationGfskBit) != 0);
}

// DS 13.1.2 caution: the dwell thread can be armed the instant after it parks the radio.
TEST_CASE("sx1262: waking straight after sleeping does not interrupt the configuration save") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);
    radio.sleep();
    REQUIRE(chip.sleeping);
    REQUIRE(radio.wake() == Status::Ok);
    CHECK(chip.wakes == 1);
    CHECK(chip.fault == models::Sx1262::Fault::None);
    CHECK(chip.faults == 0);
}

// DS 13.1.2, as a literal: a constant compared with itself passes at any value.
TEST_CASE("sx1262: the configuration save is given the datasheet's 500 us before the wake") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    radio.sleep();
    REQUIRE(radio.wake() == Status::Ok);
    CHECK(chip.slept_us >= 500);
    CHECK(chip.fault == models::Sx1262::Fault::None);
}

TEST_CASE("sx1262: an NSS edge 499 us after SetSleep interrupts the save, 500 us does not") {
    const uint8_t sleep[2] = {sx::kSetSleep, sx::kSleepWarmStartNoRtc};
    for (const uint32_t waited_us : {499u, 500u}) {
        models::Sx1262 chip;
        chip.standby = true;
        chip.select(true);
        chip.transfer(sleep, nullptr, sizeof(sleep));
        chip.select(false);
        chip.wait_at_least_us(waited_us);
        chip.select(true);
        CHECK(chip.wakes == 1);
        CHECK((chip.fault == models::Sx1262::Fault::SpiBeforeSleepSettled) == (waited_us < 500));
    }
}

// DS 13.1.1: SetSleep is STDBY-only, and the dwell that just ended left the part receiving.
TEST_CASE("sx1262: the part is parked in standby before it is told to sleep") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);
    REQUIRE(chip.receiving);

    radio.sleep();
    CHECK(chip.fault == models::Sx1262::Fault::None);
    CHECK(chip.sleeping);
    CHECK_FALSE(chip.receiving);
    CHECK(radio.mode() == RadioMode::Sleep);
}

// DS 13.3.6: a TCXO part always raises XOSC_START_ERR at POR, and the host is told to clear it.
TEST_CASE("sx1262: the TCXO's expected start-up error is not read as a radio that failed") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    CHECK(radio.begin() == Status::Ok);
    CHECK(radio.device_errors() == 0);
}

// DS 8: "when BUSY is high, the host controller must wait until it goes down again".
TEST_CASE("sx1262: no command is put on the bus while the part is still busy with the last one") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);
    const uint8_t frame[4] = {1, 2, 3, 4};
    REQUIRE(radio.transmit(frame, sizeof(frame)) == Status::Ok);
    CHECK(chip.fault == models::Sx1262::Fault::None);
    CHECK(chip.faults == 0);
}

// A failed calibration has no symptom but a deaf radio, and deafness is cured by reinit, forever.
TEST_CASE("sx1262: a bring-up whose calibration failed reports the failing block, not silence") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    chip.fail_calibration = sx::kErrorPllCalibration;
    CHECK(radio.begin() == Status::Down);
    CHECK((radio.device_errors() & sx::kErrorPllCalibration) != 0);
    CHECK(chip.device_errors == 0);  // cleared, so the next bring-up answers for itself
}

TEST_CASE("sx1262: an image calibration that failed is not a radio that came up") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    chip.fail_calibration = sx::kErrorImageCalibration | sx::kErrorPllLock;
    CHECK(radio.begin() == Status::Down);
    CHECK((radio.device_errors() & sx::kErrorImageCalibration) != 0);
    CHECK((radio.device_errors() & sx::kErrorPllLock) != 0);
}

// The clock really not appearing is not the flag the TCXO raises for free at power-on.
TEST_CASE("sx1262: an XOSC that never starts is still caught, once the free flag is cleared") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    chip.fail_calibration = sx::kErrorXoscStart;
    CHECK(radio.begin() == Status::Down);
    CHECK((radio.device_errors() & sx::kErrorXoscStart) != 0);
}

TEST_CASE("sx1262: a healthy bring-up reports no device error and says so") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    CHECK(radio.begin() == Status::Ok);
    CHECK(radio.device_errors() == 0);
    CHECK(chip.saw_command(sx::kGetDeviceErrors));
}

// The bits above PA_RAMP_ERR are RFU, and a part that floats them has not failed a calibration.
TEST_CASE("sx1262: reserved OpError bits are not read as a failure") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    chip.fail_calibration = static_cast<uint16_t>(~sx::kDeviceErrorMask);
    CHECK(radio.begin() == Status::Ok);
    CHECK(radio.device_errors() == 0);
}
