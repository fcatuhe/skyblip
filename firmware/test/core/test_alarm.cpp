// What makes an aircraft an advisory, and what the alarm is allowed to say out loud about a
// target it has already announced.
#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/alarm.h"
#include "core/traffic/lease.h"
#include "core/traffic/sanity.h"
#include "doctest/doctest.h"
#include "test/support/traffic_scene.h"

using namespace skyblip;
using namespace skyblip::traffic;

// The advisory is a place, not a prediction: inside 3 km and 300 m, an aircraft
// is one whatever it is doing, and outside it is none however fast it closes.
TEST_CASE("alarm: an aircraft inside three kilometres and three hundred metres is an advisory") {
    const model::OwnState own = flying(40, 0);

    CHECK(assess(own, neighbour(own, 2800, 0, 0, 40, 180), 0).level == Level::Advisory);
    CHECK(assess(own, neighbour(own, 3200, 0, 0, 40, 180), 0).level == Level::None);
    CHECK(assess(own, neighbour(own, 1500, 0, 250, 40, 180), 0).level == Level::Advisory);
    CHECK(assess(own, neighbour(own, 1500, 0, 350, 40, 180), 0).level == Level::None);
}

// A circuit flown over a full apron is a circuit of advisories, and G.1.2 says which those are.
TEST_CASE("alarm: an aircraft that says it is on the ground is a contact and never an advisory") {
    const model::OwnState own = flying(40, 0);

    model::AircraftObs apron = neighbour(own, 400, 0, 0, 0, 0);
    CHECK(assess(own, apron, 0).level == Level::Advisory);

    apron.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
    const AlarmAssessment graded = assess(own, apron, 0);
    CHECK(graded.level == Level::None);
    CHECK(graded.valid);
    CHECK(graded.rel_dist_m == doctest::Approx(400).epsilon(0.02));
}

TEST_CASE("alarm: a neighbour that sends no altitude is ranged on the ground, at our level") {
    const model::OwnState own = flying(30, 0);
    model::AircraftObs no_altitude = neighbour(own, 55, 0, 0, 30, 180);
    no_altitude.alt_valid = false;
    no_altitude.alt_m = 61116;
    int32_t slant_m = 0;
    CHECK(range_check(own, no_altitude, slant_m) == Plausibility::Believable);
    CHECK(slant_m == doctest::Approx(55).epsilon(0.05));
    CHECK(assess(own, no_altitude, 0).level == Level::Advisory);
}

TEST_CASE("alarm: an aircraft leaving is as much an advisory as one arriving") {
    const model::OwnState own = flying(30, 0);

    CHECK(assess(own, neighbour(own, 400, 0, 0, 30, 0), 0).level == Level::Advisory);
    CHECK(assess(own, neighbour(own, -300, 0, 0, 40, 0), 0).level == Level::Advisory);
    CHECK(assess(own, neighbour(own, 5000, 0, 0, 50, 180), 0).level == Level::None);
}

TEST_CASE("alarm: invalid when own has no fix") {
    model::OwnState own{};
    model::AircraftObs t{};
    t.position_valid = true;
    CHECK_FALSE(assess(own, t, 0).valid);
}

// The pinned bug: a fix and a report from different instants were subtracted as if they were one.
TEST_CASE("alarm: both sides are carried to the instant the geometry is read at") {
    const model::OwnState own = flying(30, 0, 0, 10'000);
    const model::AircraftObs head_on = neighbour(own, 1000, 0, 0, 30, 180, 10'000);

    CHECK(assess(own, head_on, 10'000).rel_dist_m == doctest::Approx(1000).epsilon(0.02));

    // A second on, with nothing heard since: 30 m/s each, nose to nose, 60 m of it gone.
    CHECK(assess(own, head_on, 11'000).rel_dist_m == doctest::Approx(940).epsilon(0.02));

    // Past the model's reach, both stand where they were last known to be.
    const uint32_t too_late = 10'000 + flight::kMaxExtrapolationMs + 1;
    CHECK(assess(own, head_on, too_late).rel_dist_m == doctest::Approx(1000).epsilon(0.02));
}

// The bug this replaced added both speeds together whatever the geometry, so a
// neighbour running away from us was credited with everything it had. The
// formation layer reads this figure, and a gaggle drifting downwind together
// must not read as closure.
TEST_CASE("alarm: closing speed is the relative velocity on the line of sight") {
    const model::OwnState own = flying(30, 0);

    // Ahead of us, going the same way at the same speed: the gap is not moving.
    const AlarmAssessment formation = assess(own, neighbour(own, 800, 0, 0, 30, 0), 0);
    CHECK(formation.closing_mps == 0);

    // The same target turned around: both speeds, because both are spent on us.
    const AlarmAssessment head_on = assess(own, neighbour(own, 800, 0, 0, 30, 180), 0);
    CHECK(head_on.closing_mps == doctest::Approx(60).epsilon(0.05));

    // Behind us and slower: the gap is opening at the difference.
    const AlarmAssessment overtaken = assess(own, neighbour(own, -800, 0, 0, 20, 0), 0);
    CHECK(overtaken.closing_mps == doctest::Approx(-10).epsilon(0.15));

    // Abeam, flying parallel: nothing of that 30 m/s is aimed at us.
    const AlarmAssessment abeam = assess(own, neighbour(own, 0, 600, 0, 30, 0), 0);
    CHECK(abeam.closing_mps == 0);
}

// Uplinked and relayed traffic often arrives as a position with no velocity.
// Zero would make it the safest thing in the sky, which is a lie the closing
// figure is not allowed to tell: what is unknown is charged at what these
// aircraft fly.
TEST_CASE("alarm: a target that reports no velocity degrades, it does not vanish") {
    const model::OwnState own = flying(30, 0);
    model::AircraftObs quiet = neighbour(own, 900, 0, 0, 0, 0);
    quiet.speed_valid = false;

    const AlarmAssessment a = assess(own, quiet, 0);
    CHECK(a.closing_mps >= 30 + kUnknownTargetSpeedMps - 1);
    CHECK(a.level == Level::Advisory);
}

// The annunciator is not the alarm level: a target already announced must not
// re-drive it every pass of the service loop. SoftRF keeps one notification per
// address (oss/SoftRF-lyusupov .../src/TrafficHelper.cpp:236-260).
TEST_CASE("alarm: an aircraft is announced once, and not again while it stands") {
    AlarmTracker tracker;
    const model::OwnState own = flying(30, 0);

    uint32_t t = 1000;
    AlarmTracker::Decision d = tracker.update(own, neighbour(own, 2800, 0, 0, 20, 180), t);
    REQUIRE(d.assessment.level == Level::Advisory);
    CHECK(d.notify);

    for (int i = 0; i < 50; i++) {
        t += 100;
        d = tracker.update(own, neighbour(own, 1200, 0, 0, 20, 180, t), t);
        REQUIRE(d.assessment.level == Level::Advisory);
        CHECK_FALSE(d.notify);
    }
    CHECK(tracker.announced_level(t) == Level::Advisory);
}

// A contact sitting on the boundary is one aircraft, not an alarm every second.
TEST_CASE("alarm: an aircraft is announced again only after it has been outside for a window") {
    AlarmTracker tracker;
    const model::OwnState own = flying(30, 0);

    uint32_t t = 1000;
    REQUIRE(tracker.update(own, neighbour(own, 2800, 0, 0, 20, 180, t), t).notify);

    // Out, and straight back in inside the re-notification window: said once.
    t += 500;
    CHECK_FALSE(tracker.update(own, neighbour(own, 3400, 0, 0, 20, 0, t), t).notify);
    t += 500;
    CHECK_FALSE(tracker.update(own, neighbour(own, 2800, 0, 0, 20, 180, t), t).notify);

    // Out for longer than the window, and the next entry is a new aircraft to the ear.
    for (int i = 0; i < 25; i++) {
        t += 100;
        tracker.update(own, neighbour(own, 3400, 0, 0, 20, 0, t), t);
    }
    CHECK(tracker.update(own, neighbour(own, 2800, 0, 0, 20, 180, t), t).notify);
}

// What a pilot with the aircraft in sight dismisses is what has already been said.
TEST_CASE("alarm: a dismissed contact stops re-announcing itself") {
    AlarmTracker tracker;
    const model::OwnState own = flying(30, 0);

    uint32_t t = 1000;
    REQUIRE(tracker.update(own, neighbour(own, 400, 0, 0, 30, 180, t), t).notify);
    tracker.dismiss();

    for (int i = 0; i < 50; i++) {
        t += 100;
        CHECK_FALSE(tracker.update(own, neighbour(own, 400, 0, 0, 30, 180, t), t).notify);
    }
    CHECK(tracker.dismissed());
    CHECK(tracker.announced_level(t) == Level::None);
}

// One gesture covers the sky the pilot has looked at, not the aeroplane behind them.
TEST_CASE("alarm: a dismissal is spent per aircraft, and a newcomer keeps its own voice") {
    AlarmTracker tracker;
    const model::OwnState own = flying(30, 0);

    uint32_t t = 1000;
    REQUIRE(tracker.update(own, neighbour(own, 400, 0, 0, 30, 180, t), t).notify);
    tracker.dismiss();

    t += 100;
    model::AircraftObs stranger = neighbour(own, 0, 400, 0, 30, 270, t);
    stranger.addr = 0x271828;
    CHECK(tracker.update(own, stranger, t).notify);

    // The aircraft already seen stays silent through the newcomer's alarm.
    CHECK(tracker.update(own, neighbour(own, 400, 0, 0, 30, 180, t), t).dismissed);
    CHECK_FALSE(tracker.update(own, neighbour(own, 400, 0, 0, 30, 180, t), t).notify);
}

TEST_CASE("alarm: an aircraft that comes back takes the dismissal back") {
    AlarmTracker tracker;
    const model::OwnState own = flying(30, 0);

    uint32_t t = 1000;
    REQUIRE(tracker.update(own, neighbour(own, 2800, 0, 0, 20, 180, t), t).notify);
    tracker.dismiss();

    for (int i = 0; i < 25; i++) {
        t += 100;
        tracker.update(own, neighbour(own, 3400, 0, 0, 20, 0, t), t);
    }

    t += 100;
    const AlarmTracker::Decision d = tracker.update(own, neighbour(own, 2800, 0, 0, 20, 180, t), t);
    CHECK(d.notify);
    CHECK_FALSE(tracker.dismissed());
}

TEST_CASE("alarm: a dismissal is spent on the sky that earned it") {
    AlarmTracker tracker;
    const model::OwnState own = flying(30, 0);

    uint32_t t = 1000;
    REQUIRE(tracker.update(own, neighbour(own, 400, 0, 0, 30, 180, t), t).notify);
    tracker.dismiss();

    t += 100;
    model::AircraftObs stranger = neighbour(own, 0, 400, 0, 30, 270, t);
    stranger.addr = 0x271828;
    const AlarmTracker::Decision d = tracker.update(own, stranger, t);
    CHECK(d.notify);
    CHECK_FALSE(tracker.dismissed());
}

// A target we have not heard from is a memory, not a threat: it may have turned,
// landed or switched off. SoftRF alerts only inside ALERT_EXPIRATION_TIME.
TEST_CASE("alarm: a target that has gone quiet stops driving the annunciator") {
    AlarmTracker tracker;
    const model::OwnState own = flying(30, 0);
    const model::AircraftObs frozen = neighbour(own, 400, 0, 0, 30, 180, 1000);

    uint32_t t = 1000;
    REQUIRE(tracker.update(own, frozen, t).notify);
    CHECK(tracker.announced_level(t) == Level::Advisory);

    CHECK(tracker.announced_level(t + kAlertMaxAgeMs + 1) == Level::None);
}

// A recycled slot came back empty and read as a first sighting, so the buzzer spoke.
TEST_CASE("alarm: a report older than the alert window is not announced on a recycled slot") {
    AlarmTracker tracker;
    const model::OwnState own = flying(30, 0);
    const model::AircraftObs target = neighbour(own, 400, 0, 0, 30, 180, 1000);
    REQUIRE(tracker.update(own, target, 1000).notify);

    const uint32_t forgotten = 1000 + kAirborneTargetForgetS * 1000 + 1;
    tracker.forget_stale(forgotten);
    CHECK_FALSE(tracker.update(own, target, forgotten).notify);
}

// What the buzzer follows. notify says "say it now"; this says "and this is
// what still stands", which is the difference between a tone with a cadence and
// a tone nobody remembers to stop.
TEST_CASE("alarm: the announcement stands while the contact does, and falls a window after") {
    AlarmTracker tracker;
    const model::OwnState own = flying(30, 0);

    uint32_t t = 1000;
    REQUIRE(tracker.update(own, neighbour(own, 400, 0, 0, 30, 180, t), t).assessment.level ==
            Level::Advisory);
    CHECK(tracker.announced_level(t) == Level::Advisory);

    // Outside the window, and the tracker holds what it said for a
    // re-notification window, so a target on the boundary is not a stutter...
    t += 100;
    REQUIRE(tracker.update(own, neighbour(own, 3400, 0, 0, 20, 0, t), t).assessment.level ==
            Level::None);
    CHECK(tracker.announced_level(t) == Level::Advisory);

    // ...and then it lets go, which is the moment the tone must stop.
    for (int i = 0; i < 25; i++) {
        t += 100;
        tracker.update(own, neighbour(own, 3400, 0, 0, 20, 0, t), t);
    }
    CHECK(tracker.announced_level(t) == Level::None);
}
