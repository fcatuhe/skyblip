// $LK8EX1 is the one sentence LK8000, XCSoar and their descendants already parse
// that carries a battery, and it carries the vario picture with it. Field order,
// from the LK8000 definition and the three senders that agree on it (SoftRF
// lyusupov NMEA.cpp:246-256, SoftRF MB Baro.cpp:404-415, GXAirCom main.cpp:4370):
// pressure in pascals, altitude in metres on 1013.25, vertical speed in cm/s,
// temperature in Celsius, battery.
#include <string>

#include "core/protocol/nmea_out.h"
#include "doctest/doctest.h"
#include "test/support/nmea_checksum.h"

using namespace skyblip;
using namespace skyblip::protocol;

TEST_CASE("nmea: LK8EX1 carries pressure, altitude, vario, temperature and the cell") {
    char buf[128];
    Lk8Ex1 v{};
    v.pressure_pa = 90000;
    v.pressure_valid = true;
    v.alt_m = 988;
    v.alt_valid = true;
    v.vario_cm_s = -125;
    v.vario_valid = true;
    v.temperature_c = -7;
    v.temperature_valid = true;
    v.battery_percent = 38;
    v.battery_valid = true;

    const int n = format_lk8ex1(buf, sizeof(buf), v);
    const std::string s(buf, static_cast<size_t>(n));
    CHECK(s.rfind("$LK8EX1,90000,988,-125,-7,1038*", 0) == 0);
    CHECK(checksum_ok(s));
    CHECK(s.substr(s.size() - 2) == "\r\n");
}

// The distinction the whole sentence turns on. Field 5 below 1000 is a VOLTAGE
// in volts; at 1000 and above it is a percentage with 1000 added. Sending 38 for
// 38% would be read as 38 volts, and sending 1038 for a 3.8 V cell would be read
// as 100%. SoftRF's lyusupov tree sends the voltage form and the moshe-braner
// fork corrected itself to this one with the reason in the code ("LK8000 specs
// say send percent instead of volts as an integer, percent+1000",
// MB src/driver/Baro.cpp:406-407); we send the percentage, because it is also
// the number our own panel draws and the two must not disagree.
TEST_CASE("nmea: LK8EX1 battery is a percentage plus 1000, never a bare percentage") {
    char buf[128];
    Lk8Ex1 v{};
    v.battery_valid = true;

    v.battery_percent = 0;
    format_lk8ex1(buf, sizeof(buf), v);
    CHECK(std::string(buf).find(",1000*") != std::string::npos);

    v.battery_percent = 100;
    format_lk8ex1(buf, sizeof(buf), v);
    CHECK(std::string(buf).find(",1100*") != std::string::npos);

    // Nothing may land in the voltage half of the field, which is every value
    // below 1000, and nothing above a full cell.
    for (int percent = 0; percent <= 255; percent++) {
        v.battery_percent = static_cast<uint8_t>(percent);
        const int n = format_lk8ex1(buf, sizeof(buf), v);
        const std::string s(buf, static_cast<size_t>(n));
        const size_t last = s.rfind(',');
        const long field = std::stol(s.substr(last + 1, s.find('*') - last - 1));
        CHECK(field >= 1000);
        CHECK(field <= 1100);
    }
}

// Every field has a value that means "no reading", and they are all different.
// A unit with no barometer still has a cell, and that alone is a sentence worth
// sending: it is the only one we speak that says anything about power.
TEST_CASE("nmea: LK8EX1 says 'not available' with the sentinel the protocol defines") {
    char buf[128];
    Lk8Ex1 nothing{};
    const int n = format_lk8ex1(buf, sizeof(buf), nothing);
    const std::string s(buf, static_cast<size_t>(n));
    CHECK(s.rfind("$LK8EX1,999999,99999,9999,99,999*", 0) == 0);
    CHECK(checksum_ok(s));

    // One field at a time, so a sentinel written into the wrong column fails
    // here rather than reading as a 999-metre climb on someone's vario.
    Lk8Ex1 v{};
    v.pressure_pa = 101325;
    v.pressure_valid = true;
    format_lk8ex1(buf, sizeof(buf), v);
    CHECK(std::string(buf).rfind("$LK8EX1,101325,99999,9999,99,999*", 0) == 0);

    v = Lk8Ex1{};
    v.alt_m = -300;
    v.alt_valid = true;
    format_lk8ex1(buf, sizeof(buf), v);
    CHECK(std::string(buf).rfind("$LK8EX1,999999,-300,9999,99,999*", 0) == 0);

    v = Lk8Ex1{};
    v.vario_cm_s = 250;
    v.vario_valid = true;
    format_lk8ex1(buf, sizeof(buf), v);
    CHECK(std::string(buf).rfind("$LK8EX1,999999,99999,250,99,999*", 0) == 0);

    // A real 99 C is not sendable, because 99 is the sentinel: the clamp below
    // stops at 98 and this is the boundary that proves the two never collide.
    v = Lk8Ex1{};
    v.temperature_c = 21;
    v.temperature_valid = true;
    format_lk8ex1(buf, sizeof(buf), v);
    CHECK(std::string(buf).rfind("$LK8EX1,999999,99999,9999,21,999*", 0) == 0);

    v = Lk8Ex1{};
    v.battery_percent = 92;
    v.battery_valid = true;
    format_lk8ex1(buf, sizeof(buf), v);
    CHECK(std::string(buf).rfind("$LK8EX1,999999,99999,9999,99,1092*", 0) == 0);
}

// A reading that happens to equal a sentinel would be read as no reading at all,
// so each field stops one short of the value that names it. The altitude and
// temperature bounds are SoftRF's own (constrain(-1000, 99998) and
// constrain(-99, 98), lyusupov NMEA.cpp:250-253).
TEST_CASE("nmea: LK8EX1 clamps every field short of its own 'not available' value") {
    char buf[128];
    Lk8Ex1 v{};
    v.pressure_valid = v.alt_valid = v.vario_valid = v.temperature_valid = true;
    v.pressure_pa = 999999;
    v.alt_m = 99999;
    v.vario_cm_s = 9999;
    v.temperature_c = 99;
    format_lk8ex1(buf, sizeof(buf), v);
    CHECK(std::string(buf).rfind("$LK8EX1,999998,99998,9998,98,999*", 0) == 0);

    // And the low ends, where an aircraft below sea level and a cold soak live.
    v.pressure_pa = 0;
    v.alt_m = -32000;
    v.vario_cm_s = -9999;
    v.temperature_c = -100;
    format_lk8ex1(buf, sizeof(buf), v);
    CHECK(std::string(buf).rfind("$LK8EX1,0,-1000,-9998,-99,999*", 0) == 0);
}
