#ifndef SKYBLIP_CORE_PROTOCOL_NMEA_OUT_H
#define SKYBLIP_CORE_PROTOCOL_NMEA_OUT_H

#include <cstddef>
#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"

namespace skyblip::protocol {

// INFO: fc 10nov26 NMEA 0183 caps a sentence at 82 characters, $ to CRLF
constexpr int kNmeaSentenceCeilingBytes = 82;

int nmea_finish(char* sentence, int body_bytes);

uint8_t adsl_category_to_alptas_type(uint8_t adsl_category);
uint8_t address_mapping_table_to_id_type(uint8_t address_mapping_table);

// INFO: fc 20sep26 callsign rides the ID field behind a '!', as OGN's does: lookout.h:144
// INFO: fc 10nov26 XCSoar drops a $PFLAA without RelativeVertical, and a 0 there draws it level
int format_pflaa(char* out, size_t capacity, const model::Ownship& own,
                 const model::AircraftObservation& target, uint8_t alarm_level,
                 const char* callsign);

// INFO: fc 19sep26 the GPS field: 0 no fix, 1 a 3D fix on the ground, 2 a 3D fix moving
uint8_t pflau_gps(const model::Ownship& own);

// INFO: fc 19sep26 the TX field is whether a burst can leave now: core/timing/transmit.h
// INFO: fc 20nov26 relative_bearing_deg is signed, so a threat off the left wing is not at 340
int format_pflau(char* out, size_t capacity, const model::Ownship& own, bool transmitting,
                 int target_count, const model::AircraftObservation* threat, uint8_t alarm_level,
                 int16_t relative_bearing_deg, int32_t relative_altitude_m,
                 bool relative_altitude_valid, int32_t relative_distance_m);

// INFO: fc 20nov26 Ownship has no 2D or 3D flag, so fix_valid sends 3 or 1, as SoftRF does
int format_pgrmz(char* out, size_t capacity, int32_t altitude_ft, bool fix_valid);

constexpr uint32_t kLk8NoPressurePa = 999999;
constexpr int32_t kLk8NoAltitudeM = 99999;
constexpr int32_t kLk8NoVarioCmS = 9999;
constexpr int32_t kLk8NoTemperatureC = 99;
constexpr uint32_t kLk8NoBattery = 999;

constexpr uint32_t kLk8BatteryPercentBase = 1000;

constexpr uint32_t kLk8PressureCeilingPa = kLk8NoPressurePa - 1;
constexpr int32_t kLk8AltitudeFloorM = -1000;
constexpr int32_t kLk8AltitudeCeilingM = kLk8NoAltitudeM - 1;
constexpr int32_t kLk8VarioCeilingCmS = kLk8NoVarioCmS - 1;
constexpr int32_t kLk8VarioFloorCmS = -kLk8VarioCeilingCmS;
constexpr int32_t kLk8TemperatureFloorC = -99;
constexpr int32_t kLk8TemperatureCeilingC = kLk8NoTemperatureC - 1;

struct Lk8Ex1 {
    uint32_t pressure_pa{0};
    int32_t altitude_m{0};
    int32_t vario_cm_s{0};
    int32_t temperature_c{0};
    uint8_t battery_percent{0};
    bool pressure_valid{false};
    bool altitude_valid{false};
    bool vario_valid{false};
    bool temperature_valid{false};
    bool battery_valid{false};
};

int format_lk8ex1(char* out, size_t capacity, const Lk8Ex1& reading);

int format_gprmc(char* out, size_t capacity, const model::Ownship& own);
int format_gpgga(char* out, size_t capacity, const model::Ownship& own);

}

#endif
