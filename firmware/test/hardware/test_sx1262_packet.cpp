// What goes on air and what comes off it: the buffer, the sync window the part inserts, the frame.
#include <initializer_list>

#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "test/support/sx1262_rig.h"

using namespace skyblip;
using namespace skyblip::parts;

// EN 300 220-2 V3.3.1 §4.6.3.2 wants the assessment averaged over an interval,
// and this part has nothing to average it with: GetRssiInst is an instant by
// definition (DS 13.5.2) and channel activity detection answers for a LoRa
// preamble, not a GFSK carrier. So the interval is a run of reads, and one read
// has to be able to differ from the next.
TEST_CASE("sx1262: what was written to the buffer is what goes on air") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    const uint8_t frame[5] = {0x72, 0x4B, 0x18, 0xAA, 0x55};
    REQUIRE(radio.transmit(frame, sizeof(frame)) == Status::Ok);
    CHECK_FALSE(chip.receiving);
    uint8_t out[8] = {0};
    uint8_t bytes = 0;
    REQUIRE(chip.take_tx(out, bytes));
    CHECK(bytes == 2 + sizeof(frame));
    CHECK(out[0] == 0x55);
    CHECK(out[1] == 0x55);
    CHECK(std::memcmp(out + 2, frame, sizeof(frame)) == 0);
    CHECK_FALSE(chip.take_tx(out, bytes));
}

TEST_CASE("sx1262: what goes on air is the preamble, sync window and buffer" *
          doctest::test_suite("main")) {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.sync = protocol::kSharedSync;
    config.sync_bits = protocol::kSharedSyncBits;
    config.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(radio.configure_radio(config) == Status::Ok);

    uint8_t payload[adsl::kFrameBytes];
    for (uint8_t i = 0; i < sizeof(payload); i++) payload[i] = static_cast<uint8_t>(i + 1);
    uint8_t buffer[protocol::kTxPayloadChipBytes] = {0};
    const size_t buffer_bytes =
        protocol::m_band_payload(adsl::kSyncWord, payload, sizeof(payload), buffer);
    REQUIRE(radio.transmit(buffer, static_cast<uint8_t>(buffer_bytes)) == Status::Ok);

    uint8_t on_air[protocol::kTxChipBytes] = {0};
    uint8_t bytes = 0;
    REQUIRE(chip.take_tx(on_air, bytes));
    uint8_t expected[protocol::kTxChipBytes] = {0};
    const size_t expected_bytes =
        wire::manchester_frame(adsl::kSyncWord, payload, sizeof(payload), expected);
    CHECK(bytes == 2 + expected_bytes);
    CHECK(std::memcmp(on_air + 2, expected, expected_bytes) == 0);
}

// The dwell hands the radio a sync window and a length. Without them programmed
// the chip frames nothing, so this is where the shared-window trick either
// reaches the hardware or quietly does not.
TEST_CASE("sx1262: configure programs the sync window and the fixed read length") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.sync = protocol::kSharedSync;
    config.sync_bits = protocol::kSharedSyncBits;
    config.payload_bytes = protocol::kRxChipBytes;
    CHECK(radio.configure_radio(config) == Status::Ok);
    CHECK(chip.saw_command(sx1262::kWriteRegister));
    CHECK(chip.saw_command(sx1262::kSetPacketParameters));
    CHECK(chip.sync[0] == protocol::kSharedSync[0]);
    CHECK(chip.sync[1] == protocol::kSharedSync[1]);
    CHECK(chip.sync_bits == protocol::kSharedSyncBits);
    CHECK(chip.payload_bytes == protocol::kRxChipBytes);
}

// What dates an event: free to read, where the read-out behind it costs milliseconds.
TEST_CASE("sx1262: the interrupt line is readable without a word on the bus") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);
    CHECK_FALSE(radio.irq_asserted());

    const uint8_t packet[4] = {1, 2, 3, 4};
    chip.queue_rx(packet, sizeof(packet));
    const size_t said_before = chip.commands_seen.size();
    CHECK(radio.irq_asserted());
    CHECK(chip.commands_seen.size() == said_before);

    uint8_t buffer[32];
    CHECK(radio.poll(buffer, sizeof(buffer)).type == RadioEventType::RxDone);
    CHECK_FALSE(radio.irq_asserted());
}

// The silicon executor reads the status only once DIO1 is up, so a report the line misses waits.
TEST_CASE("sx1262: every event a poll can report raises the interrupt line first") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);
    uint8_t buffer[32];
    const uint8_t frame[4] = {1, 2, 3, 4};
    auto raised = [&](RadioEventType type) {
        const bool up = radio.irq_asserted();
        return up && radio.poll(buffer, sizeof(buffer)).type == type;
    };

    chip.queue_rx(frame, sizeof(frame));
    CHECK(raised(RadioEventType::RxDone));
    chip.queue_rx(frame, sizeof(frame), true);
    CHECK(raised(RadioEventType::CrcError));
    REQUIRE(radio.transmit(frame, sizeof(frame)) == Status::Ok);
    chip.signal_tx_done();
    CHECK(raised(RadioEventType::TxDone));
    REQUIRE(radio.transmit(frame, sizeof(frame)) == Status::Ok);
    REQUIRE(chip.expire_tx());
    CHECK(raised(RadioEventType::Timeout));
}

// The silicon executor reads the status every 60 us from here on: too long dates TxDone late.
TEST_CASE("sx1262: a burst's air time is its preamble, sync window and payload at the chip rate") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.sync = protocol::kSharedSync;
    config.sync_bits = protocol::kSharedSyncBits;
    config.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(radio.configure_radio(config) == Status::Ok);

    const uint8_t adsl_burst = protocol::kSyncTailChipBytes + 2 * adsl::kFrameBytes;
    // 16 preamble + 16 sync + 54 x 8 payload chips at 100 kchip/s: transmit.h's 4.64 ms.
    CHECK(radio.air_us(adsl_burst) == 4640);
    config.bitrate = 50000;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    CHECK(radio.air_us(adsl_burst) == 9280);
}

// DS table 13-70 spells CRC off 0x01, and 0x00, which every other radio means it with, a CRC byte.
TEST_CASE("sx1262: the packet the modem is told to expect carries no CRC of the chip's own") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.sync = protocol::kSharedSync;
    config.sync_bits = protocol::kSharedSyncBits;
    config.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(radio.configure_radio(config) == Status::Ok);

    const uint8_t expected[9] = {static_cast<uint8_t>(sx1262::kPreambleChips >> 8),
                                 static_cast<uint8_t>(sx1262::kPreambleChips),
                                 sx1262::kPreambleDetect8Chips,
                                 protocol::kSharedSyncBits,
                                 sx1262::kAddressFilteringOff,
                                 sx1262::kFixedLength,
                                 protocol::kRxChipBytes,
                                 sx1262::kCrcOff,
                                 sx1262::kWhiteningOff};
    for (size_t i = 0; i < sizeof(expected); i++) {
        CAPTURE(i);
        CHECK(chip.packet_parameters[i] == expected[i]);
    }
    CHECK(sx1262::kCrcOff == 0x01);
    CHECK(sx1262::kCrcOneByte == 0x00);
}

TEST_CASE("sx1262: a burst is framed from the chips after the sync window, either system" *
          doctest::test_suite("main")) {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.sync = protocol::kSharedSync;
    config.sync_bits = protocol::kSharedSyncBits;
    config.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);

    for (uint32_t sync_word : {adsl::kSyncWord, alptas::kSyncWord}) {
        uint8_t payload[alptas::kFrameBytes];
        for (uint8_t i = 0; i < sizeof(payload); i++) payload[i] = static_cast<uint8_t>(i + 1);
        uint8_t chips[protocol::kTxChipBytes] = {0};
        const size_t chip_bytes =
            wire::manchester_frame(sync_word, payload, sizeof(payload), chips);
        REQUIRE(chip.receive_air(chips, static_cast<uint8_t>(chip_bytes)));

        uint8_t buffer[64];
        RadioEvent event = radio.poll(buffer, sizeof(buffer));
        REQUIRE(event.type == RadioEventType::RxDone);
        protocol::Frame frame{};
        REQUIRE(protocol::receive_m_band(buffer, event.bytes, frame));
        const bool adsl = sync_word == adsl::kSyncWord;
        if (adsl) CHECK(frame.system == protocol::System::AdslDirect);
        if (!adsl) CHECK(frame.system == protocol::System::Alptas);
        CHECK(frame.data[0] == 1);
        REQUIRE(radio.start_receive() == Status::Ok);
    }
}

TEST_CASE("sx1262: a burst carrying a sync word the dwell is not armed for is not reported") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.sync = adsl::kUplinkSync;
    config.sync_bits = adsl::kUplinkSyncBits;
    config.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);

    uint8_t payload[adsl::kFrameBytes] = {0};
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_bytes =
        wire::manchester_frame(adsl::kSyncWord, payload, sizeof(payload), chips);
    CHECK_FALSE(chip.receive_air(chips, static_cast<uint8_t>(chip_bytes)));
    uint8_t buffer[64];
    CHECK(radio.poll(buffer, sizeof(buffer)).type == RadioEventType::None);
}

TEST_CASE("sx1262: the shorter receive sync and receive length return after transmission") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    RadioConfig config{};
    config.sync = protocol::kSharedSync;
    config.sync_bits = protocol::kSharedSyncBits;
    config.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    const uint8_t tx_sync[]{0x55, 0x55, 0x99, 0x95, 0xA6, 0x9A, 0x65};
    config.tx_sync = tx_sync;
    config.tx_sync_bits = sizeof(tx_sync) * 8;
    config.tx_preamble_chips = 8;
    REQUIRE(radio.configure_radio(config) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);

    const uint8_t burst[2 + 2 * adsl::kFrameBytes] = {0xAA};
    REQUIRE(radio.transmit(burst, sizeof(burst)) == Status::Ok);
    uint8_t on_air[64] = {0};
    uint8_t bytes = 0;
    REQUIRE(chip.take_tx(on_air, bytes));
    CHECK(bytes == 58);
    CHECK(radio.air_us(sizeof(burst)) == 4640);

    chip.signal_tx_done();
    REQUIRE(radio.poll(on_air, sizeof(on_air)).type == RadioEventType::TxDone);
    REQUIRE(radio.start_receive() == Status::Ok);
    CHECK(chip.receiving);
    CHECK(chip.sync_bits == 16);
    CHECK(chip.sync[0] == 0x55);
    CHECK(chip.sync[1] == 0x99);
    CHECK(chip.payload_bytes == protocol::kRxChipBytes);
    const uint8_t short_preamble_peer[]{0x01, 0x55, 0x99, 0x95, 0xA6, 0x9A, 0x65};
    CHECK(chip.receive_air(short_preamble_peer, sizeof(short_preamble_peer)));
    CHECK(chip.faults == 0);
}
