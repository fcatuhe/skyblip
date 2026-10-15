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

uint16_t c9(int deg) { return static_cast<uint16_t>(((deg % 360 + 360) % 360) * 512 / 360); }

model::OwnState flying(int mps, int track_deg, uint32_t at_ms = 0) {
    model::OwnState o{};
    o.fix_valid = true;
    o.fix_ms = at_ms;
    o.lat_1e7 = 481000000;
    o.lon_1e7 = 81000000;
    o.alt_mm = 1000000;
    o.speed_mm_s = mps * 1000;
    o.track_cdeg = track_deg * 100;
    return o;
}

model::AircraftObs neighbour(const model::OwnState& own, int north_m, int east_m, int up_m, int mps,
                             int track_deg, uint32_t at_ms) {
    model::AircraftObs t{};
    t.addr = 0x424242;
    t.addr_table = 6;
    t.position_valid = true;
    t.speed_valid = true;
    t.speed_q = static_cast<uint16_t>(mps * 4);
    t.track_c9 = c9(track_deg);
    t.alt_m = to_metres(Millimetres(own.alt_mm)).v + up_m;
    t.lat_1e7 = own.lat_1e7 + static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    const int16_t ang =
        static_cast<int16_t>((static_cast<int64_t>(own.lat_1e7) * 65536) / 3600000000LL);
    const int64_t east_scaled = static_cast<int64_t>(east_m) * 16384 / icos(ang);
    t.lon_1e7 = own.lon_1e7 + static_cast<int32_t>(east_scaled * 1000000 / 11132);
    t.received.at_s = at_ms / 1000;
    t.at_ms = at_ms;
    return t;
}

uint32_t fly_on_station(Tracker& tracker) {
    uint32_t t = 1000;
    for (; t <= 1000 + kTogetherHoldMs + 1000; t += 1000) {
        const model::OwnState own = flying(40, 90, t);
        tracker.observe(own, neighbour(own, -60, -120, 10, 40, 90, t), {}, t);
    }
    return t - 1000;
}

}  // namespace

TEST_CASE("formation: a wingman holding station is flying with us after the steady window") {
    Tracker tracker;
    Report r{};
    for (uint32_t t = 1000; t <= 1000 + kTogetherHoldMs + 1000; t += 1000) {
        const model::OwnState own = flying(40, 90, t);
        r = tracker.observe(own, neighbour(own, -60, -120, 10, 40, 90, t), {}, t);
    }
    CHECK(r.state == State::Together);
    CHECK(tracker.together(6, 0x424242));
    CHECK(tracker.members() == 1);
}

TEST_CASE("formation: a contact crossing our path never joins") {
    Tracker tracker;
    Report r{};
    for (int i = 0; i <= 10; i++) {
        const uint32_t t = 1000 + static_cast<uint32_t>(i) * 1000;
        const model::OwnState own = flying(40, 0, t);
        r = tracker.observe(own, neighbour(own, 600, 400 - 80 * i, 0, 40, 270, t), {}, t);
    }
    CHECK(r.state == State::None);
}

TEST_CASE("formation: a member drifting away is parting, and it takes two fixes") {
    Tracker tracker;
    for (uint32_t t = 1000; t <= 1000 + kTogetherHoldMs + 1000; t += 1000) {
        const model::OwnState own = flying(40, 90, t);
        tracker.observe(own, neighbour(own, -60, -120, 10, 40, 90, t), {}, t);
    }

    uint32_t t = 1000 + kTogetherHoldMs + 2000;
    model::OwnState own = flying(40, 90, t);
    // One bad solution does not break a formation.
    CHECK(tracker.observe(own, neighbour(own, -60, -400, 10, 40, 90, t), {}, t).state ==
          State::Together);
    CHECK(tracker.together(6, 0x424242));

    t += 1000;
    own = flying(40, 90, t);
    CHECK(tracker.observe(own, neighbour(own, -60, -700, 10, 40, 90, t), {}, t).state ==
          State::Parting);
    CHECK_FALSE(tracker.together(6, 0x424242));
    CHECK(tracker.members() == 0);

    t += 1000;
    own = flying(40, 90, t);
    CHECK(tracker.observe(own, neighbour(own, -60, -900, 10, 40, 90, t), {}, t).state ==
          State::Parting);
}

// A break once asked for 60 m/s, then a re-anchored Parting rejoined any peel-off up to 10 m/s.
TEST_CASE("formation: a member peeling off at any speed parts once and stays parted to the band") {
    for (const int mps : {3, 5, 10, 15}) {
        CAPTURE(mps);
        Tracker tracker;
        uint32_t t = fly_on_station(tracker);
        REQUIRE(tracker.together(6, 0x424242));

        int breaks = 0;
        int rejoins = 0;
        State was = State::Together;
        for (int away_m = mps; was != State::None && away_m <= 2 * kRangeM; away_m += mps) {
            t += 1000;
            const model::OwnState own = flying(40, 90, t);
            const State now =
                tracker.observe(own, neighbour(own, -60 - away_m, -120, 10, 40, 90, t), {}, t)
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
    for (uint32_t t = 1000; t <= 1000 + kTogetherHoldMs + 1000; t += 1000) {
        const model::OwnState own = flying(40, 90, t);
        tracker.observe(own, neighbour(own, -60, -120, 10, 40, 90, t), {}, t);
    }

    const uint32_t t = 1000 + kTogetherHoldMs + 2000;
    const model::OwnState own = flying(40, 90, t);
    CHECK(tracker.observe(own, neighbour(own, -60, -1600, 10, 40, 90, t), {}, t).state ==
          State::None);
    CHECK_FALSE(tracker.together(6, 0x424242));
}

TEST_CASE("formation: a parted neighbour that comes back to its station rejoins") {
    Tracker tracker;
    uint32_t t = fly_on_station(tracker) + 1000;
    for (int i = 0; i < kBreakFixes; i++, t += 1000) {
        const model::OwnState own = flying(40, 90, t);
        tracker.observe(own, neighbour(own, -60, -300 - 200 * i, 10, 40, 90, t), {}, t);
    }
    REQUIRE_FALSE(tracker.together(6, 0x424242));

    Report r{};
    for (const uint32_t until = t + kTogetherHoldMs + 1000; t <= until; t += 1000) {
        const model::OwnState own = flying(40, 90, t);
        r = tracker.observe(own, neighbour(own, -60, -140, 10, 40, 90, t), {}, t);
    }
    CHECK(r.state == State::Together);
}

// A slow peel-off holds any box for six fixes, so only the station it left can take it back.
TEST_CASE("formation: a parted neighbour holding a station elsewhere is still parting") {
    Tracker tracker;
    uint32_t t = fly_on_station(tracker) + 1000;
    for (int i = 0; i < kBreakFixes; i++, t += 1000) {
        const model::OwnState own = flying(40, 90, t);
        tracker.observe(own, neighbour(own, -60, -300 - 200 * i, 10, 40, 90, t), {}, t);
    }

    Report r{};
    for (const uint32_t until = t + 3 * kTogetherHoldMs; t <= until; t += 1000) {
        const model::OwnState own = flying(40, 90, t);
        r = tracker.observe(own, neighbour(own, -60, -700, 10, 40, 90, t), {}, t);
    }
    CHECK(r.state == State::Parting);
    CHECK(tracker.members() == 0);
}

// Four metres apart the line of sight is GNSS noise, and a car trip put 3.1 m/s of closure on it.
TEST_CASE("formation: one fix of closure is noise, two release the member and re-arm the window") {
    Tracker tracker;
    uint32_t t = fly_on_station(tracker) + 1000;
    REQUIRE(tracker.together(6, 0x424242));

    // On station, but 4 m/s faster from behind: 3.6 m/s of it on the line of sight.
    model::OwnState own = flying(40, 90, t);
    CHECK(tracker.observe(own, neighbour(own, -60, -120, 10, 44, 90, t), {}, t).state ==
          State::Together);

    t += 1000;
    own = flying(40, 90, t);
    CHECK(tracker.observe(own, neighbour(own, -60, -120, 10, 44, 90, t), {}, t).state ==
          State::None);

    t += 1000;
    own = flying(40, 90, t);
    CHECK(tracker.observe(own, neighbour(own, -60, -120, 10, 40, 90, t), {}, t).state ==
          State::None);
}

// The service loop re-read one report every 10 ms, so one bad solution was two fixes 10 ms apart.
TEST_CASE("formation: a break is counted in receptions, not in passes of the loop") {
    Tracker tracker;
    const uint32_t t = fly_on_station(tracker) + 1000;
    REQUIRE(tracker.together(6, 0x424242));

    const model::OwnState own = flying(40, 90, t);
    const model::AircraftObs bad = neighbour(own, -60, -400, 10, 40, 90, t);
    for (uint32_t pass = 0; pass < 100; pass++) tracker.observe(own, bad, {}, t + pass * 10);
    CHECK(tracker.together(6, 0x424242));
}

TEST_CASE("formation: two gliders on one thermal circle are flying together") {
    Tracker tracker;
    Report r{};
    const int16_t turn = 12;
    for (int i = 0; i <= 8; i++) {
        const uint32_t t = 1000 + static_cast<uint32_t>(i) * 1000;
        const int track_deg = turn * i;
        model::OwnState own = flying(23, track_deg, t);
        own.turn_cdps = turn * 100;
        // Diametrically opposite on one circle: 222 m off the right wing, always.
        const double heading = track_deg * 3.14159265358979 / 180.0;
        const int north_m = static_cast<int>(-222.0 * std::sin(heading));
        const int east_m = static_cast<int>(222.0 * std::cos(heading));
        r = tracker.observe(own, neighbour(own, north_m, east_m, 0, 23, track_deg + 180, t), {}, t);
    }
    CHECK(r.state == State::Together);
}

TEST_CASE("formation: a contact nobody has heard from is forgotten, membership and all") {
    Tracker tracker;
    for (uint32_t t = 1000; t <= 1000 + kTogetherHoldMs + 1000; t += 1000) {
        const model::OwnState own = flying(40, 90, t);
        tracker.observe(own, neighbour(own, -60, -120, 10, 40, 90, t), {}, t);
    }
    CHECK(tracker.members() == 1);

    const uint32_t lease_ms = traffic::kAirborneTargetForgetS * 1000;
    tracker.forget_stale(1000 + kTogetherHoldMs + 1000 + lease_ms + 1);
    CHECK(tracker.members() == 0);
    CHECK_FALSE(tracker.together(6, 0x424242));
}

// The anchor was own-ship until the first drifting fix, so a contact inside the box never moved.
TEST_CASE("formation: a contact that appears alongside is anchored where it was first seen") {
    Tracker tracker;
    Report r{};
    for (uint32_t t = 1000; t <= 1000 + kTogetherHoldMs + 1000; t += 1000) {
        const model::OwnState own = flying(40, 90, t);
        const int right_m = (t / 1000) % 2 == 0 ? 40 : -40;
        r = tracker.observe(own, neighbour(own, -right_m, 0, 0, 40, 90, t), {}, t);
    }
    CHECK(r.state == State::None);
}

// A tug and its glider hold station on the apron as well as they do on tow.
TEST_CASE("formation: an aircraft that says it is on the ground is never a wingman") {
    Tracker tracker;
    Report r{};
    for (uint32_t t = 1000; t <= 1000 + kTogetherHoldMs + 1000; t += 1000) {
        const model::OwnState own = flying(40, 90, t);
        model::AircraftObs parked = neighbour(own, -60, -120, 10, 40, 90, t);
        parked.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
        r = tracker.observe(own, parked, {}, t);
    }
    CHECK(r.state == State::None);
    CHECK(tracker.members() == 0);
}

// A landing is the end of a formation, not six seconds of it counted on the square.
TEST_CASE("formation: a member that reports itself on the ground leaves at once") {
    Tracker tracker;
    const uint32_t t = fly_on_station(tracker) + 1000;
    REQUIRE(tracker.members() == 1);

    const model::OwnState own = flying(40, 90, t);
    model::AircraftObs landed = neighbour(own, -60, -120, 10, 40, 90, t);
    landed.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
    CHECK(tracker.observe(own, landed, {}, t).state == State::None);
    CHECK(tracker.members() == 0);
    CHECK_FALSE(tracker.together(6, 0x424242));
}

namespace {

model::OwnState with_state(model::OwnState own, flight::FlightState state) {
    own.flight_state = static_cast<uint8_t>(state);
    return own;
}

model::OwnState parked_at(uint32_t at_ms) {
    return with_state(flying(0, 90, at_ms), flight::FlightState::Ground);
}

model::OwnState airborne_at(uint32_t at_ms) {
    return with_state(flying(40, 90, at_ms), flight::FlightState::Airborne);
}

}  // namespace

// A parked pair holds station better than any patrol, and the apron would be one formation.
TEST_CASE("formation: nothing flies in formation while own-ship is on the ground") {
    Tracker tracker;
    Report r{};
    for (uint32_t t = 1000; t <= 1000 + 3 * kTogetherHoldMs; t += 1000) {
        const model::OwnState own = parked_at(t);
        tracker.observe_own(own, t);
        r = tracker.observe(own, neighbour(own, -10, -20, 0, 0, 90, t), {}, t);
    }
    CHECK(r.state == State::None);
    CHECK(tracker.members() == 0);
}

TEST_CASE("formation: a formation ends when own-ship lands") {
    Tracker tracker;
    const uint32_t t = fly_on_station(tracker) + 1000;
    REQUIRE(tracker.members() == 1);

    tracker.observe_own(parked_at(t), t);
    CHECK(tracker.members() == 0);
    CHECK_FALSE(tracker.together(6, 0x424242));
}

// Every takeoff as a pair re-formed it through an advisory, because a fresh slot is traffic.
TEST_CASE("formation: a contact beside us as we lift off is joining until it holds station") {
    Tracker tracker;
    tracker.observe_own(parked_at(0), 0);

    Report r{};
    uint32_t t = 1000;
    for (; t < 1000 + kTogetherHoldMs; t += 1000) {
        const model::OwnState own = airborne_at(t);
        tracker.observe_own(own, t);
        r = tracker.observe(own, neighbour(own, -20, -30, 0, 40, 90, t), {}, t);
        CHECK(r.state == State::Joining);
        CHECK(tracker.members() == 0);
    }
    const model::OwnState own = airborne_at(t);
    tracker.observe_own(own, t);
    CHECK(tracker.observe(own, neighbour(own, -20, -30, 0, 40, 90, t), {}, t).state ==
          State::Together);
}

// A tug lifts off after the glider it tows, so own-ship's liftoff is not the one that counts.
TEST_CASE("formation: a contact that lifts off beside us is joining") {
    Tracker tracker;
    const uint32_t t = 30000;
    const model::OwnState own = airborne_at(t);
    tracker.observe_own(own, t);

    const flight::Liftoff just_now{t, true};
    CHECK(tracker.observe(own, neighbour(own, 0, 60, 0, 40, 90, t), just_now, t).state ==
          State::Joining);
}

TEST_CASE("formation: a contact first heard in the air is traffic, not a joiner") {
    Tracker tracker;
    const uint32_t t = 30000;
    const model::OwnState own = airborne_at(t);
    tracker.observe_own(own, t);
    CHECK(tracker.observe(own, neighbour(own, 0, 60, 0, 40, 90, t), {}, t).state == State::None);

    Tracker later;
    later.observe_own(own, t);
    const flight::Liftoff long_ago{t - kLiftoffWindowMs, true};
    CHECK(later.observe(own, neighbour(own, 0, 60, 0, 40, 90, t), long_ago, t).state ==
          State::None);
}

TEST_CASE("formation: a joiner that leaves its station is traffic on that fix, and stays it") {
    Tracker tracker;
    uint32_t t = 30000;
    model::OwnState own = airborne_at(t);
    tracker.observe_own(own, t);
    const flight::Liftoff just_now{t, true};
    REQUIRE(tracker.observe(own, neighbour(own, 0, 60, 0, 40, 90, t), just_now, t).state ==
            State::Joining);

    t += 1000;
    own = airborne_at(t);
    CHECK(tracker.observe(own, neighbour(own, 0, 300, 0, 40, 90, t), just_now, t).state ==
          State::None);

    t += 1000;
    own = airborne_at(t);
    CHECK(tracker.observe(own, neighbour(own, 0, 300, 0, 40, 90, t), just_now, t).state ==
          State::None);
}

// An interval takeoff: the second aircraft catches up with the first, and that is a closure.
TEST_CASE("formation: a joiner closing on us is traffic after two fixes of it") {
    Tracker tracker;
    tracker.observe_own(parked_at(0), 0);

    uint32_t t = 1000;
    model::OwnState own = airborne_at(t);
    tracker.observe_own(own, t);
    REQUIRE(tracker.observe(own, neighbour(own, 0, -300, 0, 50, 90, t), {}, t).state ==
            State::Joining);

    t += 1000;
    own = airborne_at(t);
    CHECK(tracker.observe(own, neighbour(own, 0, -290, 0, 50, 90, t), {}, t).state ==
          State::Joining);

    t += 1000;
    own = airborne_at(t);
    CHECK(tracker.observe(own, neighbour(own, 0, -280, 0, 50, 90, t), {}, t).state == State::None);
}
