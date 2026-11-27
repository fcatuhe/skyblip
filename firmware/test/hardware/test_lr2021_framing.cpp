// The receive FIFO is a byte stream, so the driver holds every byte to a frame the part counted.
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
namespace chip = skyblip::parts::lr2021;

namespace {

constexpr uint32_t kAddress = 0x4840D6;

uint32_t address_in(const uint8_t* frame) {
    adsb::Message message{};
    if (adsb::receive(frame, adsb::kFrameBytes, adsb::Reception{0, -70}, message) !=
        wire::Status::Ok)
        return 0;
    return message.address;
}

void air(models::Lr2021& part, uint32_t address) {
    const AdsbAir frame = adsb_air(adsb::squitter(address, 5, 4));
    REQUIRE(part.receive_air(frame.chips, sizeof(frame.chips)));
}

}  // namespace

// The latch bravo-d582 showed after a flood: one stray byte shifted every later frame.
TEST_CASE("lr2021: bytes no counted frame owns empty the FIFO, and the next frame reads whole" *
          doctest::test_suite("main")) {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    part.strand(5);
    air(part, kAddress);
    uint8_t frame[adsb::kFrameBytes]{};

    CHECK(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::None);
    CHECK(radio.framing_resyncs() == 1);
    CHECK(radio.resync_drops() == 1);
    CHECK(radio.fifo_drops() == 1);
    CHECK(radio.stranded_resyncs() == 1);
    CHECK(radio.short_resyncs() == 0);
    CHECK(part.fifo.empty());
    CHECK(part.listening_for_adsb());

    air(part, kAddress);
    REQUIRE(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Frame);
    CHECK(address_in(frame) == kAddress);
}

TEST_CASE("lr2021: a frame still arriving behind the counted ones is left to finish") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    air(part, kAddress);
    part.strand(5);
    part.arrive_after(chip::kGetRxFifoLevel, [&] { part.strand(1); });
    uint8_t frame[adsb::kFrameBytes]{};

    REQUIRE(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Frame);
    CHECK(address_in(frame) == kAddress);
    CHECK(radio.framing_resyncs() == 0);
    CHECK(radio.in_flight() == 1);
    CHECK(part.fifo.size() == 6);
}

TEST_CASE("lr2021: a FIFO short of the frames the part counted is emptied, not read past") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    part.ook_packets++;
    part.irq |= chip::kIrqRxDone;
    uint8_t frame[adsb::kFrameBytes]{};

    CHECK(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::None);
    CHECK(radio.framing_resyncs() == 1);
    CHECK(radio.short_resyncs() == 1);
    CHECK(part.listening_for_adsb());
}

TEST_CASE(
    "lr2021: bytes past the counted frames that neither grow nor close are stranded remains") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    air(part, kAddress);
    part.strand(5);
    uint8_t frame[adsb::kFrameBytes]{};

    CHECK(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::None);
    CHECK(radio.framing_resyncs() == 1);
    CHECK(radio.stranded_resyncs() == 1);
    CHECK(radio.in_flight() == 0);
    CHECK(radio.resync_drops() == 1);
}

TEST_CASE("lr2021: a frame that closes while the level is read is counted as a split snapshot") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    air(part, kAddress);
    part.arrive_after(chip::kGetRxFifoLevel, [&] { air(part, 0x3C6586); });
    uint8_t frame[adsb::kFrameBytes]{};

    REQUIRE(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Frame);
    CHECK(address_in(frame) == kAddress);
    CHECK(radio.split_counts() == 1);
    CHECK(radio.framing_resyncs() == 0);
    REQUIRE(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Frame);
    CHECK(address_in(frame) == 0x3C6586);
}

TEST_CASE("lr2021: whole frames with nothing stray are read with no resync and no wait") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    air(part, kAddress);
    air(part, 0x3C6586);
    const uint64_t before_us = part.elapsed_us;
    uint8_t frame[adsb::kFrameBytes]{};

    REQUIRE(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Frame);
    REQUIRE(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Frame);
    CHECK(address_in(frame) == 0x3C6586);
    CHECK(radio.framing_resyncs() == 0);
    CHECK(part.elapsed_us - before_us < chip::kInFlightGrowthUs);
}

TEST_CASE("lr2021: a frame's worth of bytes no counted frame owns is neither stranded nor short") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    air(part, kAddress);
    part.strand(adsb::kFrameBytes + 2);
    uint8_t frame[adsb::kFrameBytes]{};

    CHECK(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::None);
    CHECK(radio.framing_resyncs() == 1);
    CHECK(radio.stranded_resyncs() == 0);
    CHECK(radio.short_resyncs() == 0);
}

TEST_CASE("lr2021: an overflow counts every frame the FIFO held, apart from resyncs") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    for (int i = 0; i < 19; i++) air(part, kAddress);
    uint8_t frame[adsb::kFrameBytes]{};

    CHECK(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::None);
    CHECK(radio.overflow_drops() == 19);
    CHECK(radio.framing_resyncs() == 0);
    CHECK(part.listening_for_adsb());
}
