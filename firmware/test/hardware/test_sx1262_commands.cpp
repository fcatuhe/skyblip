// What the datasheet asks the driver to write, and in what order: IRQ masks, PA, image, reset.
#include <initializer_list>

#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "test/support/sx1262_rig.h"

using namespace skyblip;
using namespace skyblip::parts;

// A1. The modem is the one part of this chip that cannot be left at its reset
// defaults: RadioConfig carried a bitrate and a deviation nothing ever wrote.
// A2. And the other band, which is a different modulation and not merely a
// different frequency: ADS-L 4 SRD-860 issue 2 §C.4 is 200 kbps GMSK, BT 0.5,
// in a 250 kHz channel. The driver used to hard-code §C.2's unshaped 100 kbps
// whatever it was handed, so the O-band dwell retuned the synthesiser and
// listened at half the rate a skyPost transmits at.
// A2. IrqMask is 0x0000 out of reset and gates the status register, so an
// unprogrammed mask is a radio that reports neither RxDone nor TxDone, for ever.
TEST_CASE("radio: an IRQ bit that was never unmasked is never reported") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    uint8_t pkt[4] = {1, 2, 3, 4};
    chip.queue_rx(pkt, 4);
    uint8_t buf[32];
    CHECK(r.poll(buf, sizeof(buf)).type == RadioEventType::None);
    CHECK(r.start_receive() == Status::Ok);
    CHECK(r.poll(buf, sizeof(buf)).type == RadioEventType::RxDone);
}

TEST_CASE("radio: the IRQ mask and the DIO1 mask are programmed before the receiver is started") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    CHECK(chip.irq_mask == 0);
    REQUIRE(r.start_receive() == Status::Ok);
    CHECK(chip.cmd_order(sx::kSetDioIrqParams) < chip.cmd_order(sx::kSetRx));
    CHECK((chip.irq_mask & sx::kIrqRxDone) != 0);
    CHECK((chip.irq_mask & sx::kIrqCrcErr) != 0);
    // DIO1 is the only interrupt line wired on this board.
    CHECK(chip.dio1_mask == chip.irq_mask);
    chip.queue_rx(nullptr, 0);
    CHECK(chip.get(chip.dio1_pin));
}

TEST_CASE("radio: the IRQ mask is programmed before the transmitter is keyed") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    const uint8_t frame[4] = {1, 2, 3, 4};
    REQUIRE(r.transmit(frame, sizeof(frame)) == Status::Ok);
    CHECK(chip.cmd_order(sx::kSetDioIrqParams) < chip.cmd_order(sx::kSetTx));
    CHECK((chip.irq_mask & sx::kIrqTxDone) != 0);
    CHECK((chip.irq_mask & sx::kIrqTimeout) != 0);
    chip.signal_tx_done();
    uint8_t buf[8];
    CHECK(r.poll(buf, sizeof(buf)).type == RadioEventType::TxDone);
}

// A3. Nothing in this tree set output power. The PA config is also the write
// that raises OCP, and the band's ceiling is a regulation, not a preference.
TEST_CASE("radio: the PA is the SX1262 high-power configuration, ordered before the power") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    CHECK(chip.pa_set);
    CHECK(chip.pa_config[0] == 0x04);
    CHECK(chip.pa_config[1] == 0x07);
    CHECK(chip.pa_config[2] == 0x00);
    CHECK(chip.pa_config[3] == 0x01);
    CHECK(sx::kPaConfigHighPowerRatedDbm == 22);
    CHECK(chip.cmd_order(sx::kSetPaConfig) < chip.cmd_order(sx::kSetTxParams));
}

// The register takes conducted power and the law limits radiated power, so what
// is pinned here is both the number written to the chip and the arithmetic that
// makes it legal: feed loss out, antenna gain in, dBi referenced back to the
// dipole the e.r.p. limit is written against. Change the antenna and this is the
// case that fails.
TEST_CASE("radio: output power is 14 dBm conducted, which is under 25 mW e.r.p. on our antenna") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    CHECK(chip.tx_power_set);
    CHECK(chip.tx_power_dbm == sx::kConductedDbm);
    CHECK(chip.tx_power_dbm == 14);
    CHECK(chip.ramp_time == sx::kRampTime200Us);

    // 14 dBm conducted, 0.5 dB of feed, a 1.6 dBi whip: 12.95 dBm e.r.p. against
    // a 14 dBm ceiling (ERC 70-03 band h1.4). A quarter-wave whip sits below a
    // dipole, so the part runs out of power before the regulation does.
    CHECK(sx::kResultingErpCentiDb == 1295);
    CHECK(sx::kResultingErpCentiDb <= sx::kSrd868ErpLimitDbm * 100);
}

TEST_CASE("radio: a chip whose output power was never programmed refuses to transmit") {
    models::Sx1262 chip;
    const uint8_t frame[3] = {1, 2, 3};
    chip.select(true);
    chip.transfer(frame, nullptr, 1);
    chip.select(false);
    chip.select(true);
    const uint8_t tx[4] = {sx::kSetTx, 0, 0, 0};
    chip.transfer(tx, nullptr, sizeof(tx));
    chip.select(false);
    CHECK(chip.fault == models::Sx1262::Fault::TxWithoutPower);
    CHECK_FALSE(chip.tx_pending);
}

// A4. Image rejection is calibrated per band, and only against the clock the
// chip will actually run on.
TEST_CASE("radio: image rejection is calibrated for 863-870 MHz after the TCXO is up") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    CHECK(chip.image_calibrated);
    CHECK(chip.image_band[0] == 0xD7);
    CHECK(chip.image_band[1] == 0xDB);
    CHECK(chip.cmd_order(sx::kSetDio3AsTcxoCtrl) < chip.cmd_order(sx::kCalibrateImage));
    CHECK(chip.cmd_order(sx::kCalibrate) < chip.cmd_order(sx::kCalibrateImage));
    CHECK(chip.fault == models::Sx1262::Fault::None);
    // Once per band: the second dwell does not pay for it again.
    const int before = chip.cmd_order(sx::kCalibrateImage);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    CHECK(chip.cmd_order(sx::kCalibrateImage) == before);
}

// A5. The previous dwell leaves the chip in continuous RX, and SetRfFrequency
// and SetPacketType are standby-only commands.
TEST_CASE("radio: retuning a receiving radio is bracketed by standby and returns to RX") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    RadioConfig cfg{};
    cfg.freq_hz = 868200000;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    REQUIRE(r.mode() == RadioMode::Rx);

    int standbys = 0;
    for (uint8_t c : chip.cmds_seen)
        if (c == sx::kSetStandby) standbys++;
    cfg.freq_hz = 868400000;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);
    int after = 0;
    for (uint8_t c : chip.cmds_seen)
        if (c == sx::kSetStandby) after++;
    CHECK(after == standbys + 1);
    CHECK(chip.fault == models::Sx1262::Fault::None);
    CHECK(chip.freq_hz > 868399000);
    CHECK(r.mode() == RadioMode::Rx);
    CHECK(chip.receiving);
}

TEST_CASE("radio: a standby-only command issued while the receiver runs is a chip fault") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    REQUIRE(chip.fault == models::Sx1262::Fault::None);
    const uint8_t retune[5] = {sx::kSetRfFrequency, 0x36, 0x40, 0x00, 0x00};
    chip.select(true);
    chip.transfer(retune, nullptr, sizeof(retune));
    chip.select(false);
    CHECK(chip.fault == models::Sx1262::Fault::ConfigOutsideStandby);
}

// DS 8.1, as a literal: a constant compared with itself passes at any value.
TEST_CASE("radio: NRESET is held low for the datasheet's 100 us") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    CHECK(chip.reset_pulses == 1);
    CHECK(chip.reset_low_us >= 100);
    CHECK(chip.fault == models::Sx1262::Fault::None);
}

TEST_CASE("radio: a reset pulse one microsecond under 100 us is a fault, 100 us is not") {
    models::Sx1262 held;
    held.set(held.reset_pin, false);
    held.wait_at_least_us(100);
    held.set(held.reset_pin, true);
    CHECK(held.fault == models::Sx1262::Fault::None);

    models::Sx1262 rushed;
    rushed.set(rushed.reset_pin, false);
    rushed.wait_at_least_us(99);
    rushed.set(rushed.reset_pin, true);
    CHECK(rushed.fault == models::Sx1262::Fault::ShortReset);
}

// A7. SetTx with no timeout is a PA that stays keyed when TxDone never arrives.
TEST_CASE("radio: SetTx carries a timeout past the frame's air time at the configured bitrate") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    RadioConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);
    uint8_t frame[protocol::kAdslFrameBytes] = {0};
    REQUIRE(r.transmit(frame, sizeof(frame)) == Status::Ok);
    CHECK(chip.tx_timeout_ticks != 0);
    // 100 kbps: one bit is 10 us. Preamble + sync window + payload.
    const uint32_t air_us =
        (sx::kPreambleChips + protocol::kSharedSyncBits + sizeof(frame) * 8u) * 10u;
    const uint32_t timeout_us = chip.tx_timeout_ticks * sx::kTimeoutStepNs / 1000u;
    CHECK(timeout_us > air_us);
    CHECK(timeout_us < air_us + 30000);
}

// A8. BUSY going low proves a rail and a pull-down, which an empty footprint
// and an unsoldered MISO pad manage too. Only a value out of the part proves it.
TEST_CASE("radio: presence is a register round-trip, and a dead MISO is an absent radio") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    CHECK(r.probe() == Status::Ok);
    CHECK(r.begin() == Status::Ok);  // and leaves the part where bring-up wants it
    for (uint8_t rail : {uint8_t{0x00}, uint8_t{0xFF}}) {
        models::Sx1262 mute;  // BUSY still answers: the old check still passed
        mute.miso_dead = true;
        mute.miso_level = rail;
        Sx1262 absent = make(mute);
        CHECK(absent.probe() == Status::Down);
        CHECK(absent.begin() == Status::Down);
    }
}
