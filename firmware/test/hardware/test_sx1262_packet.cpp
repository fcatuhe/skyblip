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
TEST_CASE("radio: what was written to the buffer is what goes on air") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    const uint8_t frame[5] = {0x72, 0x4B, 0x18, 0xAA, 0x55};
    REQUIRE(r.transmit(frame, sizeof(frame)) == Status::Ok);
    CHECK_FALSE(chip.receiving);
    uint8_t out[8] = {0};
    uint8_t len = 0;
    REQUIRE(chip.take_tx(out, len));
    CHECK(len == sizeof(frame));
    CHECK(out[0] == 0x72);
    CHECK(out[4] == 0x55);
    CHECK_FALSE(chip.take_tx(out, len));
}

// What a peer's detector matches is the window the part inserts, not one the dwell wrote itself.
TEST_CASE("radio: what goes on air is the sync window the part inserts, then the buffer") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    RadioConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);

    uint8_t payload[protocol::kAdslFrameBytes];
    for (uint8_t i = 0; i < sizeof(payload); i++) payload[i] = static_cast<uint8_t>(i + 1);
    uint8_t buffer[protocol::kTxPayloadChipBytes] = {0};
    const size_t buffer_len =
        protocol::mband_payload(protocol::kAdslSyncWord, payload, sizeof(payload), buffer);
    REQUIRE(r.transmit(buffer, static_cast<uint8_t>(buffer_len)) == Status::Ok);

    uint8_t on_air[protocol::kTxChipBytes] = {0};
    uint8_t len = 0;
    REQUIRE(chip.take_tx(on_air, len));
    uint8_t expected[protocol::kTxChipBytes] = {0};
    const size_t expected_len =
        protocol::encode_mband(protocol::kAdslSyncWord, payload, sizeof(payload), expected);
    CHECK(len == expected_len);
    CHECK(std::memcmp(on_air, expected, expected_len) == 0);
}

// The dwell hands the radio a sync window and a length. Without them programmed
// the chip frames nothing, so this is where the shared-window trick either
// reaches the hardware or quietly does not.
TEST_CASE("radio: configure programs the sync window and the fixed read length") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    RadioConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    CHECK(r.configure_radio(cfg) == Status::Ok);
    CHECK(chip.saw_cmd(sx::kWriteRegister));
    CHECK(chip.saw_cmd(sx::kSetPacketParams));
    CHECK(chip.sync[0] == protocol::kSharedSync[0]);
    CHECK(chip.sync[1] == protocol::kSharedSync[1]);
    CHECK(chip.sync_bits == protocol::kSharedSyncBits);
    CHECK(chip.payload_bytes == protocol::kRxChipBytes);
}

// What dates an event: free to read, where the read-out behind it costs milliseconds.
TEST_CASE("radio: the interrupt line is readable without a word on the bus") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    CHECK_FALSE(r.irq_asserted());

    const uint8_t pkt[4] = {1, 2, 3, 4};
    chip.queue_rx(pkt, sizeof(pkt));
    const size_t said_before = chip.cmds_seen.size();
    CHECK(r.irq_asserted());
    CHECK(chip.cmds_seen.size() == said_before);

    uint8_t buf[32];
    CHECK(r.poll(buf, sizeof(buf)).type == RadioEventType::RxDone);
    CHECK_FALSE(r.irq_asserted());
}

// The silicon executor reads the status only once DIO1 is up, so a report the line misses waits.
TEST_CASE("radio: every event a poll can report raises the interrupt line first") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    REQUIRE(r.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);
    uint8_t buf[32];
    const uint8_t frame[4] = {1, 2, 3, 4};
    auto raised = [&](RadioEventType type) {
        const bool up = r.irq_asserted();
        return up && r.poll(buf, sizeof(buf)).type == type;
    };

    chip.queue_rx(frame, sizeof(frame));
    CHECK(raised(RadioEventType::RxDone));
    chip.queue_rx(frame, sizeof(frame), true);
    CHECK(raised(RadioEventType::CrcError));
    REQUIRE(r.transmit(frame, sizeof(frame)) == Status::Ok);
    chip.signal_tx_done();
    CHECK(raised(RadioEventType::TxDone));
    REQUIRE(r.transmit(frame, sizeof(frame)) == Status::Ok);
    REQUIRE(chip.expire_tx());
    CHECK(raised(RadioEventType::Timeout));
}

// The silicon executor reads the status every 60 us from here on: too long dates TxDone late.
TEST_CASE("radio: a burst's air time is its preamble, sync window and payload at the chip rate") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    RadioConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);

    const uint8_t adsl_burst = protocol::kSyncTailChipBytes + 2 * protocol::kAdslFrameBytes;
    // 16 preamble + 16 sync + 54 x 8 payload chips at 100 kchip/s: transmit.h's 4.64 ms.
    CHECK(r.air_us(adsl_burst) == 4640);
    cfg.bitrate = 50000;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);
    CHECK(r.air_us(adsl_burst) == 9280);
}

// DS table 13-70 spells CRC off 0x01, and 0x00, which every other radio means it with, a CRC byte.
TEST_CASE("radio: the packet the modem is told to expect carries no CRC of the chip's own") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    RadioConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);

    const uint8_t expected[9] = {static_cast<uint8_t>(sx::kPreambleChips >> 8),
                                 static_cast<uint8_t>(sx::kPreambleChips),
                                 sx::kPreambleDetect8Chips,
                                 protocol::kSharedSyncBits,
                                 sx::kAddrCompOff,
                                 sx::kFixedLength,
                                 protocol::kRxChipBytes,
                                 sx::kCrcOff,
                                 sx::kWhiteningOff};
    for (size_t i = 0; i < sizeof(expected); i++) {
        CAPTURE(i);
        CHECK(chip.packet_params[i] == expected[i]);
    }
    CHECK(sx::kCrcOff == 0x01);
    CHECK(sx::kCrcOneByte == 0x00);
}

TEST_CASE("radio: a burst is framed from the chips after the sync window, either system") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    RadioConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);

    for (uint32_t sync_word : {protocol::kAdslSyncWord, protocol::kAlptasSyncWord}) {
        uint8_t payload[protocol::kAlptasFrameBytes];
        for (uint8_t i = 0; i < sizeof(payload); i++) payload[i] = static_cast<uint8_t>(i + 1);
        uint8_t chips[protocol::kTxChipBytes] = {0};
        const size_t chip_len = protocol::encode_mband(sync_word, payload, sizeof(payload), chips);
        REQUIRE(chip.receive_air(chips, static_cast<uint8_t>(chip_len)));

        uint8_t buf[64];
        RadioEvent ev = r.poll(buf, sizeof(buf));
        REQUIRE(ev.type == RadioEventType::RxDone);
        protocol::Frame frame{};
        REQUIRE(protocol::receive_mband(buf, ev.len, frame));
        const bool adsl = sync_word == protocol::kAdslSyncWord;
        if (adsl) CHECK(frame.system == protocol::System::AdslDirect);
        if (!adsl) CHECK(frame.system == protocol::System::Alptas);
        CHECK(frame.data[0] == 1);
        REQUIRE(r.start_receive() == Status::Ok);
    }
}

TEST_CASE("radio: a burst carrying a sync word the dwell is not armed for is not reported") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    RadioConfig cfg{};
    cfg.sync = protocol::kUplinkSync;
    cfg.sync_bits = protocol::kUplinkSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);

    uint8_t payload[protocol::kAdslFrameBytes] = {0};
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_len =
        protocol::encode_mband(protocol::kAdslSyncWord, payload, sizeof(payload), chips);
    CHECK_FALSE(chip.receive_air(chips, static_cast<uint8_t>(chip_len)));
    uint8_t buf[64];
    CHECK(r.poll(buf, sizeof(buf)).type == RadioEventType::None);
}

// Bench, 28sep26: fixed length was the receive length, so every burst carried four stale bytes
// and 0.32 ms of air it did not need.
TEST_CASE("radio: a burst goes on air at its own length, and the receiver reads its own again") {
    models::Sx1262 chip;
    Sx1262 r = make(chip);
    REQUIRE(r.begin() == Status::Ok);
    RadioConfig cfg{};
    cfg.sync = protocol::kSharedSync;
    cfg.sync_bits = protocol::kSharedSyncBits;
    cfg.payload_bytes = protocol::kRxChipBytes;
    REQUIRE(r.configure_radio(cfg) == Status::Ok);
    REQUIRE(r.start_receive() == Status::Ok);

    const uint8_t burst[protocol::kSyncTailChipBytes + 2 * protocol::kAdslFrameBytes] = {0x55};
    REQUIRE(r.transmit(burst, sizeof(burst)) == Status::Ok);
    uint8_t on_air[64] = {0};
    uint8_t len = 0;
    REQUIRE(chip.take_tx(on_air, len));
    CHECK(len == protocol::kSyncWindowChipBytes + sizeof(burst));

    chip.signal_tx_done();
    REQUIRE(r.start_receive() == Status::Ok);
    CHECK(chip.receiving);
    CHECK(chip.payload_bytes == protocol::kRxChipBytes);
    CHECK(chip.faults == 0);
}
