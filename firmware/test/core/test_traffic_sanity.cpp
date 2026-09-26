// A position the CRC let through is refused when the radio could not have heard it from there.
#include <cmath>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/alarm.h"
#include "core/traffic/sanity.h"
#include "core/traffic/table.h"
#include "doctest/doctest.h"
#include "test/support/traffic_scene.h"

using namespace skyblip;
using namespace skyblip::traffic;

namespace {

// 32.44 + 20log10(f_MHz) + 20log10(d_km), the textbook free-space loss, in the
// test because no page reads it any more and the gate below is still sized by it.
double free_space_loss_db(int32_t range_m) {
    return 32.44 + 20.0 * std::log10(868.2) + 20.0 * std::log10(range_m / 1000.0);
}

}  // namespace

// Everything below the CRC has already passed. These cases are about what
// happens when the CRC was fooled: test/core/test_adsl.cpp counts silent
// miscorrections and the count is not zero, and a miscorrected position field
// decodes to a perfectly well-formed aircraft somewhere it cannot be.

TEST_CASE("traffic: a target further away than the radio can hear is a mis-decode") {
    const model::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);

    // Ten kilometres out is a real contact on this band: it is well inside the
    // budget and it is what the radar's outer ring is for.
    model::AircraftObs near_by = neighbour(own, 10000, 0, 0, 30, 180);
    near_by.addr = 0x4A0001;
    CHECK(tbl.update(near_by, 100) >= 0);

    // A hundred kilometres is not. Nothing at 14 dBm e.r.p. on 868 MHz reaches
    // this receiver from there, so the frame that said so was wrong.
    model::AircraftObs ghost = neighbour(own, 100000, 0, 0, 30, 180);
    ghost.addr = 0x4A0002;
    CHECK(tbl.update(ghost, 100) < 0);
    CHECK(tbl.count() == 1);
    CHECK(tbl.implausible_count() == 1);

    // Straight up counts too: the altitude field miscorrects as readily as the
    // latitude one, and the slant range is the path the signal took.
    model::AircraftObs high = neighbour(own, 0, 0, 60000, 0, 0);
    high.addr = 0x4A0003;
    CHECK(tbl.update(high, 100) < 0);
    CHECK(tbl.count() == 1);
    CHECK(tbl.implausible_count() == 2);
}

TEST_CASE("traffic: the plausibility gate is exact at its own boundary") {
    const model::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);
    int32_t slant_m = 0;

    // On the limit is believed, past it is not. The integer geometry rounds, so
    // the boundary is approached from both sides with a metre of margin rather
    // than asserted on the exact metre.
    model::AircraftObs on_limit = neighbour(own, kMaxPlausibleRangeM - 1, 0, 0, 30, 180);
    CHECK(range_check(own, on_limit, slant_m) == Plausibility::Believable);
    CHECK(slant_m <= kMaxPlausibleRangeM);

    model::AircraftObs past_limit = neighbour(own, kMaxPlausibleRangeM + 100, 0, 0, 30, 180);
    CHECK(range_check(own, past_limit, slant_m) == Plausibility::TooFar);
    CHECK(slant_m > kMaxPlausibleRangeM);

    // And the threshold is the link budget, not a taste: 14 dBm e.r.p. against
    // about -107 dBm of sensitivity is 121 dB, and free space at 868.2 MHz has
    // spent it by the gate. One ring in from the gate the budget still holds,
    // which is what makes this a ceiling rather than a limit on what we display.
    CHECK(free_space_loss_db(kMaxPlausibleRangeM) >= 120);
    CHECK(free_space_loss_db(kMaxPlausibleRangeM / 2) < 121);

    // Four times the outermost thing the alarm layer will speak about, so no
    // contact a pilot could act on is inside the part being refused.
    CHECK(kMaxPlausibleRangeM > 4 * kAdvisoryDistM);
}

// Without a fix there is no point to measure from. The gate says nothing rather
// than refusing everything: a device that has just booted still collects the
// traffic it hears, and the screen already knows it cannot place it.
TEST_CASE("traffic: with no fix of our own nothing is refused for being far away") {
    model::OwnState own = flying(30, 0);
    own.fix_valid = false;
    TrafficTable tbl;
    tbl.set_own_reference(own);

    model::AircraftObs far_away = neighbour(flying(30, 0), 100000, 0, 0, 30, 180);
    CHECK(tbl.update(far_away, 100) >= 0);
    CHECK(tbl.implausible_count() == 0);

    // Same for a report that carries no position at all: it is not a range claim,
    // so it is not this gate's business.
    int32_t slant_m = 0;
    model::AircraftObs positionless = far_away;
    positionless.position_valid = false;
    CHECK(range_check(flying(30, 0), positionless, slant_m) == Plausibility::NoReference);
}

// A table nobody told about own-ship gates nothing, which is the same behaviour
// as no fix: the gate is a refinement on the door, never a new way to be blind.
TEST_CASE("traffic: a table with no reference set behaves exactly as it did before") {
    TrafficTable tbl;
    model::AircraftObs far_away = neighbour(flying(30, 0), 250000, 0, 0, 30, 180);
    CHECK(tbl.update(far_away, 100) >= 0);
    CHECK(tbl.count() == 1);
    CHECK(tbl.implausible_count() == 0);
}

// The uplink is the case that makes the gate per-observation rather than
// per-frame: one ground-station frame carries up to thirteen aircraft, and one
// ghost among them says nothing about the other twelve.
TEST_CASE("traffic: a ghost in a relayed frame does not take the good targets with it") {
    const model::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);

    model::AircraftObs good = neighbour(own, 2000, 500, 100, 30, 90);
    good.addr = 0x4B0001;
    good.source = model::Source::AdslUplink;
    model::AircraftObs ghost = neighbour(own, 400000, -200000, 0, 30, 270);
    ghost.addr = 0x4B0002;
    ghost.source = model::Source::AdslUplink;
    model::AircraftObs also_good = neighbour(own, -1500, 800, -200, 25, 180);
    also_good.addr = 0x4B0003;
    also_good.source = model::Source::AdslUplink;

    CHECK(tbl.update(good, 100) >= 0);
    CHECK(tbl.update(ghost, 100) < 0);
    CHECK(tbl.update(also_good, 100) >= 0);
    CHECK(tbl.count() == 2);
    CHECK(tbl.find(6, 0x4B0001) >= 0);
    CHECK(tbl.find(6, 0x4B0002) == -1);
    CHECK(tbl.find(6, 0x4B0003) >= 0);
    CHECK(tbl.implausible_count() == 1);
}

// And a refused report never disturbs the aircraft it claims to be. The slot an
// aircraft holds is what the alarm layer is tracking, so a miscorrected frame
// carrying a known address must not move it, blank it or age it.
TEST_CASE("traffic: a mis-decode of a tracked aircraft does not move the aircraft") {
    const model::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);

    model::AircraftObs real_contact = neighbour(own, 1200, 0, 0, 30, 180, 100000);
    real_contact.addr = 0x4C0001;
    const int idx = tbl.update(real_contact, 100);
    REQUIRE(idx >= 0);
    tbl.at(idx)->alarm_level = Level::Advisory;

    model::AircraftObs same_aircraft_wrong_place = neighbour(own, 120000, 0, 0, 30, 180, 101000);
    same_aircraft_wrong_place.addr = 0x4C0001;
    CHECK(tbl.update(same_aircraft_wrong_place, 101) < 0);

    CHECK(tbl.count() == 1);
    CHECK(tbl.find(6, 0x4C0001) == idx);
    CHECK(tbl.at(idx)->obs.lat_1e7 == real_contact.lat_1e7);
    CHECK(tbl.at(idx)->obs.received.at_s == real_contact.received.at_s);
    CHECK(tbl.at(idx)->alarm_level == Level::Advisory);
}

// A relayed target crossed two links, so it is allowed to be further away than
// anything we could have heard for ourselves - and that is the whole point of the
// uplink. The same position from the same aircraft is a ghost on the direct path
// and a legitimate contact on the relayed one.
TEST_CASE("traffic: a relayed target is judged against the two hops it travelled") {
    const model::OwnState own = flying(30, 0);
    TrafficTable tbl;
    tbl.set_own_reference(own);
    int32_t slant_m = 0;

    model::AircraftObs distant = neighbour(own, 45000, 0, 0, 30, 180);
    distant.addr = 0x4D0001;
    distant.source = model::Source::AdslDirect;
    CHECK(range_check(own, distant, slant_m) == Plausibility::TooFar);
    CHECK(tbl.update(distant, 100) < 0);

    distant.source = model::Source::AdslUplink;
    CHECK(range_check(own, distant, slant_m) == Plausibility::Believable);
    CHECK(tbl.update(distant, 100) >= 0);
    CHECK(tbl.count() == 1);

    // ALP-TAS is a direct reception like our own protocol: one hop, one ceiling.
    CHECK(plausible_range_m(model::Source::Alptas) == kMaxPlausibleRangeM);
    CHECK(plausible_range_m(model::Source::AdslDirect) == kMaxPlausibleRangeM);
    CHECK(plausible_range_m(model::Source::AdslUplink) == kMaxRelayedRangeM);

    // Past the two-hop budget a relay is refused too: a ground station cannot
    // hand us an aircraft it could not have heard either.
    model::AircraftObs relayed_ghost = neighbour(own, 200000, 0, 0, 30, 180);
    relayed_ghost.addr = 0x4D0002;
    relayed_ghost.source = model::Source::AdslUplink;
    CHECK(tbl.update(relayed_ghost, 100) < 0);
    CHECK(tbl.count() == 1);
    CHECK(tbl.implausible_count() == 2);
}
