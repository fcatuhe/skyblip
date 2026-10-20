// Aircraft flying together, told from geometry alone: no flying style is named, no pilot is asked.
#include <cmath>

#include "core/traffic/formation.h"
#include "core/traffic/lease.h"
#include "core/units/units.h"
#include "core/util/intmath.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::formation;

namespace {

model::Ownship flying(int speed_mps, int track_deg, uint32_t at_ms = 0) {
    model::Ownship own{};
    own.fix_valid = true;
    own.fix_at_ms = at_ms;
    own.latitude_1e7 = 481000000;
    own.longitude_1e7 = 81000000;
    own.altitude_hae_mm = 1000000;
    own.speed_mm_s = speed_mps * 1000;
    own.track_cdeg = track_deg * 100;
    return own;
}

model::AircraftObservation target_at(const model::Ownship& own, int north_m, int east_m, int up_m,
                                     int speed_mps, int track_deg, uint32_t at_ms) {
    model::AircraftObservation observation{};
    observation.address = 0x424242;
    observation.address_mapping_table = 6;
    observation.position_valid = true;
    observation.speed_valid = true;
    observation.speed_q = static_cast<uint16_t>(speed_mps * 4);
    observation.track_c9 = to_cordic9(CentiDegrees(track_deg * 100)).value;
    observation.altitude_m = to_metres(Millimetres(own.altitude_hae_mm)).value + up_m;
    observation.latitude_1e7 =
        own.latitude_1e7 + static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    const int16_t latitude_angle =
        static_cast<int16_t>((static_cast<int64_t>(own.latitude_1e7) * 65536) / 3600000000LL);
    const int64_t east_scaled = static_cast<int64_t>(east_m) * kTrigOne / cosine(latitude_angle);
    observation.longitude_1e7 =
        own.longitude_1e7 + static_cast<int32_t>(east_scaled * 1000000 / 11132);
    observation.received.at_s = at_ms / 1000;
    observation.at_ms = at_ms;
    return observation;
}

uint32_t fly_on_station(Tracker& tracker) {
    uint32_t now_ms = 1000;
    for (; now_ms <= 1000 + kTogetherHoldMs + 1000; now_ms += 1000) {
        const model::Ownship own = flying(40, 90, now_ms);
        tracker.observe(own, target_at(own, -60, -120, 10, 40, 90, now_ms), {}, now_ms);
    }
    return now_ms - 1000;
}

}  // namespace

TEST_CASE("formation: a wingman holding station is flying with us after the steady window") {
    Tracker tracker;
    Report report{};
    for (uint32_t now_ms = 1000; now_ms <= 1000 + kTogetherHoldMs + 1000; now_ms += 1000) {
        const model::Ownship own = flying(40, 90, now_ms);
        report = tracker.observe(own, target_at(own, -60, -120, 10, 40, 90, now_ms), {}, now_ms);
    }
    CHECK(report.state == State::Together);
    CHECK(tracker.together(6, 0x424242));
    CHECK(tracker.members() == 1);
}

TEST_CASE("formation: a target crossing our path never joins") {
    Tracker tracker;
    Report report{};
    for (int i = 0; i <= 10; i++) {
        const uint32_t now_ms = 1000 + static_cast<uint32_t>(i) * 1000;
        const model::Ownship own = flying(40, 0, now_ms);
        report =
            tracker.observe(own, target_at(own, 600, 400 - 80 * i, 0, 40, 270, now_ms), {}, now_ms);
    }
    CHECK(report.state == State::None);
}

TEST_CASE("formation: a member drifting away is parting, and it takes two fixes") {
    Tracker tracker;
    for (uint32_t now_ms = 1000; now_ms <= 1000 + kTogetherHoldMs + 1000; now_ms += 1000) {
        const model::Ownship own = flying(40, 90, now_ms);
        tracker.observe(own, target_at(own, -60, -120, 10, 40, 90, now_ms), {}, now_ms);
    }

    uint32_t now_ms = 1000 + kTogetherHoldMs + 2000;
    model::Ownship own = flying(40, 90, now_ms);
    // One bad solution does not break a formation.
    CHECK(tracker.observe(own, target_at(own, -60, -400, 10, 40, 90, now_ms), {}, now_ms).state ==
          State::Together);
    CHECK(tracker.together(6, 0x424242));

    now_ms += 1000;
    own = flying(40, 90, now_ms);
    CHECK(tracker.observe(own, target_at(own, -60, -700, 10, 40, 90, now_ms), {}, now_ms).state ==
          State::Parting);
    CHECK_FALSE(tracker.together(6, 0x424242));
    CHECK(tracker.members() == 0);

    now_ms += 1000;
    own = flying(40, 90, now_ms);
    CHECK(tracker.observe(own, target_at(own, -60, -900, 10, 40, 90, now_ms), {}, now_ms).state ==
          State::Parting);
}

// A break once asked for 60 m/s, then a re-anchored Parting rejoined any peel-off up to 10 m/s.
TEST_CASE("formation: a member peeling off at any speed parts once and stays parted to the band") {
    for (const int speed_mps : {3, 5, 10, 15}) {
        CAPTURE(speed_mps);
        Tracker tracker;
        uint32_t now_ms = fly_on_station(tracker);
        REQUIRE(tracker.together(6, 0x424242));

        int breaks = 0;
        int rejoins = 0;
        State was = State::Together;
        for (int away_m = speed_mps; was != State::None && away_m <= 2 * kRangeM;
             away_m += speed_mps) {
            now_ms += 1000;
            const model::Ownship own = flying(40, 90, now_ms);
            const State now =
                tracker
                    .observe(own, target_at(own, -60 - away_m, -120, 10, 40, 90, now_ms), {},
                             now_ms)
                    .state;
            if (was == State::Together && now == State::Parting) breaks++;
            if (was == State::Parting && now == State::Together) rejoins++;
            if (now == State::Parting) CHECK(tracker.members() == 0);
            was = now;
        }
        CHECK(breaks == 1);
        CHECK(rejoins == 0);
        CHECK(was == State::None);
    }
}

TEST_CASE("formation: a pair that parts is two aircraft again once it leaves the band") {
    Tracker tracker;
    for (uint32_t now_ms = 1000; now_ms <= 1000 + kTogetherHoldMs + 1000; now_ms += 1000) {
        const model::Ownship own = flying(40, 90, now_ms);
        tracker.observe(own, target_at(own, -60, -120, 10, 40, 90, now_ms), {}, now_ms);
    }

    const uint32_t now_ms = 1000 + kTogetherHoldMs + 2000;
    const model::Ownship own = flying(40, 90, now_ms);
    CHECK(tracker.observe(own, target_at(own, -60, -1600, 10, 40, 90, now_ms), {}, now_ms).state ==
          State::None);
    CHECK_FALSE(tracker.together(6, 0x424242));
}

TEST_CASE("formation: a parted neighbour that comes back to its station rejoins") {
    Tracker tracker;
    uint32_t now_ms = fly_on_station(tracker) + 1000;
    for (int i = 0; i < kBreakFixes; i++, now_ms += 1000) {
        const model::Ownship own = flying(40, 90, now_ms);
        tracker.observe(own, target_at(own, -60, -300 - 200 * i, 10, 40, 90, now_ms), {}, now_ms);
    }
    REQUIRE_FALSE(tracker.together(6, 0x424242));

    Report report{};
    for (const uint32_t until = now_ms + kTogetherHoldMs + 1000; now_ms <= until; now_ms += 1000) {
        const model::Ownship own = flying(40, 90, now_ms);
        report = tracker.observe(own, target_at(own, -60, -140, 10, 40, 90, now_ms), {}, now_ms);
    }
    CHECK(report.state == State::Together);
}

// A slow peel-off holds any box for six fixes, so only the station it left can take it back.
TEST_CASE("formation: a parted neighbour holding a station elsewhere is still parting") {
    Tracker tracker;
    uint32_t now_ms = fly_on_station(tracker) + 1000;
    for (int i = 0; i < kBreakFixes; i++, now_ms += 1000) {
        const model::Ownship own = flying(40, 90, now_ms);
        tracker.observe(own, target_at(own, -60, -300 - 200 * i, 10, 40, 90, now_ms), {}, now_ms);
    }

    Report report{};
    for (const uint32_t until = now_ms + 3 * kTogetherHoldMs; now_ms <= until; now_ms += 1000) {
        const model::Ownship own = flying(40, 90, now_ms);
        report = tracker.observe(own, target_at(own, -60, -700, 10, 40, 90, now_ms), {}, now_ms);
    }
    CHECK(report.state == State::Parting);
    CHECK(tracker.members() == 0);
}

// Four metres apart the line of sight is GNSS noise, and a car trip put 3.1 m/s of closure on it.
TEST_CASE("formation: one fix of closure is noise, two release the member and re-arm the window") {
    Tracker tracker;
    uint32_t now_ms = fly_on_station(tracker) + 1000;
    REQUIRE(tracker.together(6, 0x424242));

    // On station, but 4 m/s faster from behind: 3.6 m/s of it on the line of sight.
    model::Ownship own = flying(40, 90, now_ms);
    CHECK(tracker.observe(own, target_at(own, -60, -120, 10, 44, 90, now_ms), {}, now_ms).state ==
          State::Together);

    now_ms += 1000;
    own = flying(40, 90, now_ms);
    CHECK(tracker.observe(own, target_at(own, -60, -120, 10, 44, 90, now_ms), {}, now_ms).state ==
          State::None);

    now_ms += 1000;
    own = flying(40, 90, now_ms);
    CHECK(tracker.observe(own, target_at(own, -60, -120, 10, 40, 90, now_ms), {}, now_ms).state ==
          State::None);
}

// The service loop re-read one report every 10 ms, so one bad solution was two fixes 10 ms apart.
TEST_CASE("formation: a break is counted in receptions, not in passes of the loop") {
    Tracker tracker;
    const uint32_t now_ms = fly_on_station(tracker) + 1000;
    REQUIRE(tracker.together(6, 0x424242));

    const model::Ownship own = flying(40, 90, now_ms);
    const model::AircraftObservation bad = target_at(own, -60, -400, 10, 40, 90, now_ms);
    for (uint32_t pass = 0; pass < 100; pass++) tracker.observe(own, bad, {}, now_ms + pass * 10);
    CHECK(tracker.together(6, 0x424242));
}

TEST_CASE("formation: two gliders on one thermal circle are flying together") {
    Tracker tracker;
    Report report{};
    const int16_t turn = 12;
    for (int i = 0; i <= 8; i++) {
        const uint32_t now_ms = 1000 + static_cast<uint32_t>(i) * 1000;
        const int track_deg = turn * i;
        model::Ownship own = flying(23, track_deg, now_ms);
        own.turn_cdps = turn * 100;
        // Diametrically opposite on one circle: 222 m off the right wing, always.
        const double heading = track_deg * 3.14159265358979 / 180.0;
        const int north_m = static_cast<int>(-222.0 * std::sin(heading));
        const int east_m = static_cast<int>(222.0 * std::cos(heading));
        report = tracker.observe(
            own, target_at(own, north_m, east_m, 0, 23, track_deg + 180, now_ms), {}, now_ms);
    }
    CHECK(report.state == State::Together);
}

TEST_CASE("formation: a target nobody has heard from is forgotten, membership and all") {
    Tracker tracker;
    for (uint32_t now_ms = 1000; now_ms <= 1000 + kTogetherHoldMs + 1000; now_ms += 1000) {
        const model::Ownship own = flying(40, 90, now_ms);
        tracker.observe(own, target_at(own, -60, -120, 10, 40, 90, now_ms), {}, now_ms);
    }
    CHECK(tracker.members() == 1);

    const uint32_t lease_ms = traffic::kAirborneTargetForgetS * 1000;
    tracker.forget_stale(1000 + kTogetherHoldMs + 1000 + lease_ms + 1);
    CHECK(tracker.members() == 0);
    CHECK_FALSE(tracker.together(6, 0x424242));
}

// The anchor was own-ship until the first drifting fix, so a target inside the box never moved.
TEST_CASE("formation: a target that appears alongside is anchored where it was first seen") {
    Tracker tracker;
    Report report{};
    for (uint32_t now_ms = 1000; now_ms <= 1000 + kTogetherHoldMs + 1000; now_ms += 1000) {
        const model::Ownship own = flying(40, 90, now_ms);
        const int right_m = (now_ms / 1000) % 2 == 0 ? 40 : -40;
        report = tracker.observe(own, target_at(own, -right_m, 0, 0, 40, 90, now_ms), {}, now_ms);
    }
    CHECK(report.state == State::None);
}

// A tug and its glider hold station on the apron as well as they do on tow.
TEST_CASE("formation: an aircraft that says it is on the ground is never a wingman") {
    Tracker tracker;
    Report report{};
    for (uint32_t now_ms = 1000; now_ms <= 1000 + kTogetherHoldMs + 1000; now_ms += 1000) {
        const model::Ownship own = flying(40, 90, now_ms);
        model::AircraftObservation parked = target_at(own, -60, -120, 10, 40, 90, now_ms);
        parked.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
        report = tracker.observe(own, parked, {}, now_ms);
    }
    CHECK(report.state == State::None);
    CHECK(tracker.members() == 0);
}

// A landing is the end of a formation, not six seconds of it counted on the square.
TEST_CASE("formation: a member that reports itself on the ground leaves at once") {
    Tracker tracker;
    const uint32_t now_ms = fly_on_station(tracker) + 1000;
    REQUIRE(tracker.members() == 1);

    const model::Ownship own = flying(40, 90, now_ms);
    model::AircraftObservation landed = target_at(own, -60, -120, 10, 40, 90, now_ms);
    landed.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
    CHECK(tracker.observe(own, landed, {}, now_ms).state == State::None);
    CHECK(tracker.members() == 0);
    CHECK_FALSE(tracker.together(6, 0x424242));
}

namespace {

model::Ownship with_state(model::Ownship own, flight::FlightState state) {
    own.flight_state = static_cast<uint8_t>(state);
    return own;
}

model::Ownship parked_at(uint32_t at_ms) {
    return with_state(flying(0, 90, at_ms), flight::FlightState::Ground);
}

model::Ownship airborne_at(uint32_t at_ms) {
    return with_state(flying(40, 90, at_ms), flight::FlightState::Airborne);
}

}  // namespace

// A parked pair holds station better than any patrol, and the apron would be one formation.
TEST_CASE("formation: nothing flies in formation while own-ship is on the ground") {
    Tracker tracker;
    Report report{};
    for (uint32_t now_ms = 1000; now_ms <= 1000 + 3 * kTogetherHoldMs; now_ms += 1000) {
        const model::Ownship own = parked_at(now_ms);
        tracker.observe_own(own, now_ms);
        report = tracker.observe(own, target_at(own, -10, -20, 0, 0, 90, now_ms), {}, now_ms);
    }
    CHECK(report.state == State::None);
    CHECK(tracker.members() == 0);
}

TEST_CASE("formation: a formation ends when own-ship lands") {
    Tracker tracker;
    const uint32_t now_ms = fly_on_station(tracker) + 1000;
    REQUIRE(tracker.members() == 1);

    tracker.observe_own(parked_at(now_ms), now_ms);
    CHECK(tracker.members() == 0);
    CHECK_FALSE(tracker.together(6, 0x424242));
}

// Every takeoff as a pair re-formed it through an advisory, because a fresh slot is traffic.
TEST_CASE("formation: a target beside us as we lift off is joining until it holds station") {
    Tracker tracker;
    tracker.observe_own(parked_at(0), 0);

    Report report{};
    uint32_t now_ms = 1000;
    for (; now_ms < 1000 + kTogetherHoldMs; now_ms += 1000) {
        const model::Ownship own = airborne_at(now_ms);
        tracker.observe_own(own, now_ms);
        report = tracker.observe(own, target_at(own, -20, -30, 0, 40, 90, now_ms), {}, now_ms);
        CHECK(report.state == State::Joining);
        CHECK(tracker.members() == 0);
    }
    const model::Ownship own = airborne_at(now_ms);
    tracker.observe_own(own, now_ms);
    CHECK(tracker.observe(own, target_at(own, -20, -30, 0, 40, 90, now_ms), {}, now_ms).state ==
          State::Together);
}

// A tug lifts off after the glider it tows, so own-ship's liftoff is not the one that counts.
TEST_CASE("formation: a target that lifts off beside us is joining") {
    Tracker tracker;
    const uint32_t now_ms = 30000;
    const model::Ownship own = airborne_at(now_ms);
    tracker.observe_own(own, now_ms);

    const flight::Liftoff just_now{now_ms, true};
    CHECK(tracker.observe(own, target_at(own, 0, 60, 0, 40, 90, now_ms), just_now, now_ms).state ==
          State::Joining);
}

TEST_CASE("formation: a target first heard in the air is traffic, not a joiner") {
    Tracker tracker;
    const uint32_t now_ms = 30000;
    const model::Ownship own = airborne_at(now_ms);
    tracker.observe_own(own, now_ms);
    CHECK(tracker.observe(own, target_at(own, 0, 60, 0, 40, 90, now_ms), {}, now_ms).state ==
          State::None);

    Tracker later;
    later.observe_own(own, now_ms);
    const flight::Liftoff long_ago{now_ms - kLiftoffWindowMs, true};
    CHECK(later.observe(own, target_at(own, 0, 60, 0, 40, 90, now_ms), long_ago, now_ms).state ==
          State::None);
}

TEST_CASE("formation: a joiner that leaves its station is traffic on that fix, and stays it") {
    Tracker tracker;
    uint32_t now_ms = 30000;
    model::Ownship own = airborne_at(now_ms);
    tracker.observe_own(own, now_ms);
    const flight::Liftoff just_now{now_ms, true};
    REQUIRE(
        tracker.observe(own, target_at(own, 0, 60, 0, 40, 90, now_ms), just_now, now_ms).state ==
        State::Joining);

    now_ms += 1000;
    own = airborne_at(now_ms);
    CHECK(tracker.observe(own, target_at(own, 0, 300, 0, 40, 90, now_ms), just_now, now_ms).state ==
          State::None);

    now_ms += 1000;
    own = airborne_at(now_ms);
    CHECK(tracker.observe(own, target_at(own, 0, 300, 0, 40, 90, now_ms), just_now, now_ms).state ==
          State::None);
}

// An interval takeoff: the second aircraft catches up with the first, and that is a closure.
TEST_CASE("formation: a joiner closing on us is traffic after two fixes of it") {
    Tracker tracker;
    tracker.observe_own(parked_at(0), 0);

    uint32_t now_ms = 1000;
    model::Ownship own = airborne_at(now_ms);
    tracker.observe_own(own, now_ms);
    REQUIRE(tracker.observe(own, target_at(own, 0, -300, 0, 50, 90, now_ms), {}, now_ms).state ==
            State::Joining);

    now_ms += 1000;
    own = airborne_at(now_ms);
    CHECK(tracker.observe(own, target_at(own, 0, -290, 0, 50, 90, now_ms), {}, now_ms).state ==
          State::Joining);

    now_ms += 1000;
    own = airborne_at(now_ms);
    CHECK(tracker.observe(own, target_at(own, 0, -280, 0, 50, 90, now_ms), {}, now_ms).state ==
          State::None);
}
