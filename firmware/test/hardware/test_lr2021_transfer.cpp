// What the LR2021 driver does when the SPI bus reports a transfer that did not happen.
#include <cstdint>

#include "doctest/doctest.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "hardware/parts/lr2021/model.h"
#include "protocols/adsb/message.h"
#include "test/support/adsb_frames.h"
#include "test/support/lr2021_rig.h"

using namespace skyblip;
using parts::Lr2021;
using parts::Lr2021EventType;
using parts::Lr2021Mode;
namespace chip = skyblip::parts::lr2021;

namespace {

constexpr uint32_t kAddress = 0x4840D6;

}  // namespace

TEST_CASE("lr2021: a failed FIFO read is a fault, not fourteen bytes of a dead bus") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    const AdsbAir air = adsb_air(adsb::squitter(kAddress, 5, 4));
    REQUIRE(part.receive_air(air.chips, sizeof(air.chips)));
    // The snapshot is five reads of two transfers and a ClearIrq, so the FIFO read is the twelfth.
    part.transfers_before_failure = 11;
    const int clears = part.count(chip::kClearIrq);

    uint8_t frame[adsb::kFrameBytes]{};
    CHECK(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Fault);
    CHECK(part.count(chip::kClearIrq) == clears + 1);
    CHECK(radio.service(250, 30000));
    CHECK(radio.resets() == 1);
}

TEST_CASE("lr2021: a failed snapshot is a fault, and the next service resets the part") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    const AdsbAir air = adsb_air(adsb::squitter(kAddress, 5, 4));
    REQUIRE(part.receive_air(air.chips, sizeof(air.chips)));
    part.transfers_before_failure = 0;

    uint8_t frame[adsb::kFrameBytes]{};
    CHECK(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Fault);
    CHECK(radio.fifo_drops() == 0);

    part.transfers_before_failure = -1;
    CHECK(radio.service(250, 30000));
    CHECK(radio.resets() == 1);
    CHECK(radio.mode() == Lr2021Mode::Receive);
    CHECK(part.listening_for_adsb());
}

TEST_CASE("lr2021: a bring-up whose commands do not reach the part is refused") {
    models::Lr2021 part;
    // The version read passes, as two transfers, and SetStandby is the first to fail.
    part.transfers_before_failure = 2;
    Lr2021 radio = lr2021_for(part);
    CHECK(radio.begin() == Status::Down);
    CHECK(radio.mode() == Lr2021Mode::Reset);
    CHECK(radio.start_receive() == Status::Invalid);
}
