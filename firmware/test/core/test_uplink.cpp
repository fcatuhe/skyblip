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
constexpr uint32_t kUtcS = 1785628800;

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
    observation.emergency = adsl::kEmergencyNone;
    return observation;
}
}  // namespace

// The uplink gives up its edges: §C.5's slot is 200 to 450 ms, the dwell hears 210 to 440 in full.
TEST_CASE("uplink: skyPost starts its longest frame from 210 to 425 ms, done by 435") {
    CHECK(adsl::kUplinkBurstUs == 9400);
    CHECK(timing::kGroundEmitStartMs == 210);
    // 440 - 9.4 - 5, rounded down.
    CHECK(timing::last_instant_ms(timing::kUplinkWindowMs, adsl::kUplinkBurstUs) == 425);
    CHECK(timing::kGroundEmitEndMs == 435);
    CHECK(425 * 1000u + adsl::kUplinkBurstUs <= timing::kGroundEmitEndMs * 1000u);

    const timing::Window dwell = timing::dwell_of(timing::kUplinkWindowMs);
    CHECK(dwell.start_ms < timing::kGroundEmitStartMs);
    CHECK(dwell.end_ms > timing::kGroundEmitEndMs);
    // A station that is not ours may start at 440.6 ms, and that frame ends after the dwell does.
    CHECK(adsl::kUplinkSlotEndMs * 1000u > static_cast<uint32_t>(dwell.end_ms) * 1000u);
    CHECK(adsl::kUplinkSlotStartMs < dwell.start_ms);
}

TEST_CASE("uplink: a relayed record is an uplink observation, with a speed only when it has one") {
    AdslUplink up;
    model::AircraftObservation tx[2] = {make_target(0x600001), make_target(0x600002)};
    tx[1].speed_valid = false;
    tx[1].altitude_m = -12;
    uint8_t packet[AdslUplink::kPacketBytes];
    REQUIRE(up.encode(tx, 2, kUtcS, 300, 0x00FFFE, adsl::kAddressTableManufacturerFirst, packet) ==
            Status::Ok);

    model::AircraftObservation rx[2];
    AdslUplink::DecodeStatistics statistics;
    REQUIRE(up.decode(packet, sizeof(packet), kUtcS, 300, rx, 2, statistics) == Status::Ok);
    CHECK(rx[0].source == model::Source::AdslUplink);
    CHECK(rx[0].position_valid);
    CHECK(rx[0].speed_valid);
    CHECK(int(rx[0].emergency) == 1);
    CHECK(int(rx[0].flight_state) == 2);
    CHECK_FALSE(rx[1].speed_valid);
    CHECK(rx[1].altitude_m == -12);
}
