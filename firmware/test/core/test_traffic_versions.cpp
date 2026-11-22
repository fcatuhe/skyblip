// ADS-L B.5's bookkeeping: each client's version, how long it is kept, and who a full table drops.
#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/lease.h"
#include "core/traffic/versions.h"
#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "test/support/traffic_scene.h"

using namespace skyblip;
using namespace skyblip::traffic;

namespace {

constexpr uint32_t kHeardS = 100;
constexpr uint32_t kHeardMs = kHeardS * 1000;

model::AircraftObservation client(const model::Ownship& own, uint32_t address, int north_m,
                                  uint8_t version, uint32_t at_s = kHeardS) {
    model::AircraftObservation observation =
        target_at(own, north_m, 0, 0, 30, 90, static_cast<uint32_t>(own.fix_at_ms));
    observation.address = address;
    observation.adsl_version = version;
    observation.source = model::Source::AdslDirect;
    observation.flight_state = 2;
    observation.received.at_s = at_s;
    return observation;
}

}  // namespace

// G.2.11 sends a Status at least every 20 s, and an airborne position lapses after 6 s.
TEST_CASE("versions: a Status outlives its sender's position, and lapses three Status periods on") {
    const model::Ownship own = flying(30, 90, 0, kHeardMs);
    VersionTable versions;
    versions.hear_traffic(client(own, 0x111, 300, 0), kHeardS);
    versions.hear_status(6, 0x111, 1, kHeardS);
    REQUIRE(versions.readable_version(own, kHeardMs) == 1);

    versions.age_out(kHeardS + kAirborneTargetForgetS + 1);
    const int index = versions.find(6, 0x111);
    REQUIRE(index >= 0);
    CHECK_FALSE(versions.at(index)->placed);
    CHECK(versions.at(index)->declared);

    const uint32_t back_s = kHeardS + kStatusForgetS - 2;
    versions.hear_traffic(client(own, 0x111, 300, 0, back_s), back_s);
    CHECK(versions.readable_version(own, kHeardMs) == 1);
    versions.age_out(kHeardS + kStatusForgetS);
    CHECK(versions.at(index)->declared);
    versions.age_out(kHeardS + kStatusForgetS + 1);
    CHECK_FALSE(versions.at(index)->declared);
    CHECK(versions.at(index)->placed);
    CHECK(versions.readable_version(own, kHeardMs) == 0);

    versions.age_out(back_s + kAirborneTargetForgetS + 1);
    CHECK(versions.count() == 0);
}

TEST_CASE("versions: a full table gives up the far client heard longest ago, never a close one") {
    const model::Ownship own = flying(30, 90, 0, kHeardMs);
    VersionTable versions;
    versions.set_own_reference(own);
    versions.hear_traffic(client(own, 0x100, 300, 0, kHeardS - 1), kHeardS);
    for (uint32_t i = 1; i < VersionTable::kMaxClients; i++)
        versions.hear_traffic(client(own, 0x100 + i, 20000, 1, kHeardS - 1 + i), kHeardS + i);
    REQUIRE(versions.count() == VersionTable::kMaxClients);

    const uint32_t now_s = kHeardS + VersionTable::kMaxClients;
    versions.hear_traffic(client(own, 0x200, 25000, 1, now_s), now_s);
    CHECK(versions.find(6, 0x200) >= 0);
    CHECK(versions.find(6, 0x101) < 0);
    CHECK(versions.find(6, 0x100) >= 0);
    CHECK(versions.readable_version(own, kHeardMs) == 0);
}

// Only the lowest version among close clients sets own Traffic's, so that one is the one kept.
TEST_CASE("versions: a table of close clients gives one up only for a client reading less") {
    const model::Ownship own = flying(30, 90, 0, kHeardMs);
    VersionTable versions;
    versions.set_own_reference(own);
    for (uint32_t i = 0; i < VersionTable::kMaxClients; i++)
        versions.hear_traffic(client(own, 0x100 + i, 300, 1), kHeardS);

    versions.hear_traffic(client(own, 0x200, 20000, 0), kHeardS);
    CHECK(versions.find(6, 0x200) < 0);
    versions.hear_status(6, 0x201, 0, kHeardS);
    CHECK(versions.find(6, 0x201) < 0);
    CHECK(versions.readable_version(own, kHeardMs) == 1);

    versions.hear_traffic(client(own, 0x202, 400, 0), kHeardS);
    CHECK(versions.find(6, 0x202) >= 0);
    CHECK(versions.count() == VersionTable::kMaxClients);
    CHECK(versions.readable_version(own, kHeardMs) == 0);

    versions.hear_traffic(client(own, 0x203, 400, 1), kHeardS);
    CHECK(versions.find(6, 0x203) < 0);
    CHECK(versions.find(6, 0x202) >= 0);
}
