// The Mode S transponders Bravo holds: heard in clear first, then kept by any reply.
#include <cstdint>

#include "doctest/doctest.h"
#include "products/skyblip_bravo/transponders.h"
#include "protocols/adsb/squitter.h"
#include "test/support/adsb_frames.h"

using namespace skyblip;
using bravo::Transponders;

namespace {

constexpr uint32_t kModeS = 0x3A0001;
constexpr uint32_t kExtended = 0x3A0002;
constexpr uint32_t kStranger = 0x3A0003;

adsb::Message received(const adsb::Squitter& frame) {
    adsb::Message message{};
    REQUIRE(adsb::receive(frame.bytes, adsb::kFrameBytes, adsb::Reception{}, message) ==
            wire::Status::Ok);
    return message;
}

adsb::Message extended(uint32_t address) {
    adsb::Message message{};
    REQUIRE(adsb_hear(adsb::identification(address, 0, 4, 1, "FJABC"), message) == Status::Ok);
    return message;
}

}  // namespace

TEST_CASE("transponders: a squitter in clear admits an address, and its altitude replies count") {
    Transponders transponders;
    CHECK(transponders.hear(received(adsb::all_call(kModeS, 5)), 1000));
    CHECK(transponders.count() == 1);
    CHECK(transponders.mode_s_only() == 1);
    CHECK(transponders.mode_s_only_with_altitude(1000) == 0);

    CHECK(transponders.hear(received(adsb::altitude_reply(kModeS, 3500)), 2000));
    CHECK(transponders.mode_s_only_with_altitude(2000) == 1);
    CHECK(transponders.unmatched() == 0);
}

TEST_CASE("transponders: a reply from an address never heard in clear is refused and counted") {
    Transponders transponders;
    CHECK_FALSE(transponders.hear(received(adsb::altitude_reply(kStranger, 3500)), 1000));
    CHECK_FALSE(transponders.hear(received(adsb::all_call(kStranger, 5, 0x13)), 1000));
    CHECK(transponders.count() == 0);
    CHECK(transponders.unmatched() == 2);
}

TEST_CASE("transponders: an ADS-B aircraft is held, its replies kept, and it is not Mode S only") {
    Transponders transponders;
    CHECK(transponders.hear(extended(kExtended), 1000));
    CHECK(transponders.hear(received(adsb::altitude_reply(kExtended, 3500)), 1500));
    CHECK(transponders.count() == 1);
    CHECK(transponders.mode_s_only() == 0);
    CHECK(transponders.mode_s_only_with_altitude(1500) == 0);
}

TEST_CASE("transponders: one silent past the forget time is dropped, and its replies with it") {
    Transponders transponders;
    REQUIRE(transponders.hear(received(adsb::all_call(kModeS, 5)), 1000));
    transponders.age_out(1000 + Transponders::kTransponderForgetMs);
    CHECK(transponders.count() == 1);

    transponders.age_out(1001 + Transponders::kTransponderForgetMs);
    CHECK(transponders.count() == 0);
    CHECK_FALSE(transponders.hear(received(adsb::altitude_reply(kModeS, 3500)), 40000));
}

TEST_CASE("transponders: an altitude older than its max age no longer counts") {
    Transponders transponders;
    REQUIRE(transponders.hear(received(adsb::all_call(kModeS, 5)), 1000));
    REQUIRE(transponders.hear(received(adsb::altitude_reply(kModeS, 3500)), 1000));
    const uint32_t later = 1001 + Transponders::kAltitudeMaxAgeMs;
    REQUIRE(transponders.hear(received(adsb::all_call(kModeS, 5)), later));
    CHECK(transponders.mode_s_only_with_altitude(later) == 0);
}

TEST_CASE("transponders: a full roster gives up the transponder heard longest ago") {
    Transponders transponders;
    for (uint32_t i = 0; i < Transponders::kMaxTransponders; i++)
        REQUIRE(transponders.hear(received(adsb::all_call(0x400000 + i, 5)), 1000 + i));
    REQUIRE(transponders.hear(received(adsb::all_call(kModeS, 5)), 2000));
    CHECK(transponders.count() == Transponders::kMaxTransponders);
    CHECK_FALSE(transponders.hear(received(adsb::altitude_reply(0x400000, 3500)), 2000));
    CHECK(transponders.hear(received(adsb::altitude_reply(0x400001, 3500)), 2000));
}
