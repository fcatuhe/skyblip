#include "products/skyblip_go/settings_migration.h"

#include <cstring>

#include "core/settings/blob.h"

namespace skyblip::go {

namespace {

// The version-1 payload, byte for byte: the struct as it was declared when a
// device in the field last wrote its flash. Same members in the same order means
// the same layout, so a blob written then still decodes now.
struct SettingsV1 {
    uint8_t version{1};
    uint32_t address{0};
    uint8_t address_mapping_table{0};
    uint8_t aircraft_type{4};
    uint8_t region{0};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    bool stealth{false};
    Units units{Units::Metric};
    uint8_t rotation{0};
    uint8_t page_mask{0x0F};
    bool power_save{false};
    char callsign[kCallsignBytes]{0};
};

// The version-2 payload, byte for byte: the same members version 3 has, less
// battery_offset_mv, in the order they were declared in. Same length as version
// 3 by accident of alignment, different layout by design, so nothing but the
// version byte tells them apart.
struct SettingsV2 {
    uint8_t version{1};
    uint32_t address{0};
    uint8_t address_mapping_table{0};
    uint8_t aircraft_type{4};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    bool stealth{false};
    Units units{Units::Metric};
    uint8_t page_mask{0x0F};
    char callsign[kCallsignBytes]{0};
};

// The version-3 payload, byte for byte: the same members version 4 has, less
// frequency_trim_e1_ppm.
struct SettingsV3 {
    uint8_t version{1};
    uint32_t address{0};
    int16_t battery_offset_mv{0};
    uint8_t address_mapping_table{0};
    uint8_t aircraft_type{4};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    bool stealth{false};
    Units units{Units::Metric};
    uint8_t page_mask{0x0F};
    char callsign[kCallsignBytes]{0};
};

// The version-4 payload, byte for byte: the same members version 5 has, plus the
// page_mask that no longer has a reader.
struct SettingsV4 {
    uint8_t version{1};
    uint32_t address{0};
    int16_t battery_offset_mv{0};
    int16_t frequency_trim_e1_ppm{0};
    uint8_t address_mapping_table{0};
    uint8_t aircraft_type{4};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    bool stealth{false};
    Units units{Units::Metric};
    uint8_t page_mask{0x0F};
    char callsign[kCallsignBytes]{0};
};

struct SettingsV5 {
    uint8_t version{1};
    uint32_t address{0};
    int16_t battery_offset_mv{0};
    int16_t frequency_trim_e1_ppm{0};
    uint8_t address_mapping_table{0};
    uint8_t aircraft_type{4};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    bool stealth{false};
    Units units{Units::Metric};
    char callsign[kCallsignBytes]{0};
};

struct SettingsV6 {
    uint8_t version{1};
    uint32_t address{0};
    int16_t battery_offset_mv{0};
    int16_t frequency_trim_e1_ppm{0};
    uint8_t address_mapping_table{0};
    uint8_t aircraft_type{4};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    bool stealth{false};
    bool gyroscope_enabled{false};
    Units units{Units::Metric};
    char callsign[kCallsignBytes]{0};
};

struct SettingsV7 {
    uint8_t version{1};
    uint32_t address{0};
    int16_t battery_offset_mv{0};
    int16_t frequency_trim_e1_ppm{0};
    uint8_t address_mapping_table{0};
    uint8_t aircraft_type{4};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    Units units{Units::Metric};
    char callsign[kCallsignBytes]{0};
};

struct SettingsV8 {
    uint8_t version{1};
    int16_t battery_offset_mv{0};
    int16_t frequency_trim_e1_ppm{0};
    uint8_t aircraft_type{4};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    Units units{Units::Metric};
    char callsign[kCallsignBytes]{0};
};

struct SettingsV9 {
    uint8_t version{1};
    int16_t battery_offset_mv{0};
    int16_t frequency_trim_e1_ppm{0};
    bool battery_offset_manual{false};
    uint8_t aircraft_type{4};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    Units units{Units::Metric};
    char callsign[kCallsignBytes]{0};
};

struct SettingsV10 {
    uint8_t version{1};
    int16_t battery_offset_mv{0};
    int16_t frequency_trim_e1_ppm{0};
    bool battery_offset_manual{false};
    uint8_t aircraft_type{kAircraftTypeLight};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    Units units{Units::Nautical};
    uint8_t range_step{kDefaultRangeStep};
    RadarPlot plot{RadarPlot::All};
    char callsign[kCallsignBytes]{0};
};

struct SettingsV11 {
    uint8_t version{1};
    int16_t battery_offset_mv{0};
    int16_t frequency_trim_e1_ppm{0};
    bool battery_offset_manual{false};
    uint8_t aircraft_type{kAircraftTypeLight};
    bool alarm_enabled{true};
    AlarmVolume alarm_volume{AlarmVolume::Loud};
    Units units{Units::Nautical};
    uint8_t range_step{kDefaultRangeStep};
    RadarPlot plot{RadarPlot::All};
    char callsign[kCallsignBytes]{0};
};

struct SettingsV12 {
    uint8_t version{1};
    int16_t battery_offset_mv{0};
    int16_t frequency_trim_e1_ppm{0};
    bool battery_offset_manual{false};
    uint8_t aircraft_type{kAircraftTypeLight};
    bool alarm_enabled{true};
    AlarmVolume alarm_volume{AlarmVolume::Loud};
    Units units{Units::Nautical};
    uint8_t range_step{kDefaultRangeStep};
    RadarPlot plot{RadarPlot::All};
    char callsign[kCallsignBytes]{0};
    uint32_t own_icao{kOwnIcaoUnset};
};

static_assert(sizeof(SettingsV11) == sizeof(SettingsV10), "the volume changed scale, not width");
static_assert(sizeof(SettingsV12) == sizeof(SettingsV11) + sizeof(uint32_t),
              "own_icao went in where it costs no padding");
static_assert(sizeof(Settings) == sizeof(SettingsV12) + sizeof(events::BluetoothAddress) + 1,
              "the Bravo pin went on the end, with one byte of padding to the next word");

// INFO: fc 15oct26 the old 3 to 5 all drove half duty, the loudest; 1 and 2 were the quieter ones
AlarmVolume three_step_volume(uint8_t six_step) {
    if (six_step == 0) return AlarmVolume::Off;
    if (six_step < 3) return AlarmVolume::Soft;
    return AlarmVolume::Loud;
}

void migrate_v1(const SettingsV1& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = three_step_volume(old.alarm_volume);
    out.units = old.units;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
}

// A unit that stored its settings before this firmware existed was never
// calibrated, so it comes back with no trim - which is the reading it has been
// giving all along, not a change of behaviour on an upgrade.
void migrate_v2(const SettingsV2& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = three_step_volume(old.alarm_volume);
    out.units = old.units;
    out.battery_offset_mv = 0;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
}

// A unit that stored its settings before the frequency trim existed was never
// measured on a spectrum analyser, so it comes back on the reference its TCXO
// actually has - the frequency it has been transmitting on all along, not a
// correction invented by an upgrade.
void migrate_v3(const SettingsV3& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.battery_offset_mv = old.battery_offset_mv;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = three_step_volume(old.alarm_volume);
    out.units = old.units;
    out.frequency_trim_e1_ppm = 0;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
}

// A unit that stored a page mask stored a choice about six pages that no longer
// exist: the pad walks three and the two that open everything else always stand.
void migrate_v4(const SettingsV4& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.battery_offset_mv = old.battery_offset_mv;
    out.frequency_trim_e1_ppm = old.frequency_trim_e1_ppm;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = three_step_volume(old.alarm_volume);
    out.units = old.units;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
}

void migrate_v5(const SettingsV5& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.battery_offset_mv = old.battery_offset_mv;
    out.frequency_trim_e1_ppm = old.frequency_trim_e1_ppm;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = three_step_volume(old.alarm_volume);
    out.units = old.units;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
}

void migrate_v6(const SettingsV6& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.battery_offset_mv = old.battery_offset_mv;
    out.frequency_trim_e1_ppm = old.frequency_trim_e1_ppm;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = three_step_volume(old.alarm_volume);
    out.units = old.units;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
}

void migrate_v8(const SettingsV8& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.battery_offset_mv = old.battery_offset_mv;
    out.battery_offset_manual = old.battery_offset_mv != 0;
    out.frequency_trim_e1_ppm = old.frequency_trim_e1_ppm;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = three_step_volume(old.alarm_volume);
    out.units = old.units;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
}

// A unit from before the radar's range and plot were kept comes back on the ones a new unit ships
// on.
void migrate_v9(const SettingsV9& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.battery_offset_mv = old.battery_offset_mv;
    out.battery_offset_manual = old.battery_offset_manual;
    out.frequency_trim_e1_ppm = old.frequency_trim_e1_ppm;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = three_step_volume(old.alarm_volume);
    out.units = old.units;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
}

void migrate_v10(const SettingsV10& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.battery_offset_mv = old.battery_offset_mv;
    out.battery_offset_manual = old.battery_offset_manual;
    out.frequency_trim_e1_ppm = old.frequency_trim_e1_ppm;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = three_step_volume(old.alarm_volume);
    out.units = old.units;
    out.range_step = old.range_step;
    out.plot = old.plot;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
}

void migrate_v7(const SettingsV7& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.battery_offset_mv = old.battery_offset_mv;
    out.frequency_trim_e1_ppm = old.frequency_trim_e1_ppm;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = three_step_volume(old.alarm_volume);
    out.units = old.units;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
}

void migrate_v11(const SettingsV11& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.battery_offset_mv = old.battery_offset_mv;
    out.battery_offset_manual = old.battery_offset_manual;
    out.frequency_trim_e1_ppm = old.frequency_trim_e1_ppm;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = old.alarm_volume;
    out.units = old.units;
    out.range_step = old.range_step;
    out.plot = old.plot;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
    out.own_icao = kOwnIcaoUnset;
}

void migrate_v12(const SettingsV12& old, Settings& out) {
    out = Settings{};
    out.version = Settings::kCurrentVersion;
    out.battery_offset_mv = old.battery_offset_mv;
    out.battery_offset_manual = old.battery_offset_manual;
    out.frequency_trim_e1_ppm = old.frequency_trim_e1_ppm;
    out.aircraft_type = old.aircraft_type;
    out.alarm_enabled = old.alarm_enabled;
    out.alarm_volume = old.alarm_volume;
    out.units = old.units;
    out.range_step = old.range_step;
    out.plot = old.plot;
    std::memcpy(out.callsign, old.callsign, kCallsignBytes);
    out.callsign[kCallsignBytes - 1] = 0;
    out.own_icao = old.own_icao;
}

template <class Old>
Status open_as(const uint8_t* in, size_t length, Settings& out,
               void (*migrate)(const Old&, Settings&)) {
    Old old;
    const Status status = settings::open(in, length, sizeof(Old), &old);
    if (status == Status::Ok) migrate(old, out);
    return status;
}

}  // namespace

Status migrate_blob(uint8_t version, const uint8_t* in, size_t length, Settings& out) {
    switch (version) {
        case 12: return open_as<SettingsV12>(in, length, out, migrate_v12);
        case 11: return open_as<SettingsV11>(in, length, out, migrate_v11);
        case 10: return open_as<SettingsV10>(in, length, out, migrate_v10);
        case 9: return open_as<SettingsV9>(in, length, out, migrate_v9);
        case 8: return open_as<SettingsV8>(in, length, out, migrate_v8);
        case 7: return open_as<SettingsV7>(in, length, out, migrate_v7);
        case 6: return open_as<SettingsV6>(in, length, out, migrate_v6);
        case 5: return open_as<SettingsV5>(in, length, out, migrate_v5);
        case 4: return open_as<SettingsV4>(in, length, out, migrate_v4);
        case 3: return open_as<SettingsV3>(in, length, out, migrate_v3);
        case 2: return open_as<SettingsV2>(in, length, out, migrate_v2);
        case 1: return open_as<SettingsV1>(in, length, out, migrate_v1);
        default: return Status::Unsupported;
    }
}

}  // namespace skyblip::go
