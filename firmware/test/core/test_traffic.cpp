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
    TrafficTable tbl;
    CHECK(tbl.count() == 0);
    int i = tbl.update(obs(0x111, 6, 100), 100);
    CHECK(i >= 0);
    CHECK(tbl.count() == 1);
    CHECK(tbl.find(6, 0x111) == i);
    CHECK(tbl.find(6, 0x222) == -1);
}

TEST_CASE("traffic: dedup merges the same target, fresher and direct win") {
    TrafficTable tbl;
    tbl.update(obs(0x111, 6, 100, model::Source::AdslUplink), 100);
    // direct, same time -> should replace uplink (direct preferred)
    tbl.update(obs(0x111, 6, 100, model::Source::AdslDirect), 100);
    CHECK(tbl.count() == 1);
    int idx = tbl.find(6, 0x111);
    CHECK(tbl.at(idx)->obs.source == model::Source::AdslDirect);
    // older observation must not overwrite a fresher one
    tbl.update(obs(0x111, 6, 90, model::Source::AdslUplink), 100);
    CHECK(tbl.at(idx)->obs.received.at_s == 100);
}

// A ground relay is a rebroadcast, so it is always the newer report and always
// the poorer one. Letting recency decide would walk a target we are hearing
// perfectly well backwards once a second, for as long as both paths last.
TEST_CASE("traffic: a relay does not displace a direct reception that is still fresh") {
    TrafficTable tbl;
    tbl.update(obs(0x111, 6, 100, model::Source::AdslDirect), 100);
    const int idx = tbl.find(6, 0x111);
    REQUIRE(idx >= 0);

    const uint32_t hold_s = direct_preferred_max_age_s(obs(0x111, 6, 100));
    for (uint32_t later = 101; later <= 100 + hold_s; later++) {
        tbl.update(obs(0x111, 6, later, model::Source::AdslUplink), later);
        CHECK(tbl.count() == 1);
        CHECK(tbl.at(idx)->obs.source == model::Source::AdslDirect);
        CHECK(tbl.at(idx)->obs.received.at_s == 100);
    }

    // And the hold is a hold, not a block: past it the direct track is as stale
    // as the alarm layer's own patience with a contact, and the relay is the
    // only thing still reporting this aircraft.
    const uint32_t past = 100 + hold_s + 1;
    tbl.update(obs(0x111, 6, past, model::Source::AdslUplink), past);
    CHECK(tbl.count() == 1);
    CHECK(tbl.at(idx)->obs.source == model::Source::AdslUplink);
    CHECK(tbl.at(idx)->obs.received.at_s == past);

    // A relay never blocks a target of its own, and a direct reception takes it
    // straight back.
    tbl.update(obs(0x222, 6, past, model::Source::AdslUplink), past);
    CHECK(tbl.count() == 2);
    tbl.update(obs(0x111, 6, past, model::Source::AdslDirect), past);
    CHECK(tbl.at(idx)->obs.source == model::Source::AdslDirect);
}

// The hold is core/traffic/alarm.h's own freshness rule wearing a different
// unit. If one moves, the other has to, and this is what says so.
TEST_CASE("traffic: the direct hold is the alarm layer's patience with a contact") {
    CHECK(direct_preferred_max_age_s(obs(0x111, 6, 100)) * 1000 == kAlertMaxAgeMs);
}

// At 0.1 Hz a relay taking over five seconds in replaces every ground report with a poorer copy.
TEST_CASE("traffic: a parked aircraft keeps its direct report until the next one is due") {
    CHECK(direct_preferred_max_age_s(parked(0x111, 6, 100)) == flight::kGroundReportPeriodS);

    TrafficTable tbl;
    tbl.update(parked(0x111, 6, 100), 100);
    const int idx = tbl.find(6, 0x111);
    REQUIRE(idx >= 0);

    tbl.update(parked(0x111, 6, 106, model::Source::AdslUplink), 106);
    CHECK(tbl.at(idx)->obs.source == model::Source::AdslDirect);

    tbl.update(parked(0x111, 6, 111, model::Source::AdslUplink), 111);
    CHECK(tbl.at(idx)->obs.source == model::Source::AdslUplink);
}

// A ground station relays every aircraft it heard, and it heard us. Own-ship on
// the radar is a permanent collision with the aircraft the device is bolted to.
TEST_CASE("traffic: our own address is not traffic, whoever reports it") {
    TrafficTable tbl;
    tbl.set_own_address(58, 0xC5D804);
    CHECK(tbl.update(obs(0xC5D804, 58, 100, model::Source::AdslUplink), 100) < 0);
    CHECK(tbl.update(obs(0xC5D804, 58, 100, model::Source::AdslDirect), 100) < 0);
    CHECK(tbl.count() == 0);
    CHECK(tbl.update(obs(0xC5D805, 58, 100, model::Source::AdslUplink), 100) >= 0);
    CHECK(tbl.count() == 1);
    // The same 24 bits under FLARM's table are another aircraft, and it is traffic.
    CHECK(tbl.update(obs(0xC5D804, 6, 100, model::Source::AdslDirect), 100) >= 0);
    CHECK(tbl.count() == 2);
}

TEST_CASE("traffic: age-out removes stale entries") {
    TrafficTable tbl;
    tbl.update(obs(0x1, 6, 100), 100);
    tbl.update(obs(0x2, 6, 120), 120);
    tbl.age_out(124);  // 0x1 is 24 s old -> gone; 0x2 is 4 s -> stays
    CHECK(tbl.find(6, 0x1) == -1);
    CHECK(tbl.find(6, 0x2) >= 0);
}

// Six 1 Hz bursts is a link that stopped rather than one that collided, and 360 m of lie at 60 m/s.
TEST_CASE("traffic: an airborne aircraft is drawn for six of its own bursts and no longer") {
    TrafficTable tbl;
    tbl.update(obs(0x1, 6, 100), 100);
    tbl.age_out(100 + kAirborneTargetForgetS);
    CHECK(tbl.find(6, 0x1) >= 0);
    tbl.age_out(100 + kAirborneTargetForgetS + 1);
    CHECK(tbl.find(6, 0x1) == -1);
}

// The same six reports at the rate G.1.16 gives a parked aircraft, where one miss was 10 s of 12.
TEST_CASE("traffic: a parked aircraft is drawn for six of its own bursts, which is a minute") {
    TrafficTable tbl;
    tbl.update(parked(0x1, 6, 100), 100);
    CHECK(kGroundTargetForgetS == 60);

    tbl.age_out(100 + kAirborneTargetForgetS + 1);
    CHECK(tbl.find(6, 0x1) >= 0);
    tbl.age_out(100 + kGroundTargetForgetS);
    CHECK(tbl.find(6, 0x1) >= 0);
    tbl.age_out(100 + kGroundTargetForgetS + 1);
    CHECK(tbl.find(6, 0x1) == -1);
}

// The report carries the lease, so a rotation is the burst the shorter one starts at.
TEST_CASE("traffic: a takeoff shortens the lease on the burst that announces it") {
    TrafficTable tbl;
    tbl.update(parked(0x1, 6, 100), 100);
    tbl.update(obs(0x1, 6, 110), 110);
    tbl.age_out(110 + kAirborneTargetForgetS + 1);
    CHECK(tbl.find(6, 0x1) == -1);
}

// A slot outliving the table holds a dismissal for an aeroplane off the screen.
TEST_CASE("traffic: the plot, the annunciator and the formation lose an aircraft together") {
    TrafficTable tbl;
    AlarmTracker tracker;
    formation::Tracker wingmen;

    model::AircraftObs wingman{};
    uint32_t heard_ms = 0;
    for (uint32_t t = 1000; t <= 1000 + formation::kTogetherHoldMs + 1000; t += 1000) {
        const model::OwnState own = flying(40, 90, 0, t);
        wingman = neighbour(own, -60, -120, 10, 40, 90, t);
        tbl.update(wingman, wingman.received.at_s);
        tracker.update(own, wingman, t);
        wingmen.observe(own, wingman, t);
        heard_ms = t;
    }
    tracker.dismiss();
    REQUIRE(wingmen.together(6, 0x314159));
    REQUIRE(tracker.dismissed());

    const uint32_t heard_s = wingman.received.at_s;
    const uint32_t lease_ms = forget_ms(wingman);
    tbl.age_out(heard_s + forget_s(wingman));
    tracker.forget_stale(heard_ms + lease_ms);
    wingmen.forget_stale(heard_ms + lease_ms);
    CHECK(tbl.find(6, 0x314159) >= 0);
    CHECK(tracker.dismissed());
    CHECK(wingmen.together(6, 0x314159));

    tbl.age_out(heard_s + forget_s(wingman) + 1);
    tracker.forget_stale(heard_ms + lease_ms + 1);
    wingmen.forget_stale(heard_ms + lease_ms + 1);
    CHECK(tbl.find(6, 0x314159) == -1);
    CHECK_FALSE(tracker.dismissed());
    CHECK_FALSE(wingmen.together(6, 0x314159));
}

// A relay names thirteen aircraft a frame, and a burst of them evicted one flying a kilometre away.
TEST_CASE("traffic: a full table keeps its nearest aircraft, whatever floods in farther away") {
    const model::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);
    const auto at = [&](uint32_t addr, int north_m, model::Source src, uint32_t t) {
        model::AircraftObs o = neighbour(own, north_m, 0, 0, 30, 0);
        o.addr = addr;
        o.source = src;
        o.received.at_s = t;
        return o;
    };
    REQUIRE(tbl.update(at(0xAAAAAA, 1000, model::Source::AdslDirect, 100), 100) >= 0);
    for (int i = 1; i < TrafficTable::kCapacity; i++)
        REQUIRE(tbl.update(at(0x100000u + i, 10000 + i * 100, model::Source::AdslUplink, 101),
                           101) >= 0);
    for (uint32_t i = 0; i < 20; i++)
        CHECK(tbl.update(at(0x200000u + i, 25000, model::Source::AdslUplink, 102), 102) < 0);
    CHECK(tbl.find(6, 0xAAAAAA) >= 0);

    // A newcomer nearer than the farthest takes that slot, and the nearest stays.
    CHECK(tbl.update(at(0x300000, 2000, model::Source::AdslUplink, 103), 103) >= 0);
    CHECK(tbl.find(6, 0x100000u + TrafficTable::kCapacity - 1) < 0);
    CHECK(tbl.find(6, 0xAAAAAA) >= 0);
}

TEST_CASE("traffic: overflow drops oldest non-threat, keeps active alarms") {
    TrafficTable tbl;
    // fill capacity
    for (int i = 0; i < TrafficTable::kCapacity; i++) {
        int idx = tbl.update(obs(0x1000 + i, 6, 100 + i), 200);
        REQUIRE(idx >= 0);
    }
    // mark the oldest entry as an active alarm so it can't be evicted
    int oldest = tbl.find(6, 0x1000);
    tbl.at(oldest)->alarm_level = Level::Advisory;
    int idx = tbl.update(obs(0x9999, 6, 300), 300);
    CHECK(idx >= 0);                  // newcomer placed
    CHECK(tbl.find(6, 0x1000) >= 0);  // protected alarm still present
}

// Nearest-first eviction refused an airborne arrival beyond a full apron of parked aircraft.
TEST_CASE("traffic: a table full of parked aircraft gives way to one in the air, however far") {
    const model::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);
    for (int i = 0; i < TrafficTable::kCapacity; i++) {
        model::AircraftObs apron = neighbour(own, 200 + 10 * i, 0, 0, 0, 0);
        apron.addr = 0x100000u + static_cast<uint32_t>(i);
        apron.received.at_s = 100;
        apron.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
        REQUIRE(tbl.update(apron, 100) >= 0);
    }

    model::AircraftObs arriving = neighbour(own, 8000, 0, 0, 40, 180);
    arriving.addr = 0x200000;
    arriving.received.at_s = 100;
    arriving.flight_state = static_cast<uint8_t>(flight::FlightState::Airborne);
    CHECK(tbl.update(arriving, 100) >= 0);
    CHECK(tbl.find(6, 0x200000) >= 0);
    CHECK(tbl.count() == TrafficTable::kCapacity);
}

// M. Two different clocks meet in this layer and only one of them wraps at
// 49.7 days. The table ages targets out on a SECONDS base (GNSS UTC when there is
// a fix, boot seconds when there is not) and the alarm tracker holds its own
// deadlines on ports::Clock::millis(). Both are unsigned differences, and these are
// the cases that keep them that way: a target must not be forgotten because the
// counter turned over, and a contact must not go unannounced for seven weeks.
TEST_CASE("traffic: the age-out is a difference, whichever side of the wrap the stamps fell") {
    TrafficTable tbl;
    // The seconds base a device with a UTC fix uses. 2^32 seconds is 136 years, so
    // the arithmetic below is the one that matters and it holds at any magnitude.
    const uint32_t utc = 0xFFFFFFF0u;
    tbl.update(obs(0x1, 6, utc), utc);
    tbl.update(obs(0x2, 6, utc + 20u), utc + 20u);
    // 5 s past the seconds counter's own end: the first is 25 s old and goes, the second is 5.
    const uint32_t later = utc + 25u;
    tbl.age_out(later);
    CHECK(tbl.find(6, 0x1) == -1);
    CHECK(tbl.find(6, 0x2) >= 0);
    // And the eviction order is ages, not stamps: a table full of targets stamped
    // before the wrap still gives up its oldest to a newcomer stamped after it.
    TrafficTable full;
    for (uint32_t i = 0; i < static_cast<uint32_t>(TrafficTable::kCapacity); i++)
        REQUIRE(full.update(obs(0x2000u + i, 6, utc + i), utc + i) >= 0);
    const uint32_t newcomer = utc + 60u;  // after every stamp already in the table
    CHECK(full.update(obs(0x9999, 6, newcomer), newcomer) >= 0);
    CHECK(full.find(6, 0x2000) == -1);  // the oldest, and only it
    CHECK(full.find(6, 0x2001) >= 0);
}

TEST_CASE("alarm: a contact is announced and forgotten across the 49.7-day wrap") {
    AlarmTracker tracker;
    const model::OwnState own = flying(30, 0);
    const uint32_t before = 0xFFFFF000u;  // 4096 ms short of the wrap

    // A contact announced on one side of the wrap instant, still standing on the other.
    model::AircraftObs target = neighbour(own, 400, 0, 0, 30, 180, before);
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
