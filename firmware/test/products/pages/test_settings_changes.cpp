// What the settings prompt says a "set" will change, row by row, in the menu's own words.
#include <cstring>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/pages/settings_changes.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

std::string described(const Settings& from, const Settings& to) {
    char out[kSettingsChangesBytes];
    const int written = describe_settings_changes(from, to, out, sizeof(out));
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
    to.alarm_volume = AlarmVolume::Soft;
    CHECK(described(from, to) == "TYPE GLIDER\nVOLUME SOFT");
}

TEST_CASE(
    "settings changes page: a cleared callsign, a silenced alarm and the bench trims read whole") {
    Settings from = defaults();
    std::strcpy(from.callsign, "F-JABCDEF");
    Settings to = from;
    to.callsign[0] = 0;
    to.alarm_enabled = false;
    to.battery_offset_mv = -20;
    to.frequency_trim_e1_ppm = 15;
    CHECK(described(from, to) == "CALLSIGN NONE\nALARM OFF\nAND 2 MORE");

    to = from;
    to.battery_offset_mv = -20;
    to.frequency_trim_e1_ppm = 15;
    CHECK(described(from, to) == "BATTERY -20 MV\nFREQ +1.5 PPM");
}

// A phone that clears the own ICAO makes the aircraft's transponder traffic, so the glass says so.
TEST_CASE(
    "settings changes page: an own ICAO set or cleared reads in hex, or NONE, under the name") {
    Settings from = defaults();
    Settings to = from;
    to.own_icao = 0x3C6586;
    CHECK(described(from, to) == "OWN ICAO 3C6586");

    from = to;
    to.own_icao = kOwnIcaoUnset;
    to.units = Units::Metric;
    CHECK(described(from, to) == "OWN ICAO NONE\nUNITS METRIC");
}

TEST_CASE("settings changes page: every key a set may change at once still counts on three rows") {
    Settings from = defaults();
    Settings to = from;
    to.aircraft_type = 7;
    std::strcpy(to.callsign, "F-JXYZ");
    to.own_icao = 0x3C6586;
    to.units = Units::Metric;
    to.alarm_enabled = false;
    to.alarm_volume = AlarmVolume::Off;
    to.battery_offset_mv = -20;
    to.frequency_trim_e1_ppm = 15;
    CHECK(described(from, to) == "TYPE PARAGLIDER\nCALLSIGN F-JXYZ\nAND 6 MORE");
}

TEST_CASE("settings changes page: past three rows the last one counts the rest") {
    const Settings from = defaults();
    Settings to = from;
    to.aircraft_type = 7;
    std::strcpy(to.callsign, "F-JXYZ");
    to.units = Units::Metric;
    to.alarm_enabled = false;
    to.alarm_volume = AlarmVolume::Off;
    CHECK(described(from, to) == "TYPE PARAGLIDER\nCALLSIGN F-JXYZ\nAND 3 MORE");
}

// CALLSIGN and nine characters run past a line, so the row takes two and the count says so.
TEST_CASE("settings changes page: a callsign too long for one line takes two, and still fits") {
    const Settings from = defaults();
    Settings to = from;
    std::strcpy(to.callsign, "F-JXYZ123");
    to.units = Units::Metric;
    CHECK(described(from, to) == "CALLSIGN F-JXYZ123\nUNITS METRIC");
    to.alarm_enabled = false;
    CHECK(described(from, to) == "CALLSIGN F-JXYZ123\nAND 2 MORE");
}

TEST_CASE(
    "settings changes page: nothing changed writes nothing, and neither does a short buffer") {
    const Settings from = defaults();
    char out[kSettingsChangesBytes] = "untouched";
    CHECK(describe_settings_changes(from, from, out, sizeof(out)) == 0);
    CHECK(std::string(out) == "untouched");

    Settings to = from;
    to.units = Units::Metric;
    CHECK(describe_settings_changes(from, to, out, kSettingsChangesBytes - 1) == 0);
    CHECK(std::string(out) == "untouched");
}
