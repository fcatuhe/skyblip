#ifndef SKYBLIP_PROTOCOLS_ADSB_MESSAGE_H
#define SKYBLIP_PROTOCOLS_ADSB_MESSAGE_H

#include <cstddef>
#include <cstdint>

#include "protocols/common/status.h"

namespace skyblip::adsb {

constexpr uint8_t kFrameBytes = 14;
// INFO: fc 10nov26 0x95 is the chips of the bits 1000, which DF16 and DF17 share
constexpr uint8_t kSyncChips = 0x95;
constexpr uint8_t kChipBytes = 2 * kFrameBytes - 1;
constexpr uint8_t kCallsignCharacters = 8;

struct MovementBand {
    uint8_t first_code;
    int32_t floor_mkt;
    int32_t step_mkt;
};

// INFO: fc 10nov26 Doc 9871 A.2.3.3.1: codes 125 to 127 are reserved, 124 is 175 kt or more
inline constexpr MovementBand kMovementBands[] = {
    {1, 0, 0},         {2, 125, 125},     {9, 1000, 250},      {13, 2000, 500},
    {39, 15000, 1000}, {94, 70000, 2000}, {109, 100000, 5000}, {124, 175000, 0},
};
constexpr uint8_t kMovementSaturatedCode = 124;

struct Reception {
    uint64_t at_us{0};
    int8_t rssi_dbm{0};
    bool rssi_valid{false};
};

struct Quantity {
    int32_t value{0};
    bool valid{false};
    bool saturated{false};
};

struct Cpr {
    bool odd{false};
    uint32_t latitude_cpr{0};
    uint32_t longitude_cpr{0};
};

enum class Kind : uint8_t {
    Identification,
    SurfacePosition,
    AirbornePosition,
    Velocity,
    OperationalStatus,
};

struct Identification {
    char category_set{0};
    uint8_t category{0};
    char callsign[kCallsignCharacters + 1]{};
    bool callsign_valid{false};
};

struct SurfacePosition {
    Quantity speed_mkt;
    uint8_t track_c7{0};
    bool track_valid{false};
    bool utc_synchronized{false};
    Cpr cpr;
};

struct AirbornePosition {
    int32_t pressure_altitude_ft{0};
    bool pressure_altitude_valid{false};
    int32_t gnss_height_ft{0};
    bool gnss_height_valid{false};
    bool utc_synchronized{false};
    Cpr cpr;
};

enum class HeightSource : uint8_t { Gnss, Pressure };
enum class Airspeed : uint8_t { Indicated, True };

struct Velocity {
    uint8_t subtype{0};
    Quantity east_kt;
    Quantity north_kt;
    Quantity airspeed_kt;
    Airspeed airspeed_type{Airspeed::Indicated};
    uint16_t heading_c10{0};
    bool heading_valid{false};
    Quantity climb_fpm;
    HeightSource climb_source{HeightSource::Gnss};
    Quantity gnss_minus_pressure_ft;
};

struct OperationalStatus {
    uint8_t subtype{0};
    uint8_t version{0};
    uint8_t gva{0};
    bool gva_valid{false};
};

struct Message {
    Kind kind{Kind::Identification};
    Reception reception;
    uint32_t address{0};
    uint8_t capability{0};
    uint8_t type_code{0};
    Identification identification;
    SurfacePosition surface_position;
    AirbornePosition airborne_position;
    Velocity velocity;
    OperationalStatus operational_status;
};

wire::Status receive(const uint8_t* chips, size_t chip_bytes, Reception reception, Message& out);

}

#endif
