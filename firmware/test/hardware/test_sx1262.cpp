// SX1262 driver recovery tests against models/sx1262.h with fault injection
// The class of intermittent bug that is hell to reproduce on hardware.
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "test/support/sx1262_rig.h"

using namespace skyblip;
using namespace skyblip::parts;

TEST_CASE("radio: begin configures the TCXO (DIO3) and RF switch (DIO2) for T-Echo wiring") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    CHECK(r.begin() == Status::Ok);
    // Without these two the SX1262 has no clock / no antenna path on this board.
    CHECK(chip.saw_cmd(sx::kSetDio2AsRfSwitch));
    CHECK(chip.saw_cmd(sx::kSetDio3AsTcxoCtrl));
    CHECK(chip.saw_cmd(sx::kCalibrate));
}

// B1. The reset default is LDO-only and there is no symptom: the radio works,
// and draws about twice the current it needs to for the ~980 ms of every second
// the receiver is armed. So the model is stricter than the silicon.
TEST_CASE("radio: begin puts the part on its DC-DC converter, before it calibrates against it") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    CHECK(chip.saw_cmd(sx::kSetRegulatorMode));
    CHECK(chip.regulator_dcdc);
    CHECK(chip.regulator_mode == sx::kRegulatorDcDc);
    // DS 13.1.4 is a standby-only command, and the calibration runs against the
    // supply configuration that is in force when it runs.
    CHECK(chip.cmd_order(sx::kSetRegulatorMode) < chip.cmd_order(sx::kCalibrate));
    CHECK(chip.cmd_order(sx::kSetRegulatorMode) < chip.cmd_order(sx::kCalibrateImage));
    CHECK(chip.fault == models::Sx1262::Fault::None);
}

TEST_CASE("radio: a chip that was never taken off its LDO is not believed") {
    // Straight at the model, so the omission is the driver's absence rather than
    // a driver doing something else wrong.
    models::Sx1262 chip;
    chip.standby = true;
    const uint8_t rx[4] = {sx::kSetRx, 0xFF, 0xFF, 0xFF};
    chip.select(true);
    chip.transfer(rx, nullptr, sizeof(rx));
    chip.select(false);
    CHECK(chip.faults > 0);

    // And the reset default is where it comes back to: NRESET returns every
    // block to LDO, so a reinit that skipped the command would be a radio that
    // silently doubled its receive current after the first watchdog recovery.
    models::Sx1262 restarted;
    Sx1262 r = make(restarted);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(restarted.regulator_dcdc);
    restarted.set(restarted.reset_pin, false);
    restarted.wait_at_least_us(models::Sx1262::kResetLowFloorUs);
    restarted.set(restarted.reset_pin, true);
    CHECK_FALSE(restarted.regulator_dcdc);
    CHECK(restarted.regulator_mode == sx::kRegulatorLdo);
    REQUIRE(r.begin() == Status::Ok);
    CHECK(restarted.regulator_dcdc);
}

// J1. The boosted receive gain: about 2 mA for about 3 dB (DS 9.6, and the same
// trade written in SoftRF's radio-sx126x.c:365-372). Taken, because 3 dB is
// about 40% more range on a collision warner and 2 mA is under a tenth of what
// the DC-DC converter above gives back on the same dwell map.
TEST_CASE("radio: the receiver runs on the boosted gain, not the reset default") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    // The reset default is the power-saving gain, so this is a write or it is
    // 3 dB nobody notices missing.
    CHECK(chip.rx_gain == sx::kRxGainBoosted);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    CHECK(chip.rx_gain == sx::kRxGainBoosted);
    REQUIRE(r.start_receive() == Status::Ok);
    CHECK(chip.rx_gain == sx::kRxGainBoosted);
}

// DS 9.6, below table 9-3: 0x08AC is not retained across a warm start unless it
// is on the retention list at 0x029F. Our sleep IS a warm start (DS 9.3), so
// without the list the radio comes back on the power-saving gain with nothing in
// any log to say the device lost 3 dB.
TEST_CASE("radio: the boosted gain survives the warm start the radio sleeps into") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    REQUIRE(chip.rx_gain == sx::kRxGainBoosted);

    r.sleep();
    CHECK(chip.sleeping);
    CHECK(chip.rx_gain == sx::kRxGainBoosted);
    REQUIRE(r.wake() == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    CHECK(chip.rx_gain == sx::kRxGainBoosted);

    // Straight at the model, so the datasheet's half of the contract is what is
    // being asserted: a part that was never told to retain the register loses it
    // on the same sleep.
    models::Sx1262 forgetful;
    forgetful.standby = true;
    const uint8_t gain[4] = {sx::kWriteRegister, 0x08, 0xAC, sx::kRxGainBoosted};
    forgetful.select(true);
    forgetful.transfer(gain, nullptr, sizeof(gain));
    forgetful.select(false);
    REQUIRE(forgetful.rx_gain == sx::kRxGainBoosted);
    const uint8_t sleep[2] = {sx::kSetSleep, sx::kSleepWarmStartNoRtc};
    forgetful.select(true);
    forgetful.transfer(sleep, nullptr, sizeof(sleep));
    forgetful.select(false);
    CHECK(forgetful.rx_gain == sx::kRxGainPowerSaving);
}

// And the reset default is where a recovered radio comes back to: NRESET clears
// the register and the retention list together, so the reinit path has to write
// both again or a device silently loses 3 dB after its first watchdog bite.
TEST_CASE("radio: a reinitialised radio is on the boosted gain again") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(chip.rx_gain == sx::kRxGainBoosted);

    chip.set(chip.reset_pin, false);
    chip.wait_at_least_us(models::Sx1262::kResetLowFloorUs);
    chip.set(chip.reset_pin, true);
    CHECK(chip.rx_gain == sx::kRxGainPowerSaving);

    REQUIRE(r.begin() == Status::Ok);
    CHECK(chip.rx_gain == sx::kRxGainBoosted);
    // The register is written in standby, which is where DS 13.1 allows it and
    // where the model insists on it.
    CHECK(chip.fault == models::Sx1262::Fault::None);
}

TEST_CASE("radio: begin + configure + receive brings the modem to Rx") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    CHECK(r.begin() == Status::Ok);
    CHECK(r.mode() == RadioMode::Standby);
    CHECK(r.configure_radio(RadioConfig{}) == Status::Ok);
    CHECK(r.start_receive() == Status::Ok);
    CHECK(r.mode() == RadioMode::Rx);
    CHECK(chip.reset_pulses >= 1);
}

TEST_CASE("radio: a queued RX packet is delivered via poll") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    uint8_t pkt[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    chip.queue_rx(pkt, 8);
    uint8_t buf[32];
    RadioEvent ev = r.poll(buf, sizeof(buf));
    CHECK(ev.type == RadioEventType::RxDone);
    CHECK(int(ev.len) == 8);
    CHECK(buf[0] == 0x11);
    CHECK(buf[7] == 0x88);
}

TEST_CASE("radio: a CRC-error RX is reported as CrcError, NOT delivered") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    uint8_t pkt[4] = {1, 2, 3, 4};
    chip.queue_rx(pkt, 4, /*crc_error=*/true);
    uint8_t buf[32];
    RadioEvent ev = r.poll(buf, sizeof(buf));
    CHECK(ev.type == RadioEventType::CrcError);
}

TEST_CASE("radio: BUSY stuck high is surfaced as a fault, not a hang") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    chip.busy_stuck = true;
    uint8_t buf[32];
    RadioEvent ev = r.poll(buf, sizeof(buf));
    CHECK(ev.type == RadioEventType::Fault);
}

TEST_CASE("radio: health watchdog reinitialises after no-RX timeout (no-RX-in-N reinit, tested)") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    CHECK(r.reinit_count() == 0);
    // 29 s: no reinit yet
    CHECK_FALSE(r.service(29000, 30000));
    // cross 30 s -> reinit
    CHECK(r.service(2000, 30000));
    CHECK(r.reinit_count() == 1);
    CHECK(r.mode() == RadioMode::Rx);  // back to receiving
    // a received packet resets the staleness timer
    uint8_t pkt[2] = {0xAB, 0xCD};
    chip.queue_rx(pkt, 2);
    uint8_t buf[8];
    r.poll(buf, sizeof(buf));
    CHECK_FALSE(r.service(29000, 30000));
}

TEST_CASE("radio: a reinit that failed after the reset keys nothing until one succeeds") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    chip.miso_dead = true;
    REQUIRE(r.service(31000, 30000));
    chip.miso_dead = false;

    CHECK(r.configure_radio(RadioConfig{}) == Status::Down);
    const uint8_t frame[4] = {1, 2, 3, 4};
    CHECK(r.transmit(frame, sizeof(frame)) == Status::Invalid);
    CHECK(r.start_receive() == Status::Invalid);
    CHECK_FALSE(chip.tx_pending);
    CHECK_FALSE(chip.receiving);

    REQUIRE(r.service(30000, 30000));
    CHECK(r.reinit_count() == 2);
    CHECK(r.mode() == RadioMode::Rx);
    CHECK(chip.tcxo_on_dio3);
    CHECK(r.configure_radio(RadioConfig{}) == Status::Ok);
}

// D4. The way off has to reach the part, not only the executor driving it.
TEST_CASE("radio: sleep is a warm start with the RTC off, and an NSS edge brings it back") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    r.sleep();
    CHECK(r.mode() == RadioMode::Sleep);
    // DS 13.1.2 sleepConfig: bit 0 is the RTC wake-up, bit 2 the warm start.
    CHECK(chip.sleep_config == sx::kSleepWarmStartNoRtc);
    CHECK((chip.sleep_config & 0x05) == 0x04);
    CHECK(r.wake() == Status::Ok);
    CHECK(r.start_receive() == Status::Ok);
    CHECK(chip.receiving);
    CHECK(r.wake() == Status::Ok);  // and nothing is owed by a radio that is awake
    CHECK(chip.wakes == 1);
}

TEST_CASE("radio: a transmission that never completes is recovered to RX and counted") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    const uint8_t frame[4] = {1, 2, 3, 4};
    REQUIRE(r.transmit(frame, sizeof(frame)) == Status::Ok);
    REQUIRE(r.mode() == RadioMode::Tx);
    CHECK(r.tx_recovery_count() == 0);
    REQUIRE(chip.expire_tx());  // TxDone never comes; the chip's timeout does
    uint8_t buf[32];
    CHECK(r.poll(buf, sizeof(buf)).type == RadioEventType::Timeout);
    CHECK(r.tx_recovery_count() == 1);
    CHECK(r.mode() == RadioMode::Rx);
    CHECK(chip.receiving);
}
