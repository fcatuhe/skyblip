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
    char buffer[128];
    Lk8Ex1 lk8ex1{};
    lk8ex1.pressure_pa = 90000;
    lk8ex1.pressure_valid = true;
    lk8ex1.altitude_m = 988;
    lk8ex1.altitude_valid = true;
    lk8ex1.vario_cm_s = -125;
    lk8ex1.vario_valid = true;
    lk8ex1.temperature_c = -7;
    lk8ex1.temperature_valid = true;
    lk8ex1.battery_percent = 38;
    lk8ex1.battery_valid = true;

    const int written = format_lk8ex1(buffer, sizeof(buffer), lk8ex1);
    const std::string sentence(buffer, static_cast<size_t>(written));
    CHECK(sentence.rfind("$LK8EX1,90000,988,-125,-7,1038*", 0) == 0);
    CHECK(checksum_ok(sentence));
    CHECK(sentence.substr(sentence.size() - 2) == "\r\n");
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
    char buffer[128];
    Lk8Ex1 lk8ex1{};
    lk8ex1.battery_valid = true;

    lk8ex1.battery_percent = 0;
    format_lk8ex1(buffer, sizeof(buffer), lk8ex1);
    CHECK(std::string(buffer).find(",1000*") != std::string::npos);

    lk8ex1.battery_percent = 100;
    format_lk8ex1(buffer, sizeof(buffer), lk8ex1);
    CHECK(std::string(buffer).find(",1100*") != std::string::npos);

    // Nothing may land in the voltage half of the field, which is every value
    // below 1000, and nothing above a full cell.
    for (int percent = 0; percent <= 255; percent++) {
        lk8ex1.battery_percent = static_cast<uint8_t>(percent);
        const int written = format_lk8ex1(buffer, sizeof(buffer), lk8ex1);
        const std::string sentence(buffer, static_cast<size_t>(written));
        const size_t last = sentence.rfind(',');
        const long field = std::stol(sentence.substr(last + 1, sentence.find('*') - last - 1));
        CHECK(field >= 1000);
        CHECK(field <= 1100);
    }
}

// Every field has a value that means "no reading", and they are all different.
// A unit with no barometer still has a cell, and that alone is a sentence worth
// sending: it is the only one we speak that says anything about power.
TEST_CASE("nmea: LK8EX1 says 'not available' with the sentinel the protocol defines") {
    char buffer[128];
    Lk8Ex1 nothing{};
    const int written = format_lk8ex1(buffer, sizeof(buffer), nothing);
    const std::string sentence(buffer, static_cast<size_t>(written));
    CHECK(sentence.rfind("$LK8EX1,999999,99999,9999,99,999*", 0) == 0);
    CHECK(checksum_ok(sentence));

    // One field at a time, so a sentinel written into the wrong column fails
    // here rather than reading as a 999-metre climb on someone's vario.
    Lk8Ex1 lk8ex1{};
    lk8ex1.pressure_pa = 101325;
    lk8ex1.pressure_valid = true;
    format_lk8ex1(buffer, sizeof(buffer), lk8ex1);
    CHECK(std::string(buffer).rfind("$LK8EX1,101325,99999,9999,99,999*", 0) == 0);

    lk8ex1 = Lk8Ex1{};
    lk8ex1.altitude_m = -300;
    lk8ex1.altitude_valid = true;
    format_lk8ex1(buffer, sizeof(buffer), lk8ex1);
    CHECK(std::string(buffer).rfind("$LK8EX1,999999,-300,9999,99,999*", 0) == 0);

    lk8ex1 = Lk8Ex1{};
    lk8ex1.vario_cm_s = 250;
    lk8ex1.vario_valid = true;
    format_lk8ex1(buffer, sizeof(buffer), lk8ex1);
    CHECK(std::string(buffer).rfind("$LK8EX1,999999,99999,250,99,999*", 0) == 0);

    // A real 99 C is not sendable, because 99 is the sentinel: the clamp below
    // stops at 98 and this is the boundary that proves the two never collide.
    lk8ex1 = Lk8Ex1{};
    lk8ex1.temperature_c = 21;
    lk8ex1.temperature_valid = true;
    format_lk8ex1(buffer, sizeof(buffer), lk8ex1);
    CHECK(std::string(buffer).rfind("$LK8EX1,999999,99999,9999,21,999*", 0) == 0);

    lk8ex1 = Lk8Ex1{};
    lk8ex1.battery_percent = 92;
    lk8ex1.battery_valid = true;
    format_lk8ex1(buffer, sizeof(buffer), lk8ex1);
    CHECK(std::string(buffer).rfind("$LK8EX1,999999,99999,9999,99,1092*", 0) == 0);
}

// A reading that happens to equal a sentinel would be read as no reading at all,
// so each field stops one short of the value that names it. The altitude and
// temperature bounds are SoftRF's own (constrain(-1000, 99998) and
// constrain(-99, 98), lyusupov NMEA.cpp:250-253).
TEST_CASE("nmea: LK8EX1 clamps every field short of its own 'not available' value") {
    char buffer[128];
    Lk8Ex1 lk8ex1{};
    lk8ex1.pressure_valid = lk8ex1.altitude_valid = lk8ex1.vario_valid = lk8ex1.temperature_valid =
        true;
    lk8ex1.pressure_pa = 999999;
    lk8ex1.altitude_m = 99999;
    lk8ex1.vario_cm_s = 9999;
    lk8ex1.temperature_c = 99;
    format_lk8ex1(buffer, sizeof(buffer), lk8ex1);
    CHECK(std::string(buffer).rfind("$LK8EX1,999998,99998,9998,98,999*", 0) == 0);

    // And the low ends, where an aircraft below sea level and a cold soak live.
    lk8ex1.pressure_pa = 0;
    lk8ex1.altitude_m = -32000;
    lk8ex1.vario_cm_s = -9999;
    lk8ex1.temperature_c = -100;
    format_lk8ex1(buffer, sizeof(buffer), lk8ex1);
    CHECK(std::string(buffer).rfind("$LK8EX1,0,-1000,-9998,-99,999*", 0) == 0);
}
