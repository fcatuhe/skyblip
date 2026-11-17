// What a Go announces for its paired Bravo to play, and the dismissals it takes back from it.
#include "doctest/doctest.h"
#include "test/support/screen_rig.h"

using namespace skyblip;

namespace {

// INFO: fc 17nov26 a latitude step of 1e-7 degree is 1.1 cm, so 90 of them make a metre
constexpr int32_t kLatitude1e7PerMetre = 90;

struct Leader {
    ScreenRig rig;

    Leader() {
        rig.state.own.fix_valid = true;
        rig.state.own.latitude_1e7 = 481000000;
        rig.state.own.longitude_1e7 = 81000000;
        rig.state.own.altitude_hae_mm = 1000000;
        rig.state.own.speed_mm_s = 40000;
    }

    void hear(uint32_t address, int north_m, uint32_t now_ms) {
        rig.state.own.fix_at_ms = now_ms;
        model::AircraftObservation target{};
        target.address = address;
        target.address_mapping_table = 6;
        target.source = model::Source::AdslDirect;
        target.position_valid = true;
        target.latitude_1e7 = rig.state.own.latitude_1e7 + north_m * kLatitude1e7PerMetre;
        target.longitude_1e7 = rig.state.own.longitude_1e7;
        target.altitude_valid = true;
        target.altitude_m = rig.state.own.altitude_hae_mm / 1000;
        target.at_ms = now_ms;
        target.received.at_s = now_ms / 1000;
        rig.state.traffic.update(target, now_ms / 1000);
        rig.alarm_service.tick(now_ms);
    }

    const annunciation::Call& call() { return rig.state.alarm_call; }
};

}  // namespace

TEST_CASE("alarm call: each new aircraft in the window is a new announcement, numbered") {
    Leader leader;
    leader.rig.alarm_service.tick(500);
    CHECK(leader.call() == annunciation::Call{});

    leader.hear(0x515151, 800, 1000);
    CHECK(leader.call().announcing == traffic::Level::Advisory);
    CHECK(leader.call().undismissed == traffic::Level::Advisory);
    CHECK(leader.call().announcement == 1);

    leader.hear(0x515151, 780, 2000);
    CHECK(leader.call().announcement == 1);
    leader.hear(0x626262, 700, 3000);
    CHECK(leader.call().announcement == 2);
}

// A press on Bravo names the call it heard, so it never dismisses one Bravo had not played yet.
TEST_CASE("alarm call: a dismissal of the current announcement dismisses, an older one does not") {
    Leader leader;
    leader.hear(0x515151, 800, 1000);
    leader.hear(0x626262, 700, 2000);
    REQUIRE(leader.call().announcement == 2);

    leader.rig.alarm_service.dismiss_announcement(1);
    leader.rig.alarm_service.tick(2100);
    CHECK(leader.rig.state.alarm_undismissed == traffic::Level::Advisory);

    leader.rig.alarm_service.dismiss_announcement(2);
    leader.rig.alarm_service.tick(2200);
    CHECK(leader.rig.state.alarm_undismissed == traffic::Level::None);
    CHECK(leader.call().announcing == traffic::Level::None);
    CHECK(leader.call().undismissed == traffic::Level::None);
}

TEST_CASE("alarm call: off takes the Go's glass and lamp from the call, and still numbers it") {
    Leader leader;
    leader.rig.settings.alarm = go::Alarm::Off;
    leader.hear(0x515151, 800, 1000);
    CHECK(leader.call().undismissed == traffic::Level::None);
    CHECK(leader.call().announcing == traffic::Level::Advisory);
    CHECK(leader.call().announcement == 1);
}
