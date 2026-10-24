// How the radio is tuned, and what it can then hear: the modulation programmed
// for each of the two bands we dwell on, what the PLL word says the radio is
// tuned to, what a carrier-sense assessment reads off that channel, and the
// level a delivered packet arrived with. Split out of test_sx1262.cpp to keep
// both files under 500 lines; the driver and its model are exercised through
// the same public surface either way.
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "products/skyblip_go/settings.h"
#include "test/support/sx1262_rig.h"

using namespace skyblip;
using namespace skyblip::parts;

TEST_CASE("sx1262: the tuned channel is what the PLL word resolves back to") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    // Both ADS-L M-band channels, 200 kHz apart (SRD-860 issue 2 C.2).
    RadioConfig config{};
    config.frequency_hz = 868200000;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK(chip.frequency_hz > 868199000);
    CHECK(chip.frequency_hz < 868201000);
    config.frequency_hz = 868400000;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK(chip.frequency_hz > 868399000);
    CHECK(chip.frequency_hz < 868401000);
}

TEST_CASE("sx1262: carrier sense reads the level on the tuned channel") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);
    chip.rssi_dbm = -110;
    CHECK(radio.instant_rssi_dbm() == -110);
    chip.rssi_dbm = -48;
    CHECK(radio.instant_rssi_dbm() == -48);
}

TEST_CASE("sx1262: an assessment interval is a run of reads, each answering for its own instant") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);

    const int8_t levels[4] = {-112, -112, -44, -112};
    chip.set_rssi_sequence(levels, 4);
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < 4; i++) {
            CAPTURE(pass);
            CAPTURE(i);
            CHECK(radio.instant_rssi_dbm() == levels[i]);
        }

    // With no sequence loaded the chip answers one steady level, as before.
    chip.set_rssi_sequence(nullptr, 0);
    chip.rssi_dbm = -97;
    CHECK(radio.instant_rssi_dbm() == -97);
    CHECK(radio.instant_rssi_dbm() == -97);
}

TEST_CASE("sx1262: a delivered packet carries the level it arrived with") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);
    uint8_t packet[4] = {1, 2, 3, 4};
    chip.queue_rx(packet, 4, /*crc_error=*/false, /*rssi=*/-73);
    uint8_t buffer[8];
    const RadioEvent event = radio.poll(buffer, sizeof(buffer));
    CHECK(event.type == RadioEventType::RxDone);
    CHECK(event.rssi_dbm == -73);
}

TEST_CASE("sx1262: the modem is programmed for 100 kbps, 50 kHz deviation, 234.3 kHz, unshaped") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.sync = protocol::kSharedSync;
    config.sync_bits = protocol::kSharedSyncBits;
    config.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK(chip.modulation_set);
    CHECK(chip.bitrate == 100000);
    CHECK(chip.deviation_hz > 49990);
    CHECK(chip.deviation_hz < 50010);
    // DS 13.4.6 order: bit rate, pulse shape, RX bandwidth, deviation.
    CHECK(chip.modulation[3] == sx1262::kPulseShapeNone);
    CHECK(chip.modulation[4] == sx1262::kRxBandwidth234kHz);
    CHECK(chip.pulse_shape == sx1262::kPulseShapeNone);
    CHECK(chip.rx_bandwidth == sx1262::kRxBandwidth234kHz);
    // DS 13.1.4: the packet handler is configured after the modem, not before.
    CHECK(chip.command_order(sx1262::kSetRfFrequency) <
          chip.command_order(sx1262::kSetModulationParameters));
    CHECK(chip.command_order(sx1262::kSetModulationParameters) <
          chip.command_order(sx1262::kSetPacketParameters));
}

TEST_CASE("sx1262: the O band is programmed for 200 kbps GMSK in a 250 kHz channel") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.frequency_hz = 869525000;
    config.bitrate = protocol::kUplinkChipRateBps;
    config.deviation_hz = protocol::kUplinkDeviationHz;
    config.bandwidth_hz = protocol::kUplinkChannelBandwidthHz;
    config.gaussian_bt_e2 = protocol::kUplinkGaussianBtE2;
    config.sync = protocol::kUplinkSync;
    config.sync_bits = protocol::kUplinkSyncBits;
    config.payload_bytes = protocol::kUplinkFrameBytes;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK(chip.bitrate == 200000);
    CHECK(chip.deviation_hz > 49990);
    CHECK(chip.deviation_hz < 50010);
    CHECK(chip.pulse_shape == sx1262::kGaussianBt0p5);
    // The narrowest entry of DS 13.4.6's table that still passes 250 kHz. The
    // one below it, 234.3 kHz, clips the channel.
    CHECK(chip.rx_bandwidth == 0x19);
    CHECK(chip.payload_bytes == protocol::kUplinkFrameBytes);
    // A whole RS(255,223) codeword is what this band reads, and the packet
    // handler's length field is exactly wide enough for it.
    CHECK(chip.payload_bytes == 255);
}

TEST_CASE("sx1262: a modem left on its reset defaults frames nothing off the air") {
    models::Sx1262 chip;
    chip.sync_bits = protocol::kSharedSyncBits;
    chip.payload_bytes = protocol::kRxChipBytes;
    for (uint8_t i = 0; i < protocol::kSharedSyncBits / 8; i++)
        chip.sync[i] = protocol::kSharedSync[i];
    uint8_t payload[protocol::kAdslFrameBytes] = {0};
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_bytes =
        protocol::encode_m_band(protocol::kAdslSyncWord, payload, sizeof(payload), chips);
    CHECK_FALSE(chip.receive_air(chips, static_cast<uint8_t>(chip_bytes)));
}

// --- J. Frequency trim ------------------------------------------------------
// The board fits a TCXO, so zero is the expected value on every unit and the
// field exists for the batch that disappoints. What matters is that it is a
// no-op when it is zero and moves the carrier the right way by the right amount
// when it is not, because nothing on the device can measure the answer back.

TEST_CASE("sx1262: with no frequency trim the PLL word is exactly the channel asked for") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.frequency_hz = 868200000;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    const uint32_t untrimmed = chip.frequency_hz;

    config.frequency_correction_e1_ppm = 0;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK(chip.frequency_hz == untrimmed);
}

TEST_CASE("sx1262: a frequency trim moves the programmed carrier by its own ppm, both ways") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.frequency_hz = 868200000;

    // A tenth of a ppm is 86.82 Hz at 868.2 MHz, so ten ppm is 8682 Hz. The PLL
    // step is 32 MHz / 2^25, under a hertz, and the model resolves the word back
    // to the frequency it programmed: a couple of hertz of slack is the two
    // truncations, not the trim.
    config.frequency_correction_e1_ppm = 100;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK(chip.frequency_hz >= 868208680u);
    CHECK(chip.frequency_hz <= 868208682u);

    config.frequency_correction_e1_ppm = -100;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK(chip.frequency_hz >= 868191316u);
    CHECK(chip.frequency_hz <= 868191318u);

    // And it applies to the O band, on which we only ever listen: a reference
    // that is out is out on both bands, and a trim that only reached the band we
    // transmit on would leave the uplink dwell mistuned.
    config.frequency_hz = 869525000;
    config.frequency_correction_e1_ppm = 100;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK(chip.frequency_hz >= 869533693u);
    CHECK(chip.frequency_hz <= 869533695u);
}

TEST_CASE("sx1262: a trim past the band's own limit is clamped, not programmed") {
    // The two bounds are the same number in two layers: what the flash blob
    // accepts and what the PLL word is allowed to carry. If they ever part, a
    // stored value would be silently altered on its way to the synthesiser.
    CHECK(int(sx1262::kFrequencyTrimCeilingE1Ppm) == int(go::kFrequencyTrimCeilingE1Ppm));

    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.frequency_hz = 868200000;
    config.frequency_correction_e1_ppm = sx1262::kFrequencyTrimCeilingE1Ppm;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    const uint32_t at_limit = chip.frequency_hz;

    config.frequency_correction_e1_ppm = 32767;  // whatever wrote this, it was not a bench
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK(chip.frequency_hz == at_limit);
    // ERC 70-03 band h1.4 is 868.0-868.6 MHz and the clamp is what keeps a
    // trimmed carrier inside it whatever it was handed.
    CHECK(chip.frequency_hz < 868600000u);

    config.frequency_correction_e1_ppm = -32768;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK(chip.frequency_hz > 868000000u);
}
