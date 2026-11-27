// What the LR2021 says about itself once a second, so a capture can tell why Bravo hears nothing.
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

void hear_one(models::Lr2021& part, Lr2021& radio) {
    const AdsbAir air = adsb_air(adsb::squitter(0x4840D6, 5, 4));
    REQUIRE(part.receive_air(air.chips, sizeof(air.chips)));
    uint8_t frame[adsb::kFrameBytes]{};
    REQUIRE(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Frame);
}

}  // namespace

TEST_CASE("lr2021: bring-up clears the reset source its own NRESET pulse left in the status") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_for(part);
    REQUIRE(radio.begin() == Status::Ok);
    CHECK(part.reset_source() == 0);
    CHECK(part.last_order(chip::kGetErrors) < part.last_order(chip::kGetStatus));
}

TEST_CASE("lr2021: a probe reads the mode, the noise, the part's own count and the threshold") {
    models::Lr2021 part;
    part.noise_raw = 203;
    Lr2021 radio = lr2021_receiving(part);
    hear_one(part, radio);
    hear_one(part, radio);

    ports::ReceiverProbe probe{};
    REQUIRE(radio.sample(probe) == Status::Ok);
    CHECK(probe.valid);
    CHECK(probe.mode == ports::ReceiverMode::Receive);
    CHECK(probe.reset == ports::ReceiverReset::None);
    CHECK(probe.packets == 2);
    CHECK(probe.errors == 0);
    // 203 half-dB steps is -101.5 dBm, read in whole dB toward zero.
    CHECK(probe.noise_dbm == -101);
    CHECK(probe.threshold_dbm == models::Lr2021::kDefaultThresholdDbm);
    CHECK(probe.front_end_kept);
    CHECK(radio.part_resets() == 0);
    CHECK(radio.mode() == parts::Lr2021Mode::Receive);
}

TEST_CASE("lr2021: a brown-out is reported once, with the mode and settings the part lost") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    part.brown_out();

    ports::ReceiverProbe probe{};
    REQUIRE(radio.sample(probe) == Status::Ok);
    CHECK(probe.reset == ports::ReceiverReset::PowerOn);
    CHECK(probe.mode == ports::ReceiverMode::StandbyRc);
    CHECK(probe.threshold_dbm != models::Lr2021::kDefaultThresholdDbm);
    CHECK_FALSE(probe.front_end_kept);
    CHECK(radio.part_resets() == 1);
    CHECK(radio.mode() == parts::Lr2021Mode::Receive);

    REQUIRE(radio.sample(probe) == Status::Ok);
    CHECK(probe.reset == ports::ReceiverReset::None);
    CHECK(radio.part_resets() == 1);
}

TEST_CASE("lr2021: a part the probe found reset is brought up again at the next service") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    part.brown_out();
    REQUIRE_FALSE(part.listening_for_adsb());
    ports::ReceiverProbe probe{};
    REQUIRE(radio.sample(probe) == Status::Ok);
    REQUIRE(probe.reset == ports::ReceiverReset::PowerOn);

    CHECK(radio.service(250, 30000));
    CHECK(radio.resets() == 1);
    CHECK(radio.mode() == parts::Lr2021Mode::Receive);
    CHECK(part.listening_for_adsb());
    CHECK(part.reg(chip::kOokThresholdRegister) != 0);
    hear_one(part, radio);

    REQUIRE(radio.sample(probe) == Status::Ok);
    CHECK(probe.reset == ports::ReceiverReset::None);
    CHECK(probe.threshold_dbm == models::Lr2021::kDefaultThresholdDbm);
    CHECK(probe.front_end_kept);
    CHECK(radio.part_resets() == 1);
}

// Before the probe, a re-arm sent SetRx to a part that had lost its modem, and Bravo stayed deaf.
TEST_CASE("lr2021: a silent part that reset itself is not merely re-armed") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    part.brown_out();
    ports::ReceiverProbe probe{};
    REQUIRE(radio.sample(probe) == Status::Ok);
    CHECK(radio.service(30000, 30000));
    CHECK(radio.rearms() == 0);
    CHECK(part.listening_for_adsb());
}

TEST_CASE("lr2021: a parked part is not probed, so a sleeping part is never woken to answer") {
    models::Lr2021 part;
    Lr2021 radio = lr2021_receiving(part);
    REQUIRE(radio.sleep() == Status::Ok);
    const size_t seen = part.commands_seen.size();
    ports::ReceiverProbe probe{};
    CHECK(radio.sample(probe) == Status::Invalid);
    CHECK_FALSE(probe.valid);
    CHECK(part.commands_seen.size() == seen);
    CHECK(part.fault == models::Lr2021::Fault::None);
}
