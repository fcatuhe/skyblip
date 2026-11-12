// ADS-B at the table's door: its rank among the other paths, our own transponder, and no gate.
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/traffic/alarm.h"
#include "core/traffic/lease.h"
#include "core/traffic/table.h"
#include "doctest/doctest.h"
#include "test/support/traffic_scene.h"

using namespace skyblip;
using namespace skyblip::traffic;

namespace {

constexpr uint8_t kIcao = settings::kIcaoAddressMappingTable;

model::AircraftObservation heard(const model::Ownship& own, uint32_t address, int north_m, int up_m,
                                 model::Source source, uint32_t at_s = 100) {
    model::AircraftObservation observation = target_at(own, north_m, 0, up_m, 60, 180);
    observation.address = address;
    observation.address_mapping_table = source == model::Source::Adsb ? kIcao : 6;
    observation.source = source;
    observation.received.at_s = at_s;
    return observation;
}

model::AircraftObservation pressure_only(const model::Ownship& own, uint32_t address, int north_m,
                                         int32_t pressure_altitude_ft) {
    model::AircraftObservation observation = heard(own, address, north_m, 0, model::Source::Adsb);
    observation.altitude_valid = false;
    observation.pressure_altitude_ft = pressure_altitude_ft;
    observation.pressure_altitude_valid = true;
    return observation;
}

model::Source source_of(const TrafficTable& table, int index) {
    return table.at(index)->observation.source;
}

}  // namespace

// Epic #184 ranks the duplicates: direct ADS-L and FLARM, then ADS-B, then the uplink.
TEST_CASE("traffic: one aircraft heard three ways is direct first, then ADS-B, then the uplink") {
    TrafficTable table;
    const uint32_t hold_s = direct_preferred_max_age_s(observation(0x3C6586, kIcao, 100));
    table.update(observation(0x3C6586, kIcao, 100, model::Source::AdslDirect), 100);
    const int index = table.find(kIcao, 0x3C6586);
    REQUIRE(index >= 0);

    for (uint32_t later = 101; later <= 100 + hold_s; later++) {
        table.update(observation(0x3C6586, kIcao, later, model::Source::Adsb), later);
        table.update(observation(0x3C6586, kIcao, later, model::Source::AdslUplink), later);
        CHECK(source_of(table, index) == model::Source::AdslDirect);
    }

    const uint32_t direct_stale = 100 + hold_s + 1;
    table.update(observation(0x3C6586, kIcao, direct_stale, model::Source::Adsb), direct_stale);
    CHECK(source_of(table, index) == model::Source::Adsb);
    for (uint32_t later = direct_stale; later <= direct_stale + hold_s; later++) {
        table.update(observation(0x3C6586, kIcao, later, model::Source::AdslUplink), later);
        CHECK(source_of(table, index) == model::Source::Adsb);
    }

    const uint32_t adsb_stale = direct_stale + hold_s + 1;
    table.update(observation(0x3C6586, kIcao, adsb_stale, model::Source::AdslUplink), adsb_stale);
    CHECK(source_of(table, index) == model::Source::AdslUplink);
    table.update(observation(0x3C6586, kIcao, adsb_stale, model::Source::Adsb), adsb_stale);
    CHECK(source_of(table, index) == model::Source::Adsb);
    table.update(observation(0x3C6586, kIcao, adsb_stale, model::Source::Alptas), adsb_stale);
    CHECK(source_of(table, index) == model::Source::Alptas);
    CHECK(table.count() == 1);
}

// ADS-L F.2.3 has a transponder-carrying aircraft send its ICAO address, so only that copy merges.
TEST_CASE("traffic: only an ADS-L or FLARM target sending its ICAO address merges with ADS-B") {
    TrafficTable table;
    table.update(observation(0x3C6586, kIcao, 100, model::Source::Adsb), 100);
    table.update(observation(0x3C6586, kIcao, 100, model::Source::Alptas), 100);
    CHECK(table.count() == 1);
    CHECK(source_of(table, table.find(kIcao, 0x3C6586)) == model::Source::Alptas);

    table.update(observation(0x3C6586, 6, 100, model::Source::Alptas), 100);
    table.update(observation(0x3C6586, settings::kAddressMappingTable, 100), 100);
    CHECK(table.count() == 3);
}

// The Bravo in our cockpit, a FLARM set to our ICAO and every ground station all report our own.
TEST_CASE("traffic: our ADS-L address and our aircraft's ICAO address are both not traffic") {
    TrafficTable table;
    table.set_own_address(settings::kAddressMappingTable, 0x00ABCD);
    table.set_own_icao(0x3C6586);
    CHECK(table.update(observation(0x00ABCD, settings::kAddressMappingTable, 100), 100) < 0);
    CHECK(table.update(observation(0x3C6586, kIcao, 100, model::Source::Adsb), 100) < 0);
    CHECK(table.update(observation(0x3C6586, kIcao, 100, model::Source::Alptas), 100) < 0);
    CHECK(table.update(observation(0x3C6586, kIcao, 100, model::Source::AdslUplink), 100) < 0);
    CHECK(table.count() == 0);

    CHECK(table.update(observation(0x3C6586, settings::kAddressMappingTable, 100), 100) >= 0);
    CHECK(table.update(observation(0x3C6586, 6, 100, model::Source::Alptas), 100) >= 0);
    CHECK(table.update(observation(0x00ABCD, kIcao, 100, model::Source::Adsb), 100) >= 0);
    CHECK(table.count() == 3);

    table.set_own_icao(0);
    CHECK(table.update(observation(0x3C6586, kIcao, 100, model::Source::Adsb), 100) >= 0);
    CHECK(table.count() == 4);
}

// The address names our own aircraft, so even an alarm it raised is an alarm on ourselves.
TEST_CASE("traffic: setting our ICAO address releases the target it names, alarming or not") {
    TrafficTable table;
    REQUIRE(table.update(observation(0x3C6586, kIcao, 100, model::Source::Adsb), 100) >= 0);
    REQUIRE(table.update(observation(0x3C6587, kIcao, 100, model::Source::Adsb), 100) >= 0);
    REQUIRE(table.update(observation(0x3C6586, 6, 100, model::Source::Alptas), 100) >= 0);
    table.at(table.find(kIcao, 0x3C6586))->alarm_level = Level::Advisory;
    REQUIRE(table.count() == 3);

    table.set_own_icao(0x3C6586);
    CHECK(table.find(kIcao, 0x3C6586) < 0);
    CHECK(table.find(kIcao, 0x3C6587) >= 0);
    CHECK(table.find(6, 0x3C6586) >= 0);
    CHECK(table.count() == 2);

    REQUIRE(table.update(observation(0x3C6588, kIcao, 100, model::Source::Adsb), 100) >= 0);
    table.set_own_icao(0x3C6586);
    CHECK(table.count() == 3);
    table.set_own_icao(0);
    CHECK(table.count() == 3);
}

// FL350 is 10668 m, so this airliner is far past any band the alarm or the radar uses.
TEST_CASE("traffic: an ADS-B target 200 km away at FL350 is held while the table has room") {
    const model::Ownship own = flying(30, 0);
    TrafficTable table;
    table.set_own_reference(own);

    CHECK(table.update(heard(own, 0x400001, 200000, 10668 - 1000, model::Source::Adsb), 100) >= 0);
    CHECK(table.update(heard(own, 0x400002, 5000, 3000, model::Source::Adsb), 100) >= 0);
    CHECK(table.update(pressure_only(own, 0x400003, 100000, 35000), 100) >= 0);
    CHECK(table.count() == 3);
    CHECK(table.implausible() == 0);
}

TEST_CASE("traffic: a full table of far ADS-B gives its farthest slot to a nearer target") {
    const model::Ownship own = flying(30, 0);
    TrafficTable table;
    table.set_own_reference(own);
    for (int i = 0; i < TrafficTable::kMaxTargets; i++)
        REQUIRE(table.update(heard(own, 0x450000u + i, 50000 + 1000 * i, 9000, model::Source::Adsb),
                             100) >= 0);
    const uint32_t farthest = 0x450000u + TrafficTable::kMaxTargets - 1;

    CHECK(table.update(heard(own, 0x460001, 3000, 0, model::Source::Adsb), 100) >= 0);
    CHECK(table.find(kIcao, farthest) == -1);
    CHECK(table.find(kIcao, farthest - 1) >= 0);
    CHECK(table.update(heard(own, 0x460002, 400000, 9000, model::Source::Adsb), 100) < 0);
    CHECK(table.count() == TrafficTable::kMaxTargets);
}

// No report of its own position releases a held aircraft, however far it goes.
TEST_CASE("traffic: a held ADS-B target that reports itself far away and above keeps its slot") {
    const model::Ownship own = flying(30, 0);
    TrafficTable table;
    table.set_own_reference(own);
    const int index = table.update(heard(own, 0x420001, 5000, 0, model::Source::Adsb, 100), 100);
    REQUIRE(index >= 0);

    CHECK(table.update(heard(own, 0x420001, 5000, 3000, model::Source::Adsb, 101), 101) == index);
    CHECK(table.at(index)->observation.received.at_s == 101);
    CHECK(table.update(heard(own, 0x420001, 40000, 3000, model::Source::Adsb, 102), 102) == index);
    CHECK(table.count() == 1);
}

// The alarm grades the new height on the report that brings it, so the advisory drops at once.
TEST_CASE("traffic: an advisory with no height that next reports 800 m above stops being one") {
    const model::Ownship own = flying(30, 0);
    TrafficTable table;
    table.set_own_reference(own);
    model::AircraftObservation unknown = heard(own, 0x420002, 2000, 0, model::Source::Adsb, 100);
    unknown.altitude_valid = false;
    const int index = table.update(unknown, 100);
    REQUIRE(index >= 0);
    table.at(index)->alarm_level = Level::Advisory;

    CHECK(table.update(heard(own, 0x420002, 2000, 800, model::Source::Adsb, 101), 101) == index);
    CHECK(table.at(index)->observation.received.at_s == 101);
    CHECK(table.at(index)->observation.altitude_valid);
    CHECK(assess(own, table.at(index)->observation, 0).level == Level::None);
}

// Half a CPR pair has no position, and with a slant of 0 it would have outranked every real target.
TEST_CASE("traffic: an ADS-B report with no position yet takes no slot") {
    const model::Ownship own = flying(30, 0);
    TrafficTable table;
    table.set_own_reference(own);
    model::AircraftObservation half_pair = heard(own, 0x420003, 2000, 0, model::Source::Adsb);
    half_pair.position_valid = false;
    CHECK(table.update(half_pair, 100) < 0);
    CHECK(table.count() == 0);
    CHECK(table.adsb_without_position() == 1);
    CHECK(table.implausible() == 0);

    model::AircraftObservation relayed = half_pair;
    relayed.source = model::Source::AdslUplink;
    CHECK(table.update(relayed, 100) >= 0);
}

// Without a fix every slant is 0, so rank decides, and an ADS-B flood never costs a direct target.
TEST_CASE("traffic: without a fix of our own, ADS-B churns among itself and gives way to direct") {
    model::Ownship own = flying(30, 0);
    own.fix_valid = false;
    TrafficTable table;
    table.set_own_reference(own);
    for (int i = 0; i < TrafficTable::kMaxTargets; i++) {
        const uint32_t at_s = 100 + static_cast<uint32_t>(i);
        REQUIRE(table.update(
                    heard(flying(30, 0), 0x430000u + i, 400000, 9000, model::Source::Adsb, at_s),
                    at_s) >= 0);
    }

    const uint32_t now_s = 100 + TrafficTable::kMaxTargets;
    CHECK(table.update(observation(0x500001, 6, now_s, model::Source::AdslDirect), now_s) >= 0);
    CHECK(table.find(kIcao, 0x430000) == -1);
    CHECK(table.update(observation(0x500002, kIcao, now_s, model::Source::Adsb), now_s) >= 0);
    CHECK(table.find(kIcao, 0x430001) == -1);
    CHECK(table.update(observation(0x500003, 6, now_s, model::Source::AdslUplink), now_s) < 0);

    TrafficTable relayed_only;
    for (int i = 0; i < TrafficTable::kMaxTargets; i++)
        REQUIRE(relayed_only.update(observation(0x610000u + i, 6, 100, model::Source::AdslUplink),
                                    100) >= 0);
    CHECK(relayed_only.update(observation(0x500002, kIcao, 101, model::Source::Adsb), 101) >= 0);
    CHECK(relayed_only.count() == TrafficTable::kMaxTargets);

    TrafficTable direct_only;
    for (int i = 0; i < TrafficTable::kMaxTargets; i++)
        REQUIRE(direct_only.update(observation(0x600000u + i, 6, 100), 100) >= 0);
    CHECK(direct_only.update(observation(0x500002, kIcao, 101, model::Source::Adsb), 101) < 0);
}

// Every branch of a full table's door, in the sky a Tango sees beside a busy airfield.
TEST_CASE(
    "traffic: a full table of dense ADS-B evicts the ground, then the farthest, never an "
    "advisory") {
    const model::Ownship own = flying(30, 0);
    TrafficTable table;
    table.set_own_reference(own);

    model::AircraftObservation advisory = heard(own, 0x700001, 2900, 0, model::Source::Adsb);
    advisory.altitude_valid = false;
    const int advisory_index = table.update(advisory, 100);
    REQUIRE(advisory_index >= 0);
    table.at(advisory_index)->alarm_level = Level::Advisory;
    model::AircraftObservation apron = heard(own, 0x700002, 300, 0, model::Source::AdslDirect);
    apron.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
    REQUIRE(table.update(apron, 100) >= 0);
    REQUIRE(table.update(heard(own, 0x700003, 2000, 0, model::Source::AdslUplink), 100) >= 0);
    for (int i = 0; i < 4; i++)
        REQUIRE(table.update(
                    heard(own, 0x710000u + i, 500 + 100 * i, 50, model::Source::AdslDirect), 100) >=
                0);
    const int dense = TrafficTable::kMaxTargets - 7;
    for (int i = 0; i < dense; i++) {
        model::AircraftObservation nearby =
            heard(own, 0x720000u + i, 1000 + 40 * i, (i % 9 - 4) * 100, model::Source::Adsb);
        nearby.altitude_valid = i != dense - 1;
        REQUIRE(table.update(nearby, 100) >= 0);
    }
    REQUIRE(table.count() == TrafficTable::kMaxTargets);
    const uint32_t farthest_dense = 0x720000u + dense - 1;

    CHECK(table.update(heard(own, 0x730001, 2700, 0, model::Source::Adsb), 100) >= 0);
    CHECK(table.find(6, 0x700002) == -1);

    CHECK(table.update(heard(own, 0x730002, 400, 0, model::Source::AdslDirect), 100) >= 0);
    CHECK(table.find(kIcao, 0x730001) == -1);

    CHECK(table.update(heard(own, 0x730003, 2800, 0, model::Source::Adsb), 100) < 0);

    const int farthest_m = 1000 + 40 * (dense - 1);
    model::AircraftObservation level_with_it =
        heard(own, 0x730005, farthest_m, 0, model::Source::AdslDirect);
    level_with_it.altitude_valid = false;
    CHECK(table.update(level_with_it, 100) >= 0);
    CHECK(table.find(kIcao, farthest_dense) == -1);

    model::AircraftObservation same_place = level_with_it;
    same_place.address = 0x730006;
    same_place.address_mapping_table = kIcao;
    same_place.source = model::Source::Adsb;
    CHECK(table.update(same_place, 100) < 0);

    same_place.address = 0x730007;
    same_place.address_mapping_table = 6;
    same_place.source = model::Source::AdslDirect;
    same_place.received.at_s = 101;
    CHECK(table.update(same_place, 101) >= 0);
    CHECK(table.find(6, 0x730005) == -1);

    CHECK(table.count() == TrafficTable::kMaxTargets);
    CHECK(table.find(kIcao, 0x700001) == advisory_index);
}

// A paired Bravo dates by its own clock, a second or two ahead of our second, and that is no age.
TEST_CASE("traffic: a reception two seconds ahead is held as new, one further ahead is dropped") {
    const model::Ownship own = flying(30, 0);
    TrafficTable table;
    table.set_own_reference(own);
    REQUIRE(table.update(heard(own, 0x3C6586, 1500, 0, model::Source::Adsb, 102), 100) >= 0);
    REQUIRE(table.update(heard(own, 0x3C6587, 1800, 0, model::Source::Adsb, 103), 100) >= 0);
    table.age_out(100);
    CHECK(table.find(kIcao, 0x3C6586) >= 0);
    CHECK(table.find(kIcao, 0x3C6587) < 0);
    table.age_out(102 + kAirborneTargetForgetS);
    CHECK(table.find(kIcao, 0x3C6586) >= 0);
    table.age_out(103 + kAirborneTargetForgetS);
    CHECK(table.find(kIcao, 0x3C6586) < 0);
}
