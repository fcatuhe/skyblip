// What the LR2021 driver asks of the part, in what order, and what it hands up from 1090 MHz.
#include <cstdint>

#include "core/protocol/adsb.h"
#include "doctest/doctest.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "hardware/parts/lr2021/model.h"
#include "test/support/adsb_frames.h"
#include "test/support/lr2021_rig.h"

using namespace skyblip;
using parts::Lr2021;
using parts::Lr2021EventType;
using parts::Lr2021Mode;
namespace chip = skyblip::parts::lr2021;

namespace {

constexpr uint32_t kAddress = 0x4840D6;
constexpr uint32_t kOtherAddress = 0x3C6586;

uint32_t address_in(const uint8_t* chips) {
    protocol::AdsbMessage message{};
    if (protocol::adsb_receive(chips, protocol::kAdsbChipBytes, protocol::AdsbReception{0, -70},
                               message) != Status::Ok)
        return 0;
    return message.address;
}

AdsbAir identification(uint32_t address) { return adsb_air(adsb_frame(address, 5, 4)); }

}  // namespace

TEST_CASE("lr2021: bring-up holds NRESET for 100 us and reads the version before anything else") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_for(part);
    REQUIRE(radio.begin(chip::kDetectionThresholdDbm) == Status::Ok);
    CHECK(part.reset_pulses == 1);
    CHECK(part.fault == models::Lr2021::Fault::None);
    CHECK(part.command_order(chip::kGetVersion) == 0);
    CHECK(radio.mode() == Lr2021Mode::Standby);
    CHECK(radio.command_errors() == 0);
    CHECK(part.refused.empty());
}

TEST_CASE("lr2021: a part that does not answer version 1.24 is not an LR2021") {
    models::Lr2021 sibling;
    sibling.version_major = 0x02;
    sibling.version_minor = 0x00;
    CHECK(lr2021_for(sibling).probe() == Status::Down);

    models::Lr2021 unsoldered;
    unsoldered.miso_dead = true;
    Lr2021 radio = lr2021_for(unsoldered);
    CHECK(radio.probe() == Status::Down);
    CHECK(radio.begin(chip::kDetectionThresholdDbm) == Status::Down);

    models::Lr2021 fitted;
    CHECK(lr2021_for(fitted).probe() == Status::Ok);
}

// RadioLib waits up to 3 s for the boot calibration, and the datasheet names no figure.
TEST_CASE("lr2021: BUSY that never falls after reset times out at the boot ceiling") {
    models::Lr2021 part;
    part.busy_stuck = true;
    Lr2021 radio = lr2021_for(part);
    CHECK(radio.begin(chip::kDetectionThresholdDbm) == Status::Timeout);
    CHECK(part.elapsed_us >= chip::kBootBusyCeilingUs);
    CHECK(part.elapsed_us < chip::kBootBusyCeilingUs + 2 * chip::kBusySleepPollUs + 1000);
    CHECK(part.commands_seen.empty());
    CHECK(radio.mode() == Lr2021Mode::Reset);
}

TEST_CASE("lr2021: a boot that takes its time is waited out, not refused") {
    models::Lr2021 part;
    part.boot_busy_us = 300000;
    part.calibrate_busy_us = 5000;
    Lr2021 radio = lr2021_for(part);
    CHECK(radio.begin(chip::kDetectionThresholdDbm) == Status::Ok);
    CHECK(part.fault == models::Lr2021::Fault::None);
}

TEST_CASE("lr2021: the TCXO is powered at 1.6 V from standby RC before the calibration") {
    models::Lr2021 part;
    REQUIRE(lr2021_for(part).begin(chip::kDetectionThresholdDbm) == Status::Ok);
    CHECK(part.tcxo_set);
    CHECK(part.tcxo_tune == 0x00);
    CHECK(part.tcxo_start_time == 32765);
    CHECK(part.command_order(chip::kSetStandby) < part.command_order(chip::kSetTcxoMode));
    CHECK(part.command_order(chip::kSetTcxoMode) < part.command_order(chip::kCalibrate));
    CHECK(part.calibrated);
    CHECK(part.calibrated_blocks == 0x6F);
    CHECK(part.dio_function[8] == 0x11);
    CHECK(part.command_order(chip::kSetDioFunction) < part.command_order(chip::kCalibrate));
    CHECK(part.fallback_mode == 0x01);
}

TEST_CASE("lr2021: the modem is SoftRF's 1090 MHz OOK receiver") {
    models::Lr2021 part;
    REQUIRE(lr2021_for(part).begin(chip::kDetectionThresholdDbm) == Status::Ok);
    CHECK(part.packet_type == 0x0A);
    CHECK(part.bitrate_bps == 2000000);
    CHECK(part.pulse_shape == 0x00);
    CHECK(part.rx_bandwidth == 0x00);
    CHECK(part.depth == 0x00);
    CHECK(part.preamble_chips == 16);
    CHECK(part.packet_format == 0x00);
    CHECK(part.payload_bytes == protocol::kAdsbChipBytes);
    CHECK(part.crc_manchester == 0x00);
    CHECK(part.whitening_polynom == 0);
    CHECK(part.sync == 0x95);
    CHECK(part.sync_bits == 8);
    CHECK(part.sync_msb_first);
    CHECK(part.pattern == 0x0285);
    CHECK(part.pattern_length == 16);
    CHECK(part.pattern_repeats == 0);
    CHECK(part.sfd == 0);
    CHECK(part.agc_gain == 0);
    CHECK(part.rx_path == 0x00);
    CHECK(part.calib_fe_word == 273);
    CHECK(part.calib_fe_bytes == 6);
    CHECK(part.frequency_hz == 1090000000);
    CHECK(part.command_order(chip::kSetPacketType) <
          part.command_order(chip::kSetOokModulationParams));
    CHECK(part.command_order(chip::kCalibFe) < part.command_order(chip::kSetRfFrequency));
}

// The detector matches its pattern first bit first (DS 16.3.8), and the air sends the preamble.
TEST_CASE("lr2021: the detection pattern is the 8 us ADS-B preamble, first chip first") {
    uint16_t air = 0;
    for (int bit = 0; bit < chip::kAdsbPreambleChips; bit++)
        air = static_cast<uint16_t>((air << 1) | ((chip::kAdsbPreamblePattern >> bit) & 1u));
    CHECK(air == ((kAdsbPreambleAir[0] << 8) | kAdsbPreambleAir[1]));
}

TEST_CASE("lr2021: the threshold and SoftRF's register go in by masked write after the modem") {
    models::Lr2021 part;
    REQUIRE(lr2021_for(part).begin(chip::kDetectionThresholdDbm) == Status::Ok);
    CHECK(part.masks[0xF30E14] == 0x07F00000);
    CHECK(part.reg(0xF30E14) == 0x07000000);
    CHECK(part.masks[0xF30150] == 0x000000FF);
    CHECK(part.reg(0xF30150) == 0x18);
    CHECK(part.last_order(chip::kWriteRegMemMask32) >
          part.command_order(chip::kSetOokModulationParams));
}

// SoftRF asks RadioLib for -80 dBm, and RadioLib writes level + 64 where Semtech writes level + 74.
TEST_CASE("lr2021: the default threshold writes the field SoftRF's -80 dBm request wrote") {
    const int radiolib_field = (64 - 80) & 0x7F;
    CHECK(((chip::kDetectionThresholdDbm + chip::kOokThresholdOffsetDb) & 0x7F) == radiolib_field);

    models::Lr2021 part;
    REQUIRE(lr2021_for(part).begin(-100) == Status::Ok);
    CHECK(part.reg(0xF30E14) == static_cast<uint32_t>(((-100 + 74) & 0x7F) << 20));
}

TEST_CASE("lr2021: receive is continuous, with RxDone alone on the IRQ line") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_for(part);
    REQUIRE(radio.begin(chip::kDetectionThresholdDbm) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);
    CHECK(radio.mode() == Lr2021Mode::Receive);
    CHECK(part.mode == models::Lr2021::Mode::Receive);
    CHECK(part.rx_timeout == 0xFFFFFF);
    CHECK(part.dio_irq_mask[8] == chip::kIrqRxDone);
    CHECK(part.listening_for_adsb());
    CHECK(part.refused.empty());
}

TEST_CASE("lr2021: the 27 chips after the 0x95 sync are a frame the DF17 decoder reads") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    REQUIRE(radio.mode() == Lr2021Mode::Receive);
    const AdsbAir air = identification(kAddress);
    REQUIRE(part.receive_air(air.chips, sizeof(air.chips), -61));
    CHECK(radio.irq_asserted());

    uint8_t chips[protocol::kAdsbChipBytes]{};
    const parts::Lr2021Event event = radio.poll(chips, sizeof(chips));
    REQUIRE(event.type == Lr2021EventType::Frame);
    CHECK(event.rssi_valid);
    CHECK(event.rssi_dbm == -61);
    CHECK(address_in(chips) == kAddress);
    CHECK_FALSE(radio.irq_asserted());
    CHECK(radio.poll(chips, sizeof(chips)).type == Lr2021EventType::None);
}

TEST_CASE("lr2021: the next frame is heard without another SetRx") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    uint8_t chips[protocol::kAdsbChipBytes]{};
    for (const uint32_t address : {kAddress, kOtherAddress}) {
        const AdsbAir air = identification(address);
        REQUIRE(part.receive_air(air.chips, sizeof(air.chips)));
        REQUIRE(radio.poll(chips, sizeof(chips)).type == Lr2021EventType::Frame);
        CHECK(address_in(chips) == address);
    }
    CHECK(part.count(chip::kSetRx) == 1);
    CHECK(part.refused.empty());
}

TEST_CASE("lr2021: two frames read in one wake come out oldest first, the RSSI on the newest") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    const AdsbAir first = identification(kAddress);
    const AdsbAir second = identification(kOtherAddress);
    REQUIRE(part.receive_air(first.chips, sizeof(first.chips), -80));
    REQUIRE(part.receive_air(second.chips, sizeof(second.chips), -50));

    uint8_t chips[protocol::kAdsbChipBytes]{};
    const parts::Lr2021Event older = radio.poll(chips, sizeof(chips));
    REQUIRE(older.type == Lr2021EventType::Frame);
    CHECK_FALSE(older.rssi_valid);
    CHECK(address_in(chips) == kAddress);
    const parts::Lr2021Event newer = radio.poll(chips, sizeof(chips));
    REQUIRE(newer.type == Lr2021EventType::Frame);
    CHECK(newer.rssi_valid);
    CHECK(newer.rssi_dbm == -50);
    CHECK(address_in(chips) == kOtherAddress);
    CHECK(radio.poll(chips, sizeof(chips)).type == Lr2021EventType::None);
}

TEST_CASE("lr2021: chips with no preamble in them are not a frame") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    AdsbAir noise = identification(kAddress);
    noise.chips[0] = 0x00;
    CHECK_FALSE(part.receive_air(noise.chips, sizeof(noise.chips)));
    uint8_t chips[protocol::kAdsbChipBytes]{};
    CHECK(radio.poll(chips, sizeof(chips)).type == Lr2021EventType::None);
}

// The frame behind it was drained in an earlier read, so an empty FIFO proves no loss.
TEST_CASE("lr2021: an RxDone with nothing in the FIFO is no frame, no drop and no clear") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    part.irq |= chip::kIrqRxDone;
    uint8_t chips[protocol::kAdsbChipBytes]{};
    CHECK(radio.poll(chips, sizeof(chips)).type == Lr2021EventType::None);
    CHECK(radio.fifo_drops() == 0);
    CHECK(part.count(chip::kClearRxFifo) == 0);
}

TEST_CASE("lr2021: a silent receiver is re-armed through standby at the silence ceiling") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    CHECK_FALSE(radio.service(29999, 30000));
    CHECK(radio.service(1, 30000));
    CHECK(radio.rearms() == 1);
    CHECK(radio.resets() == 0);
    CHECK(part.count(chip::kSetRx) == 2);
    CHECK(part.last_order(chip::kSetStandby) < part.last_order(chip::kSetRx));
    CHECK(part.refused.empty());
    CHECK(radio.mode() == Lr2021Mode::Receive);

    const AdsbAir air = identification(kAddress);
    REQUIRE(part.receive_air(air.chips, sizeof(air.chips)));
    uint8_t chips[protocol::kAdsbChipBytes]{};
    CHECK(radio.poll(chips, sizeof(chips)).type == Lr2021EventType::Frame);
}

TEST_CASE("lr2021: a frame starts the silence count again") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    CHECK_FALSE(radio.service(20000, 30000));
    const AdsbAir air = identification(kAddress);
    REQUIRE(part.receive_air(air.chips, sizeof(air.chips)));
    uint8_t chips[protocol::kAdsbChipBytes]{};
    REQUIRE(radio.poll(chips, sizeof(chips)).type == Lr2021EventType::Frame);
    CHECK_FALSE(radio.service(20000, 30000));
    CHECK(radio.rearms() == 0);
}

TEST_CASE("lr2021: BUSY stuck in a read is a fault, and the next service resets the part") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    const AdsbAir air = identification(kAddress);
    REQUIRE(part.receive_air(air.chips, sizeof(air.chips)));
    part.busy_stuck = true;
    const uint64_t stuck_at_us = part.elapsed_us;
    uint8_t chips[protocol::kAdsbChipBytes]{};
    CHECK(radio.poll(chips, sizeof(chips)).type == Lr2021EventType::Fault);
    CHECK(part.elapsed_us - stuck_at_us >= chip::kCommandBusyCeilingUs);

    part.busy_stuck = false;
    CHECK(radio.service(250, 30000));
    CHECK(radio.resets() == 1);
    CHECK(part.reset_pulses == 2);
    CHECK(radio.mode() == Lr2021Mode::Receive);
    CHECK(part.listening_for_adsb());
}

TEST_CASE("lr2021: a command the part refuses is counted, not ignored") {
    models::Lr2021 part;
    part.fail_opcodes = {chip::kSetAgcGainManual};
    Lr2021 radio = lr2021_for(part);
    REQUIRE(radio.begin(chip::kDetectionThresholdDbm) == Status::Ok);
    CHECK(radio.command_errors() == 1);
}

TEST_CASE("lr2021: a TCXO that never starts refuses the bring-up, a calibration error is kept") {
    models::Lr2021 clockless;
    clockless.calibration_errors = chip::kErrorHfXoscStart;
    Lr2021 radio = lr2021_for(clockless);
    CHECK(radio.begin(chip::kDetectionThresholdDbm) == Status::Down);
    CHECK(radio.start_receive() == Status::Invalid);

    // 1090 MHz is past the LF path's 960 MHz, so an image calibration error is expected and logged.
    models::Lr2021 out_of_band;
    out_of_band.calibration_errors = 0x0080;
    Lr2021 receiving = lr2021_for(out_of_band);
    CHECK(receiving.begin(chip::kDetectionThresholdDbm) == Status::Ok);
    CHECK(receiving.device_errors() == 0x0080);
}

TEST_CASE("lr2021: errors left from boot are cleared before the TCXO is set") {
    models::Lr2021 part;
    part.boot_errors = chip::kErrorHfXoscStart;
    Lr2021 radio = lr2021_for(part);
    CHECK(radio.begin(chip::kDetectionThresholdDbm) == Status::Ok);
    CHECK(part.command_order(chip::kClearErrors) < part.command_order(chip::kSetTcxoMode));
    CHECK(radio.device_errors() == 0);
}

TEST_CASE("lr2021: sleep goes through standby to a cold sleep, and nothing follows it") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    CHECK(radio.sleep() == Status::Ok);
    CHECK(radio.mode() == Lr2021Mode::Sleep);
    CHECK(part.mode == models::Lr2021::Mode::Sleep);
    CHECK(part.sleep_config == 0x00);
    CHECK(part.last_order(chip::kSetStandby) < part.last_order(chip::kSetSleep));
    const size_t seen = part.commands_seen.size();
    CHECK_FALSE(radio.service(60000, 30000));
    CHECK(radio.sleep() == Status::Ok);
    CHECK(part.commands_seen.size() == seen);
    CHECK(part.fault == models::Lr2021::Fault::None);
}
