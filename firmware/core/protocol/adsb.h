#ifndef SKYBLIP_CORE_PROTOCOL_ADSB_H
#define SKYBLIP_CORE_PROTOCOL_ADSB_H

#include <cstddef>
#include <cstdint>

#include "core/util/result.h"

namespace skyblip::protocol {

constexpr uint8_t kAdsbFrameBytes = 14;
// INFO: fc 10nov26 0x95 is the chips of the bits 1000, which DF16 and DF17 share
constexpr uint8_t kAdsbSyncChips = 0x95;
constexpr uint8_t kAdsbChipBytes = 2 * kAdsbFrameBytes - 1;
constexpr uint8_t kAdsbCallsignCharacters = 8;

struct AdsbReception {
    uint64_t at_us{0};
    int8_t rssi_dbm{0};
};

struct AdsbQuantity {
    int32_t value{0};
    bool valid{false};
    bool saturated{false};
};

struct AdsbCpr {
    bool odd{false};
    uint32_t latitude_cpr{0};
    uint32_t longitude_cpr{0};
};

enum class AdsbKind : uint8_t {
    Identification,
    SurfacePosition,
    AirbornePosition,
    Velocity,
    OperationalStatus,
};

struct AdsbIdentification {
    char category_set{0};
    uint8_t category{0};
    char callsign[kAdsbCallsignCharacters + 1]{};
    bool callsign_valid{false};
};

struct AdsbSurfacePosition {
    AdsbQuantity speed_mkt;
    uint8_t track_c7{0};
    bool track_valid{false};
    bool utc_synchronized{false};
    AdsbCpr cpr;
};

struct AdsbAirbornePosition {
    int32_t pressure_altitude_ft{0};
    bool pressure_altitude_valid{false};
    int32_t gnss_height_ft{0};
    bool gnss_height_valid{false};
    bool utc_synchronized{false};
    AdsbCpr cpr;
};

enum class AdsbHeightSource : uint8_t { Gnss, Pressure };
enum class AdsbAirspeed : uint8_t { Indicated, True };

struct AdsbVelocity {
    uint8_t subtype{0};
    AdsbQuantity east_kt;
    AdsbQuantity north_kt;
    AdsbQuantity airspeed_kt;
    AdsbAirspeed airspeed_type{AdsbAirspeed::Indicated};
    uint16_t heading_c10{0};
    bool heading_valid{false};
    AdsbQuantity climb_fpm;
    AdsbHeightSource climb_source{AdsbHeightSource::Gnss};
    AdsbQuantity gnss_minus_pressure_ft;
};

struct AdsbOperationalStatus {
    uint8_t subtype{0};
    uint8_t version{0};
    uint8_t gva{0};
    bool gva_valid{false};
};

struct AdsbMessage {
    AdsbKind kind{AdsbKind::Identification};
    AdsbReception reception;
    uint32_t address{0};
    uint8_t capability{0};
    uint8_t type_code{0};
    AdsbIdentification identification;
    AdsbSurfacePosition surface_position;
    AdsbAirbornePosition airborne_position;
    AdsbVelocity velocity;
    AdsbOperationalStatus operational_status;
};

Status adsb_receive(const uint8_t* chips, size_t chip_bytes, AdsbReception reception,
                    AdsbMessage& out);

}

#endif
