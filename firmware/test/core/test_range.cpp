// How far an emitter is and how far it is above, and the order the nearby page lists them in.
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
    model::Ownship o{};
    o.fix_valid = true;
    o.latitude_1e7 = 0;
    o.longitude_1e7 = 0;
    o.altitude_hae_mm = 1000000;
    return o;
}

model::AircraftObservation emitter(int32_t north_m,
                                   model::Source source = model::Source::AdslDirect) {
    model::AircraftObservation t{};
    t.position_valid = true;
    t.address = 0xABCD;
    t.latitude_1e7 = latitude_offset_for(north_m);
    t.longitude_1e7 = 0;
    t.altitude_m = 1000;
    t.source = source;
    return t;
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
    model::AircraftObservation t = emitter(1000);
    t.position_valid = false;
    RangeRow row;
    CHECK_FALSE(range_to(own_at_equator(), t, row));

    model::Ownship blind = own_at_equator();
    blind.fix_valid = false;
    CHECK_FALSE(range_to(blind, emitter(1000), row));
}

TEST_CASE("range: an aircraft directly overhead is no distance away, only above") {
    model::AircraftObservation t = emitter(0);
    t.altitude_m = 3000;
    RangeRow row;
    REQUIRE(range_to(own_at_equator(), t, row));
    CHECK(row.up_m == 2000);
    CHECK(row.ground_m == 0);
}

TEST_CASE("range: distance is the ground track, and height above is its own figure") {
    model::AircraftObservation t = emitter(3000);
    t.altitude_m = 5000;
    RangeRow row;
    REQUIRE(range_to(own_at_equator(), t, row));
    CHECK(row.up_m == 4000);
    CHECK(row.ground_m == doctest::Approx(3000).epsilon(0.01));
}

TEST_CASE("range: ranking puts the nearest emitters first and drops the rest") {
    TrafficTable table;
    const int32_t ranges[6] = {8000, 1000, 5000, 300, 12000, 2500};
    for (int i = 0; i < 6; i++) {
        model::AircraftObservation t = emitter(ranges[i]);
        t.address = 0x100u + static_cast<uint32_t>(i);
        table.update(t, 100);
    }

    RangeRow rows[4];
    const CallsignTable callsigns;
    const int n = rank_by_range(table, callsigns, own_at_equator(), rows, 4);
    REQUIRE(n == 4);
    CHECK(rows[0].address == 0x103);  // 300 m
    CHECK(rows[1].address == 0x101);  // 1000 m
    CHECK(rows[2].address == 0x105);  // 2500 m
    CHECK(rows[3].address == 0x102);  // 5000 m
    for (int i = 1; i < n; i++) CHECK(rows[i - 1].ground_m <= rows[i].ground_m);
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
    const int n = rank_by_range(table, callsigns, own_at_equator(), rows, 4);
    CHECK(n == 1);
    CHECK(rows[0].address == 0x200);
}
