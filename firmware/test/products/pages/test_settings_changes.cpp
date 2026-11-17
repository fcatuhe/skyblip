// What the settings prompt says a "set" will change, row by row, in the menu's own words.
#include <cstring>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/pages/settings_changes.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

std::string described(const Settings& from, const Settings& to, bool sounds = true) {
    char out[kSettingsChangesBytes];
    const int written = describe_settings_changes(from, to, out, sizeof(out), sounds);
    return written == 0 ? std::string() : std::string(out, static_cast<size_t>(written));
}

}  // namespace

TEST_CASE("settings changes page: each change is a row in the menu's words, and nothing else is") {
    const Settings from = defaults();
    Settings to = from;
    std::strcpy(to.callsign, "F-JXYZ");
    to.units = Units::Metric;
    CHECK(described(from, to) == "CALLSIGN F-JXYZ\nUNITS METRIC");

    to = from;
    to.aircraft_type = 4;
    to.alarm = Alarm::Soft;
    CHECK(described(from, to) == "TYPE GLIDER\nALARM SOFT");
}

// A plain Go's soft and loud sound on its Bravo alone, so its glass confirms them as its row reads.
TEST_CASE(
    "settings changes page: a paired Go that cannot sound confirms BRAVO SOFT and BRAVO LOUD") {
    Settings from = defaults();
    from.alarm = Alarm::Visual;
    from.bravo = events::BluetoothAddress{1, {0x2A, 0x3F, 0x11, 0x22, 0x33, 0xC4}};
    Settings to = from;
    to.alarm = Alarm::Soft;
    CHECK(described(from, to, false) == "ALARM BRAVO SOFT");
    to.alarm = Alarm::Loud;
    CHECK(described(from, to, false) == "ALARM BRAVO LOUD");
    CHECK(described(from, to, true) == "ALARM LOUD");
}

TEST_CASE(
    "settings changes page: a cleared callsign, a silenced alarm and the bench trims read whole") {
    Settings from = defaults();
    std::strcpy(from.callsign, "F-JABCDEF");
    Settings to = from;
    to.callsign[0] = 0;
    to.alarm = Alarm::Off;
    to.battery_offset_mv = -20;
    to.frequency_trim_e1_ppm = 15;
    CHECK(described(from, to) == "CALLSIGN NONE\nALARM OFF\nAND 2 MORE");

    to = from;
    to.battery_offset_mv = -20;
    to.frequency_trim_e1_ppm = 15;
    CHECK(described(from, to) == "BATTERY -20 MV\nFREQ +1.5 PPM");
}

// A phone that clears the aircraft address makes the aircraft's transponder traffic, so the glass
// says so.
TEST_CASE(
    "settings changes page: an aircraft address set or cleared reads in hex, or NONE, under the "
    "name") {
    Settings from = defaults();
    Settings to = from;
    to.aircraft_address = 0x3C6586;
    CHECK(described(from, to) == "ADS-B 3C6586");

    from = to;
    to.aircraft_address = kAircraftAddressUnset;
    to.units = Units::Metric;
    CHECK(described(from, to) == "ADS-B NONE\nUNITS METRIC");
}

TEST_CASE("settings changes page: every key a set may change at once still counts on three rows") {
    Settings from = defaults();
    Settings to = from;
    to.aircraft_type = 7;
    std::strcpy(to.callsign, "F-JXYZ");
    to.aircraft_address = 0x3C6586;
    to.units = Units::Metric;
    to.alarm = Alarm::Off;
    to.battery_offset_mv = -20;
    to.frequency_trim_e1_ppm = 15;
    CHECK(described(from, to) == "TYPE PARAGLIDER\nCALLSIGN F-JXYZ\nAND 5 MORE");
}

TEST_CASE("settings changes page: past three rows the last one counts the rest") {
    const Settings from = defaults();
    Settings to = from;
    to.aircraft_type = 7;
    std::strcpy(to.callsign, "F-JXYZ");
    to.units = Units::Metric;
    to.alarm = Alarm::Off;
    to.aircraft_address = 0x3C6586;
    CHECK(described(from, to) == "TYPE PARAGLIDER\nCALLSIGN F-JXYZ\nAND 3 MORE");
}

// CALLSIGN and nine characters run past a line, so the row takes two and the count says so.
TEST_CASE("settings changes page: a callsign too long for one line takes two, and still fits") {
    const Settings from = defaults();
    Settings to = from;
    std::strcpy(to.callsign, "F-JXYZ123");
    to.units = Units::Metric;
    CHECK(described(from, to) == "CALLSIGN F-JXYZ123\nUNITS METRIC");
    to.alarm = Alarm::Visual;
    CHECK(described(from, to) == "CALLSIGN F-JXYZ123\nAND 2 MORE");
}

TEST_CASE(
    "settings changes page: nothing changed writes nothing, and neither does a short buffer") {
    const Settings from = defaults();
    char out[kSettingsChangesBytes] = "untouched";
    CHECK(describe_settings_changes(from, from, out, sizeof(out), true) == 0);
    CHECK(std::string(out) == "untouched");

    Settings to = from;
    to.units = Units::Metric;
    CHECK(describe_settings_changes(from, to, out, kSettingsChangesBytes - 1, true) == 0);
    CHECK(std::string(out) == "untouched");
}
