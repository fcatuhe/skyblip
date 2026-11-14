// Frames that land between the driver's SPI commands, a full FIFO, and a BUSY fault at shutdown.
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

constexpr uint32_t kFirst = 0x4840D6;
constexpr uint32_t kSecond = 0x3C6586;
constexpr uint32_t kThird = 0x40621D;

uint32_t address_in(const uint8_t* chips) {
    adsb::Message message{};
    if (adsb::receive(chips, adsb::kChipBytes, adsb::Reception{0, -70}, message) !=
        wire::Status::Ok)
        return 0;
    return message.address;
}

void hear(models::Lr2021& part, uint32_t address, int8_t rssi_dbm = -70) {
    const AdsbAir air = adsb_air(adsb_frame(address, 5, 4));
    REQUIRE(part.receive_air(air.chips, sizeof(air.chips), rssi_dbm));
}

struct Read {
    parts::Lr2021Event event;
    uint32_t address;
};

Read read(Lr2021& radio) {
    uint8_t chips[adsb::kChipBytes]{};
    const parts::Lr2021Event event = radio.poll(chips, sizeof(chips));
    return {event, event.type == Lr2021EventType::Frame ? address_in(chips) : 0};
}

}  // namespace

// Ten frames fill the 256-byte FIFO with nine and 13 bytes of the tenth, which would shift every
// later read.
TEST_CASE("lr2021: an overflowed FIFO is counted, cleared and re-armed before anything is read") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    for (int i = 0; i < 10; i++) hear(part, kFirst);
    REQUIRE((part.rx_fifo_flags & models::Lr2021::kFifoOverflow) != 0);

    CHECK(read(radio).event.type == Lr2021EventType::None);
    CHECK(radio.fifo_drops() == 10);
    CHECK(part.fifo.empty());
    CHECK(part.rx_fifo_flags == 0);
    CHECK(part.count(chip::kSetRx) == 2);
    CHECK(radio.mode() == Lr2021Mode::Receive);
    CHECK(part.refused.empty());

    hear(part, kSecond);
    const Read after = read(radio);
    REQUIRE(after.event.type == Lr2021EventType::Frame);
    CHECK(after.address == kSecond);
    CHECK(read(radio).event.type == Lr2021EventType::None);
}

// The RxDone of a frame already drained arrives on its own, with nothing behind it in the FIFO.
TEST_CASE("lr2021: a frame drained ahead of its own RxDone is no drop, and no clear") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    hear(part, kFirst);
    part.arrive_after(chip::kGetAndClearIrqStatus, [&] { hear(part, kSecond); });
    CHECK(read(radio).address == kFirst);
    CHECK(read(radio).address == kSecond);

    part.arrive_after(chip::kGetRxFifoLevel, [&] { hear(part, kThird); });
    CHECK(read(radio).event.type == Lr2021EventType::None);
    CHECK(read(radio).address == kThird);
    CHECK(read(radio).event.type == Lr2021EventType::None);
    CHECK(radio.fifo_drops() == 0);
    CHECK(part.count(chip::kClearRxFifo) == 0);
}

// The packet status names the last packet, which may have landed after the FIFO was measured.
TEST_CASE("lr2021: a level that may belong to a frame outside the snapshot is not given out") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    hear(part, kFirst, -80);
    part.arrive_after(chip::kGetRxFifoLevel, [&] { hear(part, kSecond, -50); });
    const Read first = read(radio);
    CHECK(first.address == kFirst);
    CHECK_FALSE(first.event.rssi_valid);
    const Read second = read(radio);
    CHECK(second.address == kSecond);
    CHECK(second.event.rssi_valid);
    CHECK(second.event.rssi_dbm == -50);
}

TEST_CASE("lr2021: a frame landing inside the snapshot leaves no frame with a borrowed level") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    hear(part, kFirst, -80);
    part.arrive_after(chip::kGetOokPacketStatus, [&] { hear(part, kSecond, -50); });
    const Read first = read(radio);
    const Read second = read(radio);
    CHECK(first.address == kFirst);
    CHECK(second.address == kSecond);
    CHECK_FALSE(first.event.rssi_valid);
    CHECK_FALSE(second.event.rssi_valid);
}

TEST_CASE("lr2021: a BUSY fault at shutdown is recovered by a reset before the sleep") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    part.busy_stuck = true;
    part.busy_stuck_until_reset = true;
    CHECK(radio.sleep() == Status::Ok);
    CHECK(part.reset_pulses == 2);
    CHECK(part.mode == models::Lr2021::Mode::Sleep);
    CHECK(radio.mode() == Lr2021Mode::Sleep);
}

TEST_CASE("lr2021: a part that cannot be put to sleep says so, within a bound") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    part.busy_stuck = true;
    const uint64_t from_us = part.elapsed_us;
    CHECK(radio.sleep() == Status::Timeout);
    CHECK(radio.mode() != Lr2021Mode::Sleep);
    CHECK(part.mode != models::Lr2021::Mode::Sleep);
    CHECK(part.elapsed_us - from_us <= chip::kCommandBusyCeilingUs + chip::kBootBusyCeilingUs +
                                           2 * chip::kBusySleepPollUs + chip::kSleepSettleUs);

    radio.hold_in_reset();
    CHECK(part.in_reset);
    CHECK_FALSE(part.listening_for_adsb());
    CHECK(radio.mode() == Lr2021Mode::Reset);
}

// A cooperative radio thread that spins on BUSY starves the BLE host and the service loop.
TEST_CASE("lr2021: a stuck BUSY is spun on for a millisecond, then polled in sleepable steps") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    hear(part, kFirst);
    part.busy_stuck = true;
    part.spun_us = 0;
    CHECK(read(radio).event.type == Lr2021EventType::Fault);
    CHECK(part.spun_us <= chip::kBusySpinUs);
}

TEST_CASE("lr2021: sleep sends the sleep time and waits for BUSY to stay high") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    CHECK(radio.sleep() == Status::Ok);
    CHECK(part.mode == models::Lr2021::Mode::Sleep);
    CHECK(part.sleep_bytes == 5);
    CHECK(part.refused.empty());
}

TEST_CASE("lr2021: a SetSleep the part refused leaves BUSY low, and is no sleep") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    part.fail_opcodes = {chip::kSetSleep};
    CHECK(radio.sleep() == Status::Down);
    CHECK(radio.mode() != Lr2021Mode::Sleep);
    CHECK(part.mode != models::Lr2021::Mode::Sleep);
}

TEST_CASE(
    "lr2021: a part that never came up is put to sleep, or held in reset when it cannot sleep") {
    models::Lr2021 clockless;
    clockless.calibration_errors = chip::kErrorHfXoscStart;
    Lr2021 radio = lr2021_for(clockless);
    REQUIRE(radio.begin(chip::kDetectionThresholdDbm) == Status::Down);
    radio.park();
    CHECK(clockless.mode == models::Lr2021::Mode::Sleep);
    CHECK_FALSE(clockless.in_reset);

    models::Lr2021 stuck;
    stuck.busy_stuck = true;
    Lr2021 silent = lr2021_for(stuck);
    REQUIRE(silent.begin(chip::kDetectionThresholdDbm) == Status::Timeout);
    silent.park();
    CHECK(stuck.in_reset);
    CHECK_FALSE(stuck.listening_for_adsb());
}
