#include "products/skyblip_go/settings.h"

#include <cstring>

#include "core/power/battery.h"
#include "core/protocol/adsl.h"
#include "core/settings/address.h"
#include "core/settings/blob.h"
#include "core/util/json.h"

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

constexpr size_t kPayloadV1 = sizeof(SettingsV1);
constexpr size_t kPayloadV2 = sizeof(SettingsV2);
constexpr size_t kPayloadV3 = sizeof(SettingsV3);
constexpr size_t kPayloadV4 = sizeof(SettingsV4);
constexpr size_t kPayloadV5 = sizeof(SettingsV5);
constexpr size_t kPayloadV6 = sizeof(SettingsV6);
constexpr size_t kPayloadV7 = sizeof(SettingsV7);
constexpr size_t kPayloadV8 = sizeof(SettingsV8);
constexpr size_t kPayloadV9 = sizeof(SettingsV9);
constexpr size_t kPayloadV10 = sizeof(SettingsV10);
constexpr size_t kPayload = sizeof(Settings);
static_assert(kPayloadV10 == kPayload, "the volume changed scale, not width");

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

bool callsign_is_typeable(const char* callsign) {
    for (size_t i = 0; i < kCallsignBytes; i++) {
        if (callsign[i] == 0) return true;
        if (!protocol::is_callsign_character(callsign[i])) return false;
    }
    return false;
}

bool fits_uint8(long value) { return value >= 0 && value <= UINT8_MAX; }

}  // namespace

Settings defaults() { return Settings{}; }

Status validate(const Settings& settings) {
    if (settings.version != Settings::kCurrentVersion) return Status::Unsupported;
    if (settings.aircraft_type > 17) return Status::OutOfRange;
    if (settings.alarm_volume > AlarmVolume::Loud) return Status::OutOfRange;
    if (settings.range_step >= kRangeStepCount) return Status::OutOfRange;
    if (settings.plot != RadarPlot::All && settings.plot != RadarPlot::ToScale)
        return Status::OutOfRange;
    if (settings.battery_offset_mv > power::kCalibrationCeilingMv ||
        settings.battery_offset_mv < -power::kCalibrationCeilingMv)
        return Status::OutOfRange;
    if (settings.frequency_trim_e1_ppm > kFrequencyTrimCeilingE1Ppm ||
        settings.frequency_trim_e1_ppm < -kFrequencyTrimCeilingE1Ppm)
        return Status::OutOfRange;
    if (!callsign_is_typeable(settings.callsign)) return Status::Invalid;
    return Status::Ok;
}

size_t blob_size() { return settings::blob_bytes(kPayload); }

void to_blob(const Settings& settings, uint8_t* out, size_t capacity) {
    settings::seal(kBlobVersion, &settings, kPayload, out, capacity);
}

Status from_blob(const uint8_t* in, size_t length, Settings& out) {
    if (length < settings::kBlobOverhead) return Status::Crc;

    const uint8_t version = settings::blob_version(in);
    if (version == kBlobVersion) {
        const Status status = settings::open(in, length, kPayload, &out);
        if (status != Status::Ok) return status;
    } else if (version == 10) {
        SettingsV10 old;
        const Status status = settings::open(in, length, kPayloadV10, &old);
        if (status != Status::Ok) return status;
        migrate_v10(old, out);
    } else if (version == 9) {
        SettingsV9 old;
        const Status status = settings::open(in, length, kPayloadV9, &old);
        if (status != Status::Ok) return status;
        migrate_v9(old, out);
    } else if (version == 8) {
        SettingsV8 old;
        const Status status = settings::open(in, length, kPayloadV8, &old);
        if (status != Status::Ok) return status;
        migrate_v8(old, out);
    } else if (version == 7) {
        SettingsV7 old;
        const Status status = settings::open(in, length, kPayloadV7, &old);
        if (status != Status::Ok) return status;
        migrate_v7(old, out);
    } else if (version == 6) {
        SettingsV6 old;
        const Status status = settings::open(in, length, kPayloadV6, &old);
        if (status != Status::Ok) return status;
        migrate_v6(old, out);
    } else if (version == 5) {
        SettingsV5 old;
        const Status status = settings::open(in, length, kPayloadV5, &old);
        if (status != Status::Ok) return status;
        migrate_v5(old, out);
    } else if (version == 4) {
        SettingsV4 old;
        const Status status = settings::open(in, length, kPayloadV4, &old);
        if (status != Status::Ok) return status;
        migrate_v4(old, out);
    } else if (version == 3) {
        SettingsV3 old;
        const Status status = settings::open(in, length, kPayloadV3, &old);
        if (status != Status::Ok) return status;
        migrate_v3(old, out);
    } else if (version == 2) {
        SettingsV2 old;
        const Status status = settings::open(in, length, kPayloadV2, &old);
        if (status != Status::Ok) return status;
        migrate_v2(old, out);
    } else if (version == 1) {
        SettingsV1 old;
        const Status status = settings::open(in, length, kPayloadV1, &old);
        if (status != Status::Ok) return status;
        migrate_v1(old, out);
    } else {
        return Status::Unsupported;
    }
    // INFO: fc 23sep26 a name an older build took over the link is dropped, not the trims beside it
    if (!callsign_is_typeable(out.callsign)) out.callsign[0] = 0;
    if (validate(out) != Status::Ok) return Status::Invalid;
    return Status::Ok;
}

void write_json_fields(json::Writer& writer, const Settings& settings,
                       settings::AirAddress address) {
    writer.add_integer("version", settings.version);
    if (address) writer.add_integer("address", static_cast<long>(*address));
    writer.add_integer("address_mapping_table", settings::kAddressMappingTable);
    write_json_settings(writer, settings);
}

void write_json_settings(json::Writer& writer, const Settings& settings) {
    writer.add_integer("aircraft_type", settings.aircraft_type);
    writer.add_boolean("alarm_enabled", settings.alarm_enabled);
    writer.add_integer("alarm_volume", static_cast<long>(settings.alarm_volume));
    writer.add_integer("units", static_cast<long>(settings.units));
    writer.add_string("callsign", settings.callsign);
}

int to_json(const Settings& settings, settings::AirAddress address, char* out, int capacity) {
    json::Writer writer(out, capacity);
    write_json_fields(writer, settings, address);
    return writer.finish();
}

Status apply_json(Settings& settings, const char* json, int length) {
    json::Reader reader(json, length);
    long value;
    bool flag;
    Settings next = settings;
    if (reader.read_integer("aircraft_type", value)) {
        if (!fits_uint8(value)) return Status::OutOfRange;
        next.aircraft_type = static_cast<uint8_t>(value);
    }
    if (reader.read_boolean("alarm_enabled", flag)) next.alarm_enabled = flag;
    if (reader.read_integer("alarm_volume", value)) {
        if (!fits_uint8(value)) return Status::OutOfRange;
        next.alarm_volume = static_cast<AlarmVolume>(value);
    }
    if (reader.read_integer("units", value)) next.units = value ? Units::Metric : Units::Nautical;
    // Narrowed before it is validated, not after: 65536 truncates to 0 in an
    // int16 and would pass a bound check that never saw the value sent.
    if (reader.read_integer("battery_offset_mv", value)) {
        if (value < -power::kCalibrationCeilingMv || value > power::kCalibrationCeilingMv)
            return Status::OutOfRange;
        next.battery_offset_mv = static_cast<int16_t>(value);
        next.battery_offset_manual = true;
    }
    if (reader.read_integer("frequency_trim_e1_ppm", value)) {
        if (value < -kFrequencyTrimCeilingE1Ppm || value > kFrequencyTrimCeilingE1Ppm)
            return Status::OutOfRange;
        next.frequency_trim_e1_ppm = static_cast<int16_t>(value);
    }
    char callsign[kCallsignBytes + 1] = {0};
    if (reader.read_string("callsign", callsign, sizeof(callsign))) {
        if (std::strlen(callsign) >= kCallsignBytes) return Status::OutOfRange;
        std::memcpy(next.callsign, callsign, kCallsignBytes);
    }
    Status status = validate(next);
    if (status != Status::Ok) return status;
    settings = next;
    return Status::Ok;
}

}  // namespace skyblip::go
