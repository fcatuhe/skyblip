// What the SX1262 driver does when the SPI bus reports a transfer that did not happen.
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "test/support/sx1262_rig.h"

using namespace skyblip;
using namespace skyblip::parts;

namespace {

void receive(Sx1262& radio) {
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    REQUIRE(radio.start_receive() == Status::Ok);
}

}  // namespace

TEST_CASE("sx1262: a failed status read is a fault, not the flags a dead bus left behind") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    receive(radio);
    const uint8_t packet[4] = {1, 2, 3, 4};
    chip.queue_rx(packet, sizeof(packet));
    chip.transfers_before_failure = 0;

    uint8_t buffer[32] = {};
    CHECK(radio.poll(buffer, sizeof(buffer)).type == RadioEventType::Fault);
    CHECK(radio.mode() == RadioMode::Rx);
}

TEST_CASE("sx1262: a frame whose payload read fails is a fault, not a frame") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    receive(radio);
    const uint8_t packet[4] = {1, 2, 3, 4};
    chip.queue_rx(packet, sizeof(packet));
    // GetIrqStatus, ClearIrqStatus, GetRxBufferStatus and the ReadBuffer header pass.
    chip.transfers_before_failure = 4;

    uint8_t buffer[32] = {};
    const RadioEvent event = radio.poll(buffer, sizeof(buffer));
    CHECK(event.type == RadioEventType::Fault);
    CHECK(event.bytes == 0);
}

TEST_CASE("sx1262: a burst that could not be staged is refused, and keys nothing after it") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    receive(radio);
    chip.transfers_before_failure = 0;
    const uint8_t frame[4] = {1, 2, 3, 4};
    CHECK(radio.stage_tx(frame, sizeof(frame)) == Status::Down);

    chip.transfers_before_failure = -1;
    CHECK(radio.key_tx() == Status::Invalid);
    CHECK_FALSE(chip.saw_command(sx1262::kSetTx));
    CHECK(radio.mode() != RadioMode::Tx);
}

TEST_CASE("sx1262: a channel level that could not be read is no level at all") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    receive(radio);
    chip.rssi_dbm = -110;
    REQUIRE(radio.instant_rssi_dbm().value() == -110);

    chip.transfers_before_failure = 0;
    CHECK(radio.instant_rssi_dbm().status() == Status::Down);
}

TEST_CASE("sx1262: a failed transfer owes a reinitialisation, which the no-RX rope pays") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    REQUIRE(radio.begin() == Status::Ok);
    REQUIRE(radio.configure_radio(RadioConfig{}) == Status::Ok);
    chip.transfers_before_failure = 0;
    CHECK(radio.start_receive() == Status::Down);
    CHECK(radio.mode() == RadioMode::Standby);

    chip.transfers_before_failure = -1;
    CHECK_FALSE(radio.service(29000, 30000));
    CHECK(radio.service(2000, 30000));
    CHECK(radio.reinitialisations() == 1);
    CHECK(radio.mode() == RadioMode::Rx);
}

TEST_CASE("sx1262: a bring-up whose transfers fail is refused") {
    models::Sx1262 chip;
    Sx1262 radio = driver_for(chip);
    SUBCASE("at the link check") { chip.transfers_before_failure = 0; }
    SUBCASE("after the link check, at the regulator") { chip.transfers_before_failure = 3; }
    CHECK(radio.begin() == Status::Down);
    CHECK(radio.configure_radio(RadioConfig{}) == Status::Down);
}
