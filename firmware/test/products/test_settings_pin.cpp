// The Bravo pin a paired Go keeps in its settings blob, version 13, and what it refuses.
#include <cstring>
#include <string>

#include "core/events/central.h"
#include "core/fec/crc.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/settings.h"

using namespace skyblip;
using namespace skyblip::go;

// Until version 13 a unit held no Bravo pin, so a Go that updates comes back unpaired.
TEST_CASE("settings: a blob written by version-12 firmware comes back whole and unpaired") {
    struct V12 {
        uint8_t version{1};
        int16_t battery_offset_mv{0};
        int16_t frequency_trim_e1_ppm{0};
        bool battery_offset_manual{false};
        uint8_t aircraft_type{1};
        bool alarm_enabled{true};
        AlarmVolume alarm_volume{AlarmVolume::Loud};
        Units units{Units::Nautical};
        uint8_t range_step{2};
        RadarPlot plot{RadarPlot::All};
        char callsign[10]{0};
        uint32_t own_icao{0};
    };

    V12 old{};
    old.battery_offset_mv = -40;
    old.frequency_trim_e1_ppm = 12;
    old.battery_offset_manual = true;
    old.aircraft_type = 4;
    old.alarm_enabled = false;
    old.alarm_volume = AlarmVolume::Soft;
    old.units = Units::Metric;
    old.range_step = 3;
    old.plot = RadarPlot::ToScale;
    std::memcpy(old.callsign, "F-JABC", 7);
    old.own_icao = 0x3C6586;

    uint8_t blob[128] = {0};
    blob[0] = 12;
    std::memcpy(blob + 1, &old, sizeof(V12));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V12));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V12) + i] = static_cast<uint8_t>(crc >> (8 * i));

    Settings out;
    REQUIRE(from_blob(blob, 1 + sizeof(V12) + 4, out) == Status::Ok);
    CHECK(int(out.battery_offset_mv) == -40);
    CHECK(int(out.frequency_trim_e1_ppm) == 12);
    CHECK(out.battery_offset_manual);
    CHECK(int(out.aircraft_type) == 4);
    CHECK_FALSE(out.alarm_enabled);
    CHECK(out.alarm_volume == AlarmVolume::Soft);
    CHECK(out.units == Units::Metric);
    CHECK(int(out.range_step) == 3);
    CHECK(out.plot == RadarPlot::ToScale);
    CHECK(std::string(out.callsign) == "F-JABC");
    CHECK(out.own_icao == 0x3C6586u);
    CHECK_FALSE(paired(out));

    out.bravo = events::BluetoothAddress{1, {0x2A, 0x3F, 0x11, 0x22, 0x33, 0xC4}};
    uint8_t rewritten[128] = {0};
    to_blob(out, rewritten, sizeof(rewritten));
    CHECK(int(rewritten[0]) == 13);
    // The pin is a type byte and six address bytes, and one byte pads the struct to its word.
    CHECK(blob_size() == 1 + sizeof(V12) + 7 + 1 + 4);
    Settings again;
    REQUIRE(from_blob(rewritten, blob_size(), again) == Status::Ok);
    CHECK(again.bravo == out.bravo);
    CHECK(again.own_icao == 0x3C6586u);
    CHECK(std::string(again.callsign) == "F-JABC");
}

TEST_CASE("settings: a Bravo pin whose address type no controller names is refused") {
    Settings settings = defaults();
    settings.bravo.type = kBluetoothAddressTypeCeiling;
    CHECK(validate(settings) == Status::Ok);
    settings.bravo.type = kBluetoothAddressTypeCeiling + 1;
    CHECK(validate(settings) == Status::OutOfRange);
}
