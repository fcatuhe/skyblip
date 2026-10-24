// products/skyblip_go/settings.h: the config struct a pilot can change, its defaults,
// and the two framings it survives in: a versioned CRC blob for flash, CBOR for
// the companion link. Validation is one function, so no path can store a value
// another path would refuse.
//
// Every field here has a reader outside this module. A page that accepts a
// setting the firmware ignores is worse than one that does not offer it, so a
// field that loses its last reader leaves the struct, the link and the schema
// together, and the blob layout is versioned so a device that stored the old
// one still comes back up as itself.
#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_H

#include <cstddef>
#include <cstdint>

#include "core/settings/address.h"
#include "core/util/cbor.h"
#include "core/util/result.h"

namespace skyblip::go {

enum class Units : uint8_t { Nautical = 0, Metric = 1 };

enum class RadarPlot : uint8_t { All = 0, ToScale = 1 };

enum class AlarmVolume : uint8_t { Off = 0, Soft = 1, Loud = 2 };

constexpr int kRangeStepCount = 4;
constexpr int kDefaultRangeStep = 2;

constexpr size_t kCallsignBytes = 10;

// The most the radio's centre frequency may be trimmed, in tenths of a ppm, and
// the reason there is a trim at all. The board fits a TCXO, so zero is the
// design intent and SoftRF hard-codes exactly that for the SX1262; OGN keeps the
// correction because it also runs on crystal parts (src/parameters.h:52-54). What
// a TCXO does not give us is a way to find out: there is no measurement on the
// device, so if a batch of references comes in ten times worse than its
// datasheet, a field with no way to correct it is a recall. The bound is that
// tenfold miss and no more (+-10 ppm, +-8.7 kHz at 868.2 MHz): inside it the
// carrier cannot leave ERC 70-03 band h1.4, and beyond it the part is broken
// rather than out of trim. sx1262::kFrequencyTrimCeilingE1Ppm is the same number where
// the PLL word is computed.
constexpr int16_t kFrequencyTrimCeilingE1Ppm = 100;

// INFO: fc 12sep26 ADS-L G.1.3's light aircraft, what a device says until a pilot says otherwise
constexpr uint8_t kAircraftTypeLight = 1;

struct Settings {
    uint8_t version{kCurrentVersion};
    // The per-unit battery trim, core/power/battery.h's kCalibrationCeilingMv
    // either way, applied to every reading before the gauge or the cutoff sees
    // it. Placed here rather than at the end because a signed 16-bit value after
    // the 32-bit address costs no padding, so the blob is the same length it was.
    //
    // INFO: fc 20oct26 set-only: read back as the millivolts the status reply and the panel show
    int16_t battery_offset_mv{0};
    // The radio's frequency trim in tenths of a ppm, positive upwards, applied
    // to every dwell the radio arms (hardware/parts/sx1262). Beside
    // battery_offset_mv because the pair of 16-bit trims fills one word with no
    // padding, and because they are the same kind of field: a per-unit
    // correction set on a bench, not a pilot's setting.
    //
    // INFO: fc 20oct26 set-only and never read back: the bench's spectrum analyser is the record
    int16_t frequency_trim_e1_ppm{0};
    // INFO: fc 05oct26 every trim is a person's since the charger learner went, kept for the blob
    bool battery_offset_manual{false};
    uint8_t aircraft_type{kAircraftTypeLight};
    bool alarm_enabled{true};
    AlarmVolume alarm_volume{AlarmVolume::Loud};
    Units units{Units::Nautical};
    uint8_t range_step{kDefaultRangeStep};
    RadarPlot plot{RadarPlot::All};
    char callsign[kCallsignBytes]{0};

    // The companion-link contract, which the config frame of schemas/link.v1.json
    // describes. It moves when a key changes meaning, not when the flash layout does.
    static constexpr uint8_t kCurrentVersion = 1;
};

constexpr uint8_t kBlobVersion = 11;

Settings defaults();

Status validate(const Settings& settings);

size_t blob_size();
void to_blob(const Settings& settings, uint8_t* out, size_t capacity);
Status from_blob(const uint8_t* in, size_t length, Settings& out);

// INFO: fc 04aug26 the fields alone, into the caller's writer, so the config reply is one map
void write_config_fields(cbor::Writer& writer, const Settings& settings,
                         settings::AirAddress address);

// INFO: fc 03oct26 the pilot's keys only, so a "defaults" reply is a "set" as it stands
void write_settings_fields(cbor::Writer& writer, const Settings& settings);

Status apply_settings(Settings& settings, const cbor::Reader& frame);

}  // namespace skyblip::go

#endif
