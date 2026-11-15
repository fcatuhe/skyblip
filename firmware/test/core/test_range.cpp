// How far an emitter is and how far it is above, and the order the radar and nearby take them in.
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/range.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::traffic;

namespace {

// 1e-7 degrees of latitude per metre north, the same 11132 m/deg the position
// maths uses, so a test distance and the code's distance cannot drift apart.
int32_t latitude_offset_for(int32_t north_m) {
    return static_cast<int32_t>((static_cast<int64_t>(north_m) * 1000000) / 11132);
}

model::Ownship own_at_equator() {
    model::Ownship own{};
    own.fix_valid = true;
    own.latitude_1e7 = 0;
    own.longitude_1e7 = 0;
    own.altitude_hae_mm = 1000000;
    return own;
}

model::AircraftObservation emitter(int32_t north_m,
                                   model::Source source = model::Source::AdslDirect) {
    model::AircraftObservation observation{};
    observation.position_valid = true;
    observation.address = 0xABCD;
    observation.latitude_1e7 = latitude_offset_for(north_m);
    observation.longitude_1e7 = 0;
    observation.altitude_m = 1000;
    observation.source = source;
    return observation;
}

}  // namespace

TEST_CASE("range: an emitter carries the address and the system it was heard on") {
    RangeRow row;
    REQUIRE(range_to(own_at_equator(), emitter(1000, model::Source::Alptas), row));
    CHECK(row.address == 0xABCD);
    CHECK(row.source == model::Source::Alptas);
    CHECK(row.ground_m == doctest::Approx(1000).epsilon(0.01));
    CHECK(row.up_m == 0);
}

TEST_CASE("range: an emitter without a position cannot be ranged") {
    model::AircraftObservation observation = emitter(1000);
    observation.position_valid = false;
    RangeRow row;
    CHECK_FALSE(range_to(own_at_equator(), observation, row));

    model::Ownship blind = own_at_equator();
    blind.fix_valid = false;
    CHECK_FALSE(range_to(blind, emitter(1000), row));
}

TEST_CASE("range: an aircraft directly overhead is no distance away, only above") {
    model::AircraftObservation observation = emitter(0);
    observation.altitude_m = 3000;
    RangeRow row;
    REQUIRE(range_to(own_at_equator(), observation, row));
    CHECK(row.up_m == 2000);
    CHECK(row.ground_m == 0);
}

TEST_CASE("range: distance is the ground track, and height above is its own figure") {
    model::AircraftObservation observation = emitter(3000);
    observation.altitude_m = 5000;
    RangeRow row;
    REQUIRE(range_to(own_at_equator(), observation, row));
    CHECK(row.up_m == 4000);
    CHECK(row.ground_m == doctest::Approx(3000).epsilon(0.01));
}

TEST_CASE("range: an emitter that sends no altitude has a distance and no height") {
    model::AircraftObservation observation = emitter(3000);
    observation.altitude_valid = false;
    RangeRow row;
    REQUIRE(range_to(own_at_equator(), observation, row));
    CHECK_FALSE(row.up_valid);
    CHECK(row.ground_m == doctest::Approx(3000).epsilon(0.01));

    REQUIRE(range_to(own_at_equator(), emitter(3000), row));
    CHECK(row.up_valid);
}

TEST_CASE("range: ranking puts the nearest emitters at one height first and drops the rest") {
    TrafficTable table;
    const int32_t ranges[6] = {8000, 1000, 5000, 300, 12000, 2500};
    for (int i = 0; i < 6; i++) {
        model::AircraftObservation observation = emitter(ranges[i]);
        observation.address = 0x100u + static_cast<uint32_t>(i);
        table.update(observation, 100);
    }

    RangeRow rows[4];
    const CallsignTable callsigns;
    const int ranked = rank_by_relevance(table, callsigns, own_at_equator(), rows, 4);
    REQUIRE(ranked == 4);
    CHECK(rows[0].address == 0x103);  // 300 m
    CHECK(rows[1].address == 0x101);  // 1000 m
    CHECK(rows[2].address == 0x105);  // 2500 m
    CHECK(rows[3].address == 0x102);  // 5000 m
    for (int i = 1; i < ranked; i++) CHECK(rows[i - 1].ground_m <= rows[i].ground_m);
}

TEST_CASE("range: ranking skips what it cannot range") {
    TrafficTable table;
    model::AircraftObservation positioned = emitter(4000);
    positioned.address = 0x200;
    table.update(positioned, 100);

    model::AircraftObservation blind = emitter(1000);
    blind.address = 0x201;
    blind.position_valid = false;
    table.update(blind, 100);

    RangeRow rows[4];
    const CallsignTable callsigns;
    const int ranked = rank_by_relevance(table, callsigns, own_at_equator(), rows, 4);
    CHECK(ranked == 1);
    CHECK(rows[0].address == 0x200);
}

TEST_CASE("range: an aircraft raising an alarm ranks above every quiet one, however near") {
    TrafficTable table;
    for (int i = 0; i < 4; i++) {
        model::AircraftObservation observation = emitter(500 + 100 * i);
        observation.address = 0x300u + static_cast<uint32_t>(i);
        table.update(observation, 100);
    }
    model::AircraftObservation threat = emitter(2500);
    threat.address = 0x3FF;
    table.update(threat, 100);
    table.at(table.find(threat.address_mapping_table, threat.address))->alarm_level =
        Level::Advisory;

    RangeRow rows[4];
    const CallsignTable callsigns;
    REQUIRE(rank_by_relevance(table, callsigns, own_at_equator(), rows, 4) == 4);
    CHECK(rows[0].address == 0x3FF);
    CHECK(rows[0].alarm_level == Level::Advisory);
    CHECK(rows[1].address == 0x300);
}

// An airliner overhead is nearer on the ground than the glider at our height, and matters less.
TEST_CASE("range: inside the advisory's height window outranks nearer traffic far above") {
    TrafficTable table;
    model::AircraftObservation airliner = emitter(1000);
    airliner.address = 0x401;
    airliner.altitude_m = 1000 + kAdvisoryAltitudeM + 1;
    table.update(airliner, 100);
    model::AircraftObservation glider = emitter(5000);
    glider.address = 0x402;
    glider.altitude_m = 1000 + kAdvisoryAltitudeM;
    table.update(glider, 100);
    model::AircraftObservation silent = emitter(8000);
    silent.address = 0x403;
    silent.altitude_valid = false;
    table.update(silent, 100);

    RangeRow rows[3];
    const CallsignTable callsigns;
    REQUIRE(rank_by_relevance(table, callsigns, own_at_equator(), rows, 3) == 3);
    CHECK(rows[0].address == 0x402);
    CHECK(rows[1].address == 0x403);  // no altitude may be ours, so it is in the window
    CHECK(rows[2].address == 0x401);
}

TEST_CASE("range: a ranked row names the table slot it came from") {
    TrafficTable table;
    model::AircraftObservation observation = emitter(1000);
    observation.address = 0x501;
    table.update(observation, 100);

    RangeRow rows[1];
    const CallsignTable callsigns;
    REQUIRE(rank_by_relevance(table, callsigns, own_at_equator(), rows, 1) == 1);
    CHECK(table.at(rows[0].slot)->observation.address == 0x501);
}
