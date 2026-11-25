#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_H

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/events/central.h"
#include "core/power/cable.h"
#include "core/settings/address.h"
#include "core/util/cbor.h"
#include "core/util/result.h"
#include "products/common/alarm.h"

namespace skyblip::go {

enum class Units : uint8_t { Nautical = 0, Metric = 1 };

enum class RadarPlot : uint8_t { All = 0, ToScale = 1 };

using common::Alarm;

constexpr int kRangeStepCount = 4;
constexpr int kDefaultRangeStep = 2;

constexpr size_t kCallsignBytes = 10;

// INFO: fc 20nov26 a TCXO needs no trim, so the bound is a tenfold datasheet miss, +-10 ppm, which
// keeps the carrier in ERC 70-03 h1.4
constexpr int16_t kFrequencyTrimCeilingE1Ppm = 100;

// INFO: fc 12sep26 ADS-L G.1.3's light aircraft, what a device says until a pilot says otherwise
constexpr uint8_t kAircraftTypeLight = 1;

constexpr uint32_t kAircraftAddressUnset = 0;

// INFO: fc 11nov26 BT_ADDR_LE_RANDOM_ID, the last LE address type (zephyr/bluetooth/addr.h)
constexpr uint8_t kBluetoothAddressTypeCeiling = 3;

struct Settings {
    uint8_t version{kCurrentVersion};
    // INFO: fc 20oct26 set-only: read back as the millivolts the status reply and the glass show
    int16_t battery_offset_mv{0};
    // INFO: fc 20oct26 tenths of a ppm, positive upwards, set-only: the bench's spectrum analyser
    // is the record
    int16_t frequency_trim_e1_ppm{0};
    // INFO: fc 05oct26 every trim is a person's since the charger learner went, kept for the blob
    bool battery_offset_manual{false};
    uint8_t aircraft_type{kAircraftTypeLight};
    Alarm alarm{Alarm::Loud};
    Units units{Units::Nautical};
    uint8_t range_step{kDefaultRangeStep};
    RadarPlot plot{RadarPlot::All};
    char callsign[kCallsignBytes]{0};
    // INFO: fc 11nov26 stored whole, so an F.2.3 table-5 transmitter can read it unchanged
    uint32_t aircraft_address{kAircraftAddressUnset};
    // INFO: fc 11nov26 the pinned Bravo, its type with its address, all zero while unpaired
    events::BluetoothAddress bravo{};
    // INFO: fc 26nov26 in the padding byte after bravo, so a version 14 blob keeps its size
    power::Switch power_switch{power::Switch::Button};

    // INFO: fc 20nov26 the link contract of schemas/link.v1.json: it moves when a key changes
    // meaning, not when the flash layout does
    static constexpr uint8_t kCurrentVersion = 1;
};

constexpr uint8_t kBlobVersion = 15;
// INFO: fc 26nov26 v0.8.0 shipped it, so its blob loads with power_switch on Button
constexpr uint8_t kBlobVersionBeforePowerSwitch = 14;

Settings defaults();

inline bool paired(const Settings& settings) {
    return settings.bravo != events::BluetoothAddress{};
}

// INFO: fc 17nov26 soft and loud sound on the Go's own parts, or on Bravo's alone for a plain Go
inline bool alarm_sounds(const Settings& settings, bool sounds) {
    return sounds || paired(settings);
}

// INFO: fc 20nov26 the one validation, so no path stores a value another path would refuse
Status validate(const Settings& settings);

// INFO: fc 14nov26 ADS-L F.2.3 inconsistency: an aircraft address other than the transponder Bravo
// names
bool contradicts(uint32_t aircraft_address, std::optional<uint32_t> own_transponder);

size_t blob_size();
void to_blob(const Settings& settings, uint8_t* out, size_t capacity);
Status from_blob(const uint8_t* in, size_t length, Settings& out);

// INFO: fc 04aug26 the fields alone, into the caller's writer, so the config reply is one map
void write_config_fields(cbor::Writer& writer, const Settings& settings,
                         settings::AirAddress address);

// INFO: fc 03oct26 the pilot's keys only, so a "defaults" reply is a "set" as it stands
void write_settings_fields(cbor::Writer& writer, const Settings& settings);

Status apply_settings(Settings& settings, const cbor::Reader& frame,
                      std::optional<uint32_t> own_transponder = std::nullopt);

}  // namespace skyblip::go

#endif
