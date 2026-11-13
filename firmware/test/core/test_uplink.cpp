// The uplink as skyBlip hears it: the window the dwell map leaves it, and the observation a record
// becomes.
#include <cstring>

#include "core/model/aircraft.h"
#include "core/protocol/adsl_uplink.h"
#include "core/timing/slot.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::protocol;

namespace {
model::AircraftObservation make_target(uint32_t address) {
    model::AircraftObservation observation{};
    observation.address = address & 0xFFFFFF;
    observation.address_mapping_table = 6;
    observation.aircraft_category = 4;
    observation.flight_state = 2;
    observation.speed_q = 180;
    observation.speed_valid = true;
    observation.latitude_1e7 = 481234500;
    observation.longitude_1e7 = 81234500;
    observation.altitude_m = 1234;
    observation.position_valid = true;
    return observation;
}
}  // namespace

// Where the burst sits in the second, which is the fourth thing a dwell and a
// transmitter have to agree on after the sync word, the length and the rate.
//
// A full frame is 10.44 ms of air at §C.4's 200 kbps, and core/timing/slot.h
// cuts the O-band dwell at 205..395 because our own skyPost is on air from 210
// and "completes by 390". These are the sums behind that sentence.
TEST_CASE("uplink: a frame fits the window the dwell map is cut against") {
    CHECK(adsl::kUplinkBurstUs == 10440);

    // Our own ground station: on air at 210, done at 220.44, and the dwell that
    // was listening for it opened at 205 and closes at 395.
    const uint32_t ours_start_us = timing::kGroundEmitStartMs * 1000u;
    CHECK(ours_start_us > timing::kUplinkRxStartMs * 1000u);
    CHECK(ours_start_us + adsl::kUplinkBurstUs < timing::kUplinkRxEndMs * 1000u);

    // kGroundEmitEndMs is a completion deadline and not a start deadline, and the
    // difference is 10.44 ms: a burst BEGUN at 390 would still be on air at 400,
    // where the radio has already retuned to the M band.
    CHECK(timing::kGroundEmitEndMs * 1000u + adsl::kUplinkBurstUs > timing::kUplinkRxEndMs * 1000u);
    const uint32_t last_start_us = timing::kGroundEmitEndMs * 1000u - adsl::kUplinkBurstUs;
    CHECK(last_start_us > timing::kUplinkRxStartMs * 1000u);

    // And the limit this dwell has, stated rather than discovered: §C.5 gives
    // the uplink slot 200..450 ms and only requires a transmission to complete
    // before it ends, so a ground station that is not ours may legally begin one
    // as late as 439.56 ms. One radio cannot be on two bands at once and the
    // M-band dwell opens at 400 (§C.2.5 costs half the neighbours if we skip a
    // channel), so a third-party uplink transmitted in the second half of its
    // slot is one we do not hear. skyPost transmits at 210 for exactly this
    // reason, and hearing our own network is the capability we ship; a
    // third-party station that starts late is a known and accepted deaf spot.
    const uint32_t conforming_last_start_us = adsl::kUplinkSlotEndMs * 1000u - adsl::kUplinkBurstUs;
    CHECK(conforming_last_start_us > timing::kUplinkRxEndMs * 1000u);
    CHECK(adsl::kUplinkSlotStartMs < timing::kUplinkRxStartMs);
}

TEST_CASE("uplink: a relayed record is an uplink observation, with a speed only when it has one") {
    AdslUplink up;
    model::AircraftObservation tx[2] = {make_target(0x600001), make_target(0x600002)};
    tx[1].speed_q = 0;
    tx[1].altitude_m = -12;
    uint8_t frame[AdslUplink::kFrameBytes];
    REQUIRE(up.encode(tx, 2, 0, frame) == Status::Ok);

    model::AircraftObservation rx[2];
    AdslUplink::DecodeStatistics statistics;
    REQUIRE(up.decode(frame, rx, 2, statistics) == Status::Ok);
    CHECK(rx[0].source == model::Source::AdslUplink);
    CHECK(rx[0].position_valid);
    CHECK(rx[0].speed_valid);
    CHECK(int(rx[0].emergency) == 1);
    CHECK(int(rx[0].flight_state) == 2);
    CHECK_FALSE(rx[1].speed_valid);
    CHECK(rx[1].altitude_m == -12);
}
