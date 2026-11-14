// What a retune sends the LR2021, and the frames its Manchester layout hands up.
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

parts::Lr2021Tuning cb_tfc() {
    parts::Lr2021Tuning tuning{};
    tuning.detection_threshold_dbm = -98;
    tuning.gain_step = 13;
    tuning.boost = 7;
    tuning.front_end_raw = false;
    tuning.layout = parts::Lr2021Layout::Manchester;
    return tuning;
}

uint32_t address_in(const uint8_t* chips) {
    protocol::AdsbMessage message{};
    if (protocol::adsb_receive(chips, protocol::kAdsbChipBytes, protocol::AdsbReception{0, -70},
                               message) != Status::Ok)
        return 0;
    return message.address;
}

}  // namespace

TEST_CASE("lr2021 trial: a retune while receiving sends every setting and keeps receiving") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    REQUIRE(radio.retune(cb_tfc()) == Status::Ok);
    CHECK(radio.mode() == Lr2021Mode::Receive);
    CHECK(part.count(chip::kSetRx) == 2);
    CHECK(part.agc_gain == 13);
    CHECK(part.rx_boost == 7);
    CHECK(part.reg(0xF30150) == 0x75);
    CHECK(part.reg(0xF30E14) == static_cast<uint32_t>(((-98 + 74) & 0x7F) << 20));
    CHECK(part.payload_bytes == 14);
    CHECK(part.manchester());
    CHECK(part.sync_bits == 0);
    CHECK(part.listening_for_adsb());
    CHECK(part.refused.empty());
}

TEST_CASE("lr2021 trial: the Manchester layout hands up the chips the decoder reads") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    REQUIRE(radio.retune(cb_tfc()) == Status::Ok);
    const AdsbAir air = adsb_air(adsb_frame(kAddress, 5, 4));
    REQUIRE(part.receive_air(air.chips, sizeof(air.chips), -64));

    uint8_t chips[protocol::kAdsbChipBytes]{};
    const parts::Lr2021Event event = radio.poll(chips, sizeof(chips));
    REQUIRE(event.type == Lr2021EventType::Frame);
    CHECK(event.rssi_dbm == -64);
    CHECK(address_in(chips) == kAddress);
}

// A Manchester capture has no sync word, so DF11 replies land too, and only DF16 and DF17 go up.
TEST_CASE("lr2021 trial: a frame of another downlink format is counted and not handed up") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    REQUIRE(radio.retune(cb_tfc()) == Status::Ok);
    AdsbAir reply = adsb_air(adsb_frame(kAddress, 5, 4));
    // The chips of the bits 0101, so the frame opens on DF 01011, an all-call reply.
    reply.chips[2] = 0x66;
    const AdsbAir squitter = adsb_air(adsb_frame(kAddress, 5, 4));
    REQUIRE(part.receive_air(reply.chips, sizeof(reply.chips)));
    REQUIRE(part.receive_air(squitter.chips, sizeof(squitter.chips)));

    uint8_t chips[protocol::kAdsbChipBytes]{};
    REQUIRE(radio.poll(chips, sizeof(chips)).type == Lr2021EventType::Frame);
    CHECK(address_in(chips) == kAddress);
    CHECK(radio.foreign_frames() == 1);
    CHECK(radio.poll(chips, sizeof(chips)).type == Lr2021EventType::None);
}

TEST_CASE("lr2021 trial: back on the default tuning, the 0x95 sync and SoftRF's writes return") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    REQUIRE(radio.retune(cb_tfc()) == Status::Ok);
    REQUIRE(radio.retune(parts::Lr2021Tuning{}) == Status::Ok);
    CHECK(part.payload_bytes == chip::kChipBytes);
    CHECK_FALSE(part.manchester());
    CHECK(part.sync_bits == 8);
    CHECK(part.agc_gain == chip::kAgcAutomatic);
    CHECK(part.rx_boost == chip::kRxBoostOff);
    CHECK(part.reg(0xF30150) == 0x18);
    CHECK(part.reg(0xF30E14) == 0x07000000);
}

TEST_CASE("lr2021 trial: a reset after a retune brings the part up on the tuning it was given") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    REQUIRE(radio.retune(cb_tfc()) == Status::Ok);
    part.busy_stuck = true;
    part.busy_stuck_until_reset = true;
    CHECK(radio.retune(cb_tfc()) == Status::Timeout);
    CHECK(radio.service(1, 30000));
    CHECK(radio.mode() == Lr2021Mode::Receive);
    CHECK(part.agc_gain == 13);
    CHECK(part.manchester());
}
