// The haptic driver, against its model.
//
// The bug this part exists to fix: pins.h named P0.08 "DRV haptic / vibration
// motor enable" and the annunciator drove it as a plain GPIO with a timer, so
// every claim in the test suite about the alarm escalating to vibration was a
// claim about a pin. On a T-Echo Plus that pin is a DRV2605's enable: raising it
// brings a waveform driver out of hardware standby and moves nothing at all. The
// first test below is that fact, and it fails against the old annunciator.
#include "doctest/doctest.h"
#include "hardware/parts/drv2605/drv2605.h"
#include "hardware/parts/drv2605/model.h"

using namespace skyblip;

namespace {

parts::Drv2605 driver_for(models::Drv2605& chip,
                          parts::Drv2605::Actuator actuator = parts::Drv2605::Actuator::Erm) {
    return parts::Drv2605(chip, chip, chip, chip.enable_pin, actuator);
}

parts::Drv2605 lra_driver_for(models::Drv2605& chip) {
    return driver_for(chip, parts::Drv2605::Actuator::Lra);
}

}  // namespace

TEST_CASE("drv2605: the enable pin on its own produces no pulse") {
    models::Drv2605 chip;
    parts::Drv2605 haptic = driver_for(chip);

    // Exactly what the annunciator used to do: drive the pin, wait, drop it.
    haptic.power_up();
    CHECK(chip.enabled);
    CHECK(chip.enable_raises == 1);
    CHECK_FALSE(chip.moving());
    CHECK(chip.drive() == 0);
}

TEST_CASE("drv2605: bring-up identifies the part and leaves it configured and asleep") {
    models::Drv2605 chip;
    // INFO: fc 12nov26 left as an LRA by another firmware, so the driver writes the ERM bits below
    chip.registers[0x1A] = 0xB6;
    chip.registers[0x1D] = 0x00;
    parts::Drv2605 haptic = driver_for(chip);

    REQUIRE(haptic.begin() == Status::Ok);
    CHECK(haptic.ready());
    // STATUS[7:5] = 7 is the DRV2605L the model presents.
    CHECK(int(haptic.device_id()) == 7);

    // The enable line is up before anything on the bus is believed.
    CHECK(chip.enabled);
    // An ERM in open loop: the fitted motor is in no datasheet we have, so
    // closed loop has no rated voltage or back-EMF it could trust.
    CHECK(chip.erm_selected());
    CHECK(chip.open_loop());
    CHECK(int(chip.library()) == 1);
    CHECK(chip.calibrations == 0);
    CHECK_FALSE(haptic.calibration().ran);
    // Configured, and drawing nothing: a driver stage idling is milliamps on a
    // device that counts microamps in a flight bag.
    CHECK(chip.standby());
    CHECK_FALSE(chip.moving());
}

TEST_CASE("drv2605: a part that does not answer its address is absent, not broken") {
    models::Drv2605 chip;
    chip.answers = false;
    parts::Drv2605 haptic = driver_for(chip);

    CHECK(haptic.begin() == Status::Down);
    CHECK_FALSE(haptic.ready());

    // And it stays silent: a driver that pretends is worse than one that admits.
    haptic.start();
    CHECK_FALSE(chip.moving());
}

TEST_CASE("drv2605: something else at 0x5A is not a haptic driver") {
    models::Drv2605 chip;
    chip.registers[0x00] = 0x00;  // device id 0: not any DRV260x
    parts::Drv2605 haptic = driver_for(chip);

    CHECK(haptic.begin() == Status::Unsupported);
    CHECK_FALSE(haptic.ready());
    haptic.start();
    CHECK_FALSE(chip.moving());
}

TEST_CASE("drv2605: a pulse is a drive value that holds, and stops when it is told") {
    models::Drv2605 chip;
    parts::Drv2605 haptic = driver_for(chip);
    REQUIRE(haptic.begin() == Status::Ok);

    haptic.start();
    CHECK(chip.moving());
    CHECK_FALSE(chip.standby());
    // Real-time playback: the drive value is a register that holds, so the pulse
    // is as long as the adapter's timer rather than as long as some entry in the
    // effect ROM happens to be.
    CHECK(int(chip.mode()) == 0x05);
    CHECK(int(chip.drive()) == 0x7F);

    // It holds. Nothing here is a one-shot that a second reader could miss.
    CHECK(chip.moving());

    haptic.stop();
    CHECK_FALSE(chip.moving());
    CHECK(chip.standby());
}

TEST_CASE("drv2605: a second start inside a pulse does not restack the bus traffic") {
    models::Drv2605 chip;
    parts::Drv2605 haptic = driver_for(chip);
    REQUIRE(haptic.begin() == Status::Ok);

    haptic.start();
    const int writes = chip.writes;
    haptic.start();
    CHECK(chip.writes == writes);
    CHECK(chip.moving());
}

TEST_CASE("drv2605: parking clears the GO bit a bench session may have left set") {
    models::Drv2605 chip;
    parts::Drv2605 haptic = driver_for(chip);
    REQUIRE(haptic.begin() == Status::Ok);

    // A ROM sequence left running by anything else: mode 0, GO set, waveform 1.
    chip.registers[0x01] = 0x00;
    chip.registers[0x04] = 0x01;
    chip.registers[0x0C] = 0x01;
    REQUIRE(chip.moving());

    haptic.park();
    CHECK_FALSE(chip.moving());
    CHECK(int(chip.registers[0x0C]) == 0);
}

// EN low is the part's own shutdown, and every road into SYSTEM OFF parks the board.
TEST_CASE("drv2605: parking leaves an answering part in standby with its enable held low") {
    models::Drv2605 chip;
    parts::Drv2605 haptic = driver_for(chip);
    REQUIRE(haptic.begin() == Status::Ok);

    haptic.park();
    CHECK(chip.standby());
    CHECK_FALSE(chip.enabled);
    CHECK(chip.enable_driven);
}

// Bring-up raised P0.08 on every board, and on a plain T-Echo that pin is console RX.
TEST_CASE("drv2605: parking releases the enable of a part that never answered") {
    models::Drv2605 chip;
    chip.answers = false;
    parts::Drv2605 haptic = driver_for(chip);
    REQUIRE(haptic.begin() != Status::Ok);
    REQUIRE(chip.enable_driven);

    haptic.park();
    CHECK_FALSE(chip.enabled);
    CHECK_FALSE(chip.enable_driven);
}

// Meshtastic drives the X1's motor as an LRA, and our ERM drive gave the first unit a weak buzz.
TEST_CASE("drv2605: an LRA is driven in closed loop at full scale, without a calibration buzz") {
    models::Drv2605 chip;
    // INFO: fc 12nov26 left by another firmware as an ERM, with unsigned RTP data and LRA open loop
    chip.registers[0x1A] = 0x35;
    chip.registers[0x1D] = 0xA9;
    parts::Drv2605 haptic = lra_driver_for(chip);

    REQUIRE(haptic.begin() == Status::Ok);
    CHECK(chip.lra_closed_loop());
    CHECK(int(chip.registers[0x1A]) == 0xB5);
    CHECK(chip.rtp_signed());
    CHECK(int(chip.library()) == 6);
    CHECK(int(chip.registers[0x16]) == 0x3E);
    CHECK(int(chip.registers[0x17]) == 0x8C);
    CHECK(chip.calibrations == 0);
    CHECK_FALSE(chip.otp_burned);
    CHECK(chip.standby());

    haptic.start();
    CHECK(int(chip.drive()) == 0x7F);
    haptic.stop();
    CHECK_FALSE(chip.moving());
}

TEST_CASE("drv2605: a part with programmed OTP keeps its rated voltage and is still an LRA") {
    models::Drv2605 chip;
    chip.registers[0x1E] |= 0x04;
    chip.registers[0x16] = 0x50;
    chip.registers[0x17] = 0x90;
    parts::Drv2605 haptic = lra_driver_for(chip);

    REQUIRE(haptic.begin() == Status::Ok);
    CHECK(haptic.otp_programmed());
    CHECK(int(chip.registers[0x16]) == 0x50);
    CHECK(int(chip.registers[0x17]) == 0x90);
    CHECK(chip.lra_closed_loop());
    CHECK_FALSE(chip.otp_burned);
}

TEST_CASE("drv2605: a calibration reports what the part converged on and leaves it asleep") {
    models::Drv2605 chip;
    parts::Drv2605 haptic = lra_driver_for(chip);
    REQUIRE(haptic.begin() == Status::Ok);

    haptic.calibrate();
    CHECK(chip.calibrations == 1);
    CHECK((chip.calibrated_feedback & 0x80) != 0);
    CHECK(int(chip.calibrated_rated_voltage) == 0x3E);
    CHECK(int(chip.calibrated_overdrive_clamp) == 0x8C);
    CHECK(haptic.calibration().ran);
    CHECK(haptic.calibration().passed);
    CHECK(int(haptic.calibration().compensation) == models::Drv2605::kConvergedCompensation);
    CHECK(int(haptic.calibration().back_emf) == models::Drv2605::kConvergedBackEmf);
    CHECK(int(haptic.calibration().back_emf_gain) == models::Drv2605::kConvergedBackEmfGain);
    CHECK(int(chip.registers[0x18]) == models::Drv2605::kConvergedCompensation);
    CHECK(int(chip.registers[0x19]) == models::Drv2605::kConvergedBackEmf);
    CHECK(int(chip.registers[0x1A] & 0x03) == models::Drv2605::kConvergedBackEmfGain);
    CHECK(chip.standby());
    CHECK_FALSE(chip.moving());
}

TEST_CASE("drv2605: a calibration that does not converge is reported, and the held results drive") {
    models::Drv2605 chip;
    chip.calibration_converges = false;
    parts::Drv2605 haptic = lra_driver_for(chip);
    REQUIRE(haptic.begin() == Status::Ok);
    chip.registers[0x18] = 0x11;
    chip.registers[0x19] = 0x22;
    chip.registers[0x1A] = 0xB5;

    haptic.calibrate();
    CHECK(haptic.calibration().ran);
    CHECK_FALSE(haptic.calibration().passed);
    CHECK(int(haptic.calibration().compensation) == 0xFF);
    CHECK(int(chip.registers[0x18]) == 0x11);
    CHECK(int(chip.registers[0x19]) == 0x22);
    CHECK(int(chip.registers[0x1A]) == 0xB5);

    haptic.start();
    CHECK(chip.moving());
}

TEST_CASE("drv2605: a calibration that never ends is given up at its ceiling and reported") {
    models::Drv2605 chip;
    chip.calibration_hangs = true;
    parts::Drv2605 haptic = lra_driver_for(chip);
    REQUIRE(haptic.begin() == Status::Ok);

    haptic.calibrate();
    CHECK(haptic.calibration().ran);
    CHECK_FALSE(haptic.calibration().passed);
    CHECK(chip.waited_us == parts::Drv2605::kCalibrationCeilingMs * 1000);
    CHECK(int(chip.registers[0x0C]) == 0);
    CHECK(chip.standby());
    CHECK_FALSE(chip.moving());
}

TEST_CASE("drv2605: an ERM, or a part that never answered, is not calibrated") {
    models::Drv2605 erm_chip;
    parts::Drv2605 erm = driver_for(erm_chip);
    REQUIRE(erm.begin() == Status::Ok);
    erm.calibrate();
    CHECK(erm_chip.calibrations == 0);
    CHECK_FALSE(erm.calibration().ran);

    models::Drv2605 silent_chip;
    silent_chip.answers = false;
    parts::Drv2605 silent = lra_driver_for(silent_chip);
    REQUIRE(silent.begin() == Status::Down);
    silent.calibrate();
    CHECK_FALSE(silent.calibration().ran);
}
