// What the settings prompt says a "set" will change, row by row, in the menu's own words.
#include <cstring>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/pages/settings_changes.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

std::string described(const Settings& from, const Settings& to) {
    char out[kSettingsChangesCap];
    const int n = describe_settings_changes(from, to, out, sizeof(out));
    return n == 0 ? std::string() : std::string(out, static_cast<size_t>(n));
}

}  // namespace

TEST_CASE("settings changes: each change is a row in the menu's words, and nothing else is") {
    const Settings from = defaults();
    Settings to = from;
    std::strcpy(to.callsign, "F-JXYZ");
    to.units = Units::Metric;
    CHECK(described(from, to) == "CALLSIGN F-JXYZ\nUNITS METRIC");

    to = from;
    to.aircraft_type = 4;
    to.alarm_volume = 5;
    CHECK(described(from, to) == "TYPE GLIDER\nVOLUME 5 OF 5");
}

TEST_CASE("settings changes: a cleared callsign, a silenced alarm and the bench trims read whole") {
    Settings from = defaults();
    std::strcpy(from.callsign, "F-JABCDEF");
    Settings to = from;
    to.callsign[0] = 0;
    to.alarm_enabled = false;
    to.battery_offset_mv = -20;
    to.freq_trim_e1_ppm = 15;
    CHECK(described(from, to) == "CALLSIGN NONE\nALARM OFF\nBATTERY -20 MV\nFREQ TRIM +1.5 PPM");
}

TEST_CASE("settings changes: past four rows the last one counts the rest") {
    const Settings from = defaults();
    Settings to = from;
    to.aircraft_type = 7;
    std::strcpy(to.callsign, "F-JXYZ");
    to.units = Units::Metric;
    to.alarm_enabled = false;
    to.alarm_volume = 1;
    CHECK(described(from, to) == "TYPE PARAGLIDER\nCALLSIGN F-JXYZ\nUNITS METRIC\nAND 2 MORE");
}

TEST_CASE("settings changes: nothing changed writes nothing, and neither does a short buffer") {
    const Settings from = defaults();
    char out[kSettingsChangesCap] = "untouched";
    CHECK(describe_settings_changes(from, from, out, sizeof(out)) == 0);
    CHECK(std::string(out) == "untouched");

    Settings to = from;
    to.units = Units::Metric;
    CHECK(describe_settings_changes(from, to, out, kSettingsChangesCap - 1) == 0);
    CHECK(std::string(out) == "untouched");
}
