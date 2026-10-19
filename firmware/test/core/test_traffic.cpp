// The table is finite and the sky is not, so every entry that arrives asks which
// one leaves. That is a safety decision, not bookkeeping: an aircraft under alarm
// stays even when the table overflows, a second report of the same aircraft merges
// rather than doubles it, and a stale entry ages out instead of haunting the
// screen.
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/alarm.h"
#include "core/traffic/formation.h"
#include "core/traffic/lease.h"
#include "core/traffic/table.h"
#include "doctest/doctest.h"
#include "test/support/traffic_scene.h"

using namespace skyblip;
using namespace skyblip::traffic;

TEST_CASE("traffic: insert, find, count") {
    TrafficTable table;
    CHECK(table.count() == 0);
    int i = table.update(observation(0x111, 6, 100), 100);
    CHECK(i >= 0);
    CHECK(table.count() == 1);
    CHECK(table.find(6, 0x111) == i);
    CHECK(table.find(6, 0x222) == -1);
}

TEST_CASE("traffic: dedup merges the same target, fresher and direct win") {
    TrafficTable table;
    table.update(observation(0x111, 6, 100, model::Source::AdslUplink), 100);
    // direct, same time -> should replace uplink (direct preferred)
    table.update(observation(0x111, 6, 100, model::Source::AdslDirect), 100);
    CHECK(table.count() == 1);
    int index = table.find(6, 0x111);
    CHECK(table.at(index)->observation.source == model::Source::AdslDirect);
    // older observation must not overwrite a fresher one
    table.update(observation(0x111, 6, 90, model::Source::AdslUplink), 100);
    CHECK(table.at(index)->observation.received.at_s == 100);
}

// A ground relay is a rebroadcast, so it is always the newer report and always
// the poorer one. Letting recency decide would walk a target we are hearing
// perfectly well backwards once a second, for as long as both paths last.
TEST_CASE("traffic: a relay does not displace a direct reception that is still fresh") {
    TrafficTable table;
    table.update(observation(0x111, 6, 100, model::Source::AdslDirect), 100);
    const int index = table.find(6, 0x111);
    REQUIRE(index >= 0);

    const uint32_t hold_s = direct_preferred_max_age_s(observation(0x111, 6, 100));
    for (uint32_t later = 101; later <= 100 + hold_s; later++) {
        table.update(observation(0x111, 6, later, model::Source::AdslUplink), later);
        CHECK(table.count() == 1);
        CHECK(table.at(index)->observation.source == model::Source::AdslDirect);
        CHECK(table.at(index)->observation.received.at_s == 100);
    }

    // And the hold is a hold, not a block: past it the direct track is as stale
    // as the alarm layer's own patience with a target, and the relay is the
    // only thing still reporting this aircraft.
    const uint32_t past = 100 + hold_s + 1;
    table.update(observation(0x111, 6, past, model::Source::AdslUplink), past);
    CHECK(table.count() == 1);
    CHECK(table.at(index)->observation.source == model::Source::AdslUplink);
    CHECK(table.at(index)->observation.received.at_s == past);

    // A relay never blocks a target of its own, and a direct reception takes it
    // straight back.
    table.update(observation(0x222, 6, past, model::Source::AdslUplink), past);
    CHECK(table.count() == 2);
    table.update(observation(0x111, 6, past, model::Source::AdslDirect), past);
    CHECK(table.at(index)->observation.source == model::Source::AdslDirect);
}

// The hold is core/traffic/alarm.h's own freshness rule wearing a different
// unit. If one moves, the other has to, and this is what says so.
TEST_CASE("traffic: the direct hold is the alarm layer's patience with a target") {
    CHECK(direct_preferred_max_age_s(observation(0x111, 6, 100)) * 1000 == kAlertMaxAgeMs);
}

// At 0.1 Hz a relay taking over five seconds in replaces every ground report with a poorer copy.
TEST_CASE("traffic: a parked aircraft keeps its direct report until the next one is due") {
    CHECK(direct_preferred_max_age_s(parked(0x111, 6, 100)) == flight::kGroundReportPeriodS);

    TrafficTable table;
    table.update(parked(0x111, 6, 100), 100);
    const int index = table.find(6, 0x111);
    REQUIRE(index >= 0);

    table.update(parked(0x111, 6, 106, model::Source::AdslUplink), 106);
    CHECK(table.at(index)->observation.source == model::Source::AdslDirect);

    table.update(parked(0x111, 6, 111, model::Source::AdslUplink), 111);
    CHECK(table.at(index)->observation.source == model::Source::AdslUplink);
}

// A ground station relays every aircraft it heard, and it heard us. Own-ship on
// the radar is a permanent collision with the aircraft the device is bolted to.
TEST_CASE("traffic: our own address is not traffic, whoever reports it") {
    TrafficTable table;
    table.set_own_address(58, 0xC5D804);
    CHECK(table.update(observation(0xC5D804, 58, 100, model::Source::AdslUplink), 100) < 0);
    CHECK(table.update(observation(0xC5D804, 58, 100, model::Source::AdslDirect), 100) < 0);
    CHECK(table.count() == 0);
    CHECK(table.update(observation(0xC5D805, 58, 100, model::Source::AdslUplink), 100) >= 0);
    CHECK(table.count() == 1);
    // The same 24 bits under FLARM's table are another aircraft, and it is traffic.
    CHECK(table.update(observation(0xC5D804, 6, 100, model::Source::AdslDirect), 100) >= 0);
    CHECK(table.count() == 2);
}

TEST_CASE("traffic: age-out removes stale entries") {
    TrafficTable table;
    table.update(observation(0x1, 6, 100), 100);
    table.update(observation(0x2, 6, 120), 120);
    table.age_out(124);  // 0x1 is 24 s old -> gone; 0x2 is 4 s -> stays
    CHECK(table.find(6, 0x1) == -1);
    CHECK(table.find(6, 0x2) >= 0);
}

// Six 1 Hz bursts is a link that stopped rather than one that collided, and 360 m of lie at 60 m/s.
TEST_CASE("traffic: an airborne aircraft is drawn for six of its own bursts and no longer") {
    TrafficTable table;
    table.update(observation(0x1, 6, 100), 100);
    table.age_out(100 + kAirborneTargetForgetS);
    CHECK(table.find(6, 0x1) >= 0);
    table.age_out(100 + kAirborneTargetForgetS + 1);
    CHECK(table.find(6, 0x1) == -1);
}

// The same six reports at the rate G.1.16 gives a parked aircraft, where one miss was 10 s of 12.
TEST_CASE("traffic: a parked aircraft is drawn for six of its own bursts, which is a minute") {
    TrafficTable table;
    table.update(parked(0x1, 6, 100), 100);
    CHECK(kGroundTargetForgetS == 60);

    table.age_out(100 + kAirborneTargetForgetS + 1);
    CHECK(table.find(6, 0x1) >= 0);
    table.age_out(100 + kGroundTargetForgetS);
    CHECK(table.find(6, 0x1) >= 0);
    table.age_out(100 + kGroundTargetForgetS + 1);
    CHECK(table.find(6, 0x1) == -1);
}

// The report carries the lease, so a rotation is the burst the shorter one starts at.
TEST_CASE("traffic: a takeoff shortens the lease on the burst that announces it") {
    TrafficTable table;
    table.update(parked(0x1, 6, 100), 100);
    table.update(observation(0x1, 6, 110), 110);
    table.age_out(110 + kAirborneTargetForgetS + 1);
    CHECK(table.find(6, 0x1) == -1);
}

// A slot outliving the table holds a dismissal for an aeroplane off the screen.
TEST_CASE("traffic: the plot, the annunciator and the formation lose an aircraft together") {
    TrafficTable table;
    AlarmTracker tracker;
    formation::Tracker wingmen;

    model::AircraftObservation wingman{};
    uint32_t heard_ms = 0;
    for (uint32_t now_ms = 1000; now_ms <= 1000 + formation::kTogetherHoldMs + 1000;
         now_ms += 1000) {
        const model::Ownship own = flying(40, 90, 0, now_ms);
        wingman = neighbour(own, -60, -120, 10, 40, 90, now_ms);
        table.update(wingman, wingman.received.at_s);
        tracker.update(own, wingman, now_ms);
        wingmen.observe(own, wingman, {}, now_ms);
        heard_ms = now_ms;
    }
    tracker.dismiss();
    REQUIRE(wingmen.together(6, 0x314159));
    REQUIRE(tracker.dismissed());

    const uint32_t heard_s = wingman.received.at_s;
    const uint32_t lease_ms = forget_ms(wingman);
    table.age_out(heard_s + forget_s(wingman));
    tracker.forget_stale(heard_ms + lease_ms);
    wingmen.forget_stale(heard_ms + lease_ms);
    CHECK(table.find(6, 0x314159) >= 0);
    CHECK(tracker.dismissed());
    CHECK(wingmen.together(6, 0x314159));

    table.age_out(heard_s + forget_s(wingman) + 1);
    tracker.forget_stale(heard_ms + lease_ms + 1);
    wingmen.forget_stale(heard_ms + lease_ms + 1);
    CHECK(table.find(6, 0x314159) == -1);
    CHECK_FALSE(tracker.dismissed());
    CHECK_FALSE(wingmen.together(6, 0x314159));
}

// A relay names thirteen aircraft a frame, and a burst of them evicted one flying a kilometre away.
TEST_CASE("traffic: a full table keeps its nearest aircraft, whatever floods in farther away") {
    const model::Ownship own = flying(30, 0);
    TrafficTable table;
    table.set_own_reference(own);
    const auto at = [&](uint32_t address, int north_m, model::Source source, uint32_t now_ms) {
        model::AircraftObservation observation = neighbour(own, north_m, 0, 0, 30, 0);
        observation.address = address;
        observation.source = source;
        observation.received.at_s = now_ms;
        return observation;
    };
    REQUIRE(table.update(at(0xAAAAAA, 1000, model::Source::AdslDirect, 100), 100) >= 0);
    for (int i = 1; i < TrafficTable::kMaxTargets; i++)
        REQUIRE(table.update(at(0x100000u + i, 10000 + i * 100, model::Source::AdslUplink, 101),
                             101) >= 0);
    for (uint32_t i = 0; i < 20; i++)
        CHECK(table.update(at(0x200000u + i, 25000, model::Source::AdslUplink, 102), 102) < 0);
    CHECK(table.find(6, 0xAAAAAA) >= 0);

    // A newcomer nearer than the farthest takes that slot, and the nearest stays.
    CHECK(table.update(at(0x300000, 2000, model::Source::AdslUplink, 103), 103) >= 0);
    CHECK(table.find(6, 0x100000u + TrafficTable::kMaxTargets - 1) < 0);
    CHECK(table.find(6, 0xAAAAAA) >= 0);
}

TEST_CASE("traffic: overflow drops oldest non-threat, keeps active alarms") {
    TrafficTable table;
    // fill capacity
    for (int i = 0; i < TrafficTable::kMaxTargets; i++) {
        int index = table.update(observation(0x1000 + i, 6, 100 + i), 200);
        REQUIRE(index >= 0);
    }
    // mark the oldest entry as an active alarm so it can't be evicted
    int oldest = table.find(6, 0x1000);
    table.at(oldest)->alarm_level = Level::Advisory;
    int index = table.update(observation(0x9999, 6, 300), 300);
    CHECK(index >= 0);                  // newcomer placed
    CHECK(table.find(6, 0x1000) >= 0);  // protected alarm still present
}

// The radar counts a slot by its formation flag, so a newcomer must not inherit a departed
// wingman's.
TEST_CASE("traffic: a slot handed to a new aircraft keeps nothing of the last one's standing") {
    const auto standing = [](TrafficTable& table, int index) {
        table.at(index)->in_formation = true;
        table.at(index)->alarm_dismissed = true;
    };
    const auto fresh = [](const TrafficTable& table, int index) {
        return index >= 0 && !table.at(index)->in_formation && !table.at(index)->alarm_dismissed;
    };

    TrafficTable full;
    for (int i = 0; i < TrafficTable::kMaxTargets; i++)
        standing(full, full.update(observation(0x1000 + i, 6, 100 + i), 200));
    CHECK(fresh(full, full.update(observation(0x9999, 6, 300), 300)));

    TrafficTable aged;
    standing(aged, aged.update(observation(0x1, 6, 100), 100));
    aged.age_out(100 + kAirborneTargetForgetS + 1);
    REQUIRE(aged.count() == 0);
    CHECK(fresh(aged, aged.update(observation(0x2, 6, 120), 120)));

    TrafficTable cleared;
    standing(cleared, cleared.update(observation(0x1, 6, 100), 100));
    cleared.clear();
    CHECK(fresh(cleared, cleared.update(observation(0x2, 6, 100), 100)));
}

// Nearest-first eviction refused an airborne arrival beyond a full apron of parked aircraft.
TEST_CASE("traffic: a table full of parked aircraft gives way to one in the air, however far") {
    const model::Ownship own = flying(30, 0);
    TrafficTable table;
    table.set_own_reference(own);
    for (int i = 0; i < TrafficTable::kMaxTargets; i++) {
        model::AircraftObservation apron = neighbour(own, 200 + 10 * i, 0, 0, 0, 0);
        apron.address = 0x100000u + static_cast<uint32_t>(i);
        apron.received.at_s = 100;
        apron.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
        REQUIRE(table.update(apron, 100) >= 0);
    }

    model::AircraftObservation arriving = neighbour(own, 8000, 0, 0, 40, 180);
    arriving.address = 0x200000;
    arriving.received.at_s = 100;
    arriving.flight_state = static_cast<uint8_t>(flight::FlightState::Airborne);
    CHECK(table.update(arriving, 100) >= 0);
    CHECK(table.find(6, 0x200000) >= 0);
    CHECK(table.count() == TrafficTable::kMaxTargets);
}

// M. Two different clocks meet in this layer and only one of them wraps at
// 49.7 days. The table ages targets out on a SECONDS base (GNSS UTC when there is
// a fix, boot seconds when there is not) and the alarm tracker holds its own
// deadlines on ports::Clock::milliseconds(). Both are unsigned differences, and these are
// the cases that keep them that way: a target must not be forgotten because the
// counter turned over, and a target must not go unannounced for seven weeks.
TEST_CASE("traffic: the age-out is a difference, whichever side of the wrap the stamps fell") {
    TrafficTable table;
    // The seconds base a device with a UTC fix uses. 2^32 seconds is 136 years, so
    // the arithmetic below is the one that matters and it holds at any magnitude.
    const uint32_t utc_s = 0xFFFFFFF0u;
    table.update(observation(0x1, 6, utc_s), utc_s);
    table.update(observation(0x2, 6, utc_s + 20u), utc_s + 20u);
    // 5 s past the seconds counter's own end: the first is 25 s old and goes, the second is 5.
    const uint32_t later = utc_s + 25u;
    table.age_out(later);
    CHECK(table.find(6, 0x1) == -1);
    CHECK(table.find(6, 0x2) >= 0);
    // And the eviction order is ages, not stamps: a table full of targets stamped
    // before the wrap still gives up its oldest to a newcomer stamped after it.
    TrafficTable full;
    for (uint32_t i = 0; i < static_cast<uint32_t>(TrafficTable::kMaxTargets); i++)
        REQUIRE(full.update(observation(0x2000u + i, 6, utc_s + i), utc_s + i) >= 0);
    const uint32_t newcomer = utc_s + 60u;  // after every stamp already in the table
    CHECK(full.update(observation(0x9999, 6, newcomer), newcomer) >= 0);
    CHECK(full.find(6, 0x2000) == -1);  // the oldest, and only it
    CHECK(full.find(6, 0x2001) >= 0);
}

TEST_CASE("alarm: a target is announced and forgotten across the 49.7-day wrap") {
    AlarmTracker tracker;
    const model::Ownship own = flying(30, 0);
    const uint32_t before = 0xFFFFF000u;  // 4096 ms short of the wrap

    // A target announced on one side of the wrap instant, still standing on the other.
    model::AircraftObservation target = neighbour(own, 400, 0, 0, 30, 180, before);
    REQUIRE(tracker.update(own, target, before).notify);
    CHECK(tracker.announced_level(before) == Level::Advisory);

    // 3000 ms later, past the wrap. Still fresh (kAlertMaxAgeMs is 5000), so the
    // level still stands - an announced_level that read 0 here would be a buzzer
    // that stopped mid-alarm at the wrap.
    const uint32_t after = before + 3000u;
    target.received.at_s += 3;  // a new observation of the same aircraft
    tracker.update(own, target, after);
    CHECK(tracker.announced_level(after) == Level::Advisory);

    // Past the alert age with nothing new heard: no longer driving the annunciator.
    CHECK(tracker.announced_level(after + kAlertMaxAgeMs + 1u) == Level::None);
    // And past the lease the slot is released, so the next aircraft can have it.
    const uint32_t lease_ms = kAirborneTargetForgetS * 1000u;
    tracker.forget_stale(after + lease_ms + 1u);
    CHECK(tracker.announced_level(after + lease_ms + 1u) == Level::None);
}
