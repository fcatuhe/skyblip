// What a tablet is told about own-ship: pressure altitude in $PGRMZ, the cell in $LK8EX1, and
// $GPRMC/$GPGGA for an EFB with no GNSS. test_nmea_service.cpp says which app reads what.
#include <string>
#include <vector>

#include "core/events/link.h"
#include "core/events/rf.h"
#include "core/flight/atmosphere.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/link.h"
#include "ports/null.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/nmea_checksum.h"
#include "test/support/nmea_stream.h"
#include "test/support/product_rig.h"

using namespace skyblip;

TEST_CASE("nmea: $PGRMZ carries pressure altitude on the standard datum") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    // The air the aircraft is actually flying through, nowhere near standard.
    rig.platform.baro().chip.set_pressure_mpa(90000000);
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 2);
    REQUIRE(rig.state().baro.active);

    std::string altitude;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$PGRMZ,", 0) == 0) altitude = s;
    REQUIRE_FALSE(altitude.empty());
    CHECK(checksum_ok(altitude));
    const std::vector<std::string> f = fields(altitude);
    REQUIRE(f.size() >= 3);
    CHECK(f[2] == "F");

    const uint32_t pressure_pa = rig.state().baro.pressure_mpa / 1000;
    const int32_t standard_cm = flight::pressure_to_alt_cm(pressure_pa);
    const int32_t sent_cm = static_cast<int32_t>(std::stoi(f[1])) * 3048 / 100;
    // What an EFB does with this figure is apply its own QNH, so the figure has
    // to be the datum-free one: pressure altitude on 1013.25, within a foot.
    CHECK(standard_cm > 90000);
    CHECK(std::abs(sent_cm - standard_cm) < 40);
}

// G. Battery state reaches the panel and stops there. $LK8EX1 is the one
// sentence LK8000, XCSoar and their descendants already parse that carries a
// cell, and it carries the vario picture with it. Field 5 is a voltage below
// 1000 and a percentage plus 1000 at or above it, which is the distinction the
// whole sentence turns on: 55 in that field is fifty-five volts.
TEST_CASE("nmea: the cell and the temperature reach a tablet in $LK8EX1, on the $PGRMZ pass") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    // A cell in the flat middle, and air the aircraft is really flying through.
    rig.platform.battery().millivolts = 3800;
    rig.platform.baro().chip.set_pressure_mpa(90000000);
    rig.platform.baro().chip.set_temperature_decicelsius(-72);
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 2);
    REQUIRE(rig.state().baro.active);
    REQUIRE(rig.state().power.battery.valid);

    std::string lk8;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$LK8EX1,", 0) == 0) lk8 = s;
    REQUIRE_FALSE(lk8.empty());
    CHECK(checksum_ok(lk8));

    const std::vector<std::string> f = fields(lk8);
    REQUIRE(f.size() == 6);
    // Field 1 is the raw pressure in pascals, which is what a consumer prefers
    // over field 2 because it can apply its own datum to it.
    CHECK(std::stol(f[1]) == static_cast<long>(rig.state().baro.pressure_mpa / 1000));
    // Field 2 is metres on 1013.25, the same datum-free figure $PGRMZ carries.
    CHECK(std::abs(std::stol(f[2]) -
                   flight::pressure_to_alt_cm(rig.state().baro.pressure_mpa / 1000) / 100) <= 1);
    // Field 5 is the gauge's own percentage, offset by 1000. A device that says
    // 55% on its panel and something else on the tablet is a support call.
    CHECK(std::stol(f[5]) == 1000 + rig.state().power.battery.percent);
    CHECK(std::stol(f[5]) >= 1000);
    CHECK(int(rig.state().power.battery.percent) == 55);
    // Field 4 is what the part measured, carried in the whole degrees the sentence is read in.
    REQUIRE(rig.state().baro.temperature_valid);
    CHECK(rig.state().baro.temperature_decicelsius == -72);
    CHECK(f[4] == "-7");

    // Same pass as $PGRMZ, so the same cadence, second for second.
    rig.platform.link().clear();
    fly(rig, t, 3);
    CHECK(count_of(rig, "$LK8EX1") >= 3);
    CHECK(count_of(rig, "$LK8EX1") == count_of(rig, "$PGRMZ"));
    CHECK(count_of(rig, "$LK8EX1") == count_of(rig, "$PFLAU"));
}

// $PGRMZ is gated on a barometer because a GNSS altitude under that sentence
// name would feed a geometric height into an app's altimeter. $LK8EX1 is not,
// because the cell is not a barometric quantity and this is the only sentence we
// speak that says anything about power: silence here is a pilot with no way to
// see a flat unit coming. SoftRF MB sends the same battery-only sentence when no
// baro chip answered (src/protocol/data/NMEA.cpp:1398-1401).
TEST_CASE("nmea: a unit with no barometer still tells the tablet about its cell") {
    Rig rig(kBaroByHand);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.platform.battery().millivolts = 3600;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 2);
    REQUIRE_FALSE(rig.state().baro.active);
    REQUIRE(rig.state().power.battery.valid);

    // No $PGRMZ at all, and an $LK8EX1 every second regardless.
    CHECK(count_of(rig, "$PGRMZ") == 0);
    CHECK(count_of(rig, "$LK8EX1") >= 2);

    std::string lk8;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$LK8EX1,", 0) == 0) lk8 = s;
    REQUIRE_FALSE(lk8.empty());
    CHECK(checksum_ok(lk8));

    const std::vector<std::string> f = fields(lk8);
    REQUIRE(f.size() == 6);
    CHECK(f[1] == "999999");  // no pressure
    CHECK(f[2] == "99999");   // no pressure altitude
    CHECK(f[4] == "99");      // no temperature
    CHECK(std::stol(f[5]) == 1000 + rig.state().power.battery.percent);
    CHECK(int(rig.state().power.battery.percent) == 12);
}

// The cadence arithmetic this relies on: emit_ownship() is two calls inside
// run_pass(), the same pass PFLAU and PGRMZ already share, at the pass's fixed
// 1 Hz. NmeaService::kTargetsPerPass and kPassesPerRefreshBound are derived
// only from kTargetRefreshBoundMs, kMovingTargetRedrawMs and the table's
// capacity - none of which this reads or writes - so two more sentences a pass
// changes what a pass costs in bytes, never how many passes the refresh bound
// allows or how many targets one may carry.
TEST_CASE("nmea: GPRMC/GPGGA give a panel-mounted tablet the position it has no GNSS for") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 2);
    REQUIRE(rig.state().own.fix_valid);
    REQUIRE(rig.state().own.utc_valid);

    std::string rmc, gga;
    for (const std::string& s : sentences(rig)) {
        if (s.rfind("$GPRMC,", 0) == 0) rmc = s;
        if (s.rfind("$GPGGA,", 0) == 0) gga = s;
    }
    REQUIRE_FALSE(rmc.empty());
    REQUIRE_FALSE(gga.empty());
    CHECK(checksum_ok(rmc));
    CHECK(checksum_ok(gga));

    const std::vector<std::string> rf = fields(rmc);
    REQUIRE(rf.size() >= 10);
    CHECK(rf[2] == "A");       // status: a valid fix, not void
    CHECK(rf[1].size() == 6);  // hhmmss
    CHECK(rf[9].size() == 6);  // ddmmyy

    const std::vector<std::string> gf = fields(gga);
    REQUIRE(gf.size() >= 9);
    CHECK(gf[6] == "1");          // fix quality: a fix
    CHECK(std::stoi(gf[7]) > 0);  // satellites, as the rig's fix reports them

    // One pass a second, one of each per pass: two more seconds of flight is
    // two more of each, same as the $PFLAU heartbeat they now share a pass with.
    rig.platform.link().clear();
    fly(rig, t, 2);
    CHECK(count_of(rig, "$GPRMC") >= 2);
    CHECK(count_of(rig, "$GPGGA") >= 2);
    CHECK(count_of(rig, "$GPRMC") == count_of(rig, "$PFLAU"));
}
