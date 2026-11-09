// core/protocol/adsl.h: ADS-L 4 SRD-860 Issue 2 iConspicuity (position) packet.
#ifndef SKYBLIP_CORE_PROTOCOL_ADSL_H
#define SKYBLIP_CORE_PROTOCOL_ADSL_H

#include <cstdint>
#include <cstring>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/util/bit_field.h"
#include "core/util/exponential.h"

namespace skyblip::protocol {

struct __attribute__((packed)) AdslPacket {
    static constexpr uint8_t kTxBytes = 27;
    static constexpr uint8_t kSync1 = 0x72;
    static constexpr uint8_t kSync2 = 0x4B;
    static constexpr uint8_t kDataBytes = kTxBytes - 3;
    static constexpr uint8_t kCrcCoverBytes = kTxBytes - 6;
    static constexpr uint8_t kPayloadBytes = 20;
    // INFO: fc 20sep26 F.2.1 leaves payload 66 to OGN, layout: nrf52-ogn-tracker/src/adsl.h:120
    static constexpr uint8_t kPayloadTypePosition = 0x02;
    static constexpr uint8_t kPayloadTypeOgnDiagnostics = 0x42;
    static constexpr uint8_t kTelemetryTypeInformation = 1;
    static constexpr uint8_t kInformationTypeRegistration = 5;
    static constexpr int kInformationHeaderOffset = 5;
    static constexpr int kInformationTextOffset = 6;
    static constexpr int kInformationTextBytes = 14;

    uint8_t sync[2];
    uint8_t data_bytes;
    union {
        uint8_t data[kDataBytes];
        struct __attribute__((packed)) {
            uint8_t version;
            union {
                uint8_t payload[kPayloadBytes];
                struct __attribute__((packed)) {
                    uint8_t payload_type;
                    uint8_t address_bytes[4];
                    uint8_t timestamp : 6;
                    uint8_t flight_state : 2;
                    uint8_t aircraft_category : 5;
                    uint8_t emergency : 3;
                    uint8_t motion[11];
                    uint8_t source_integrity : 2;
                    uint8_t design_assurance : 2;
                    uint8_t navigation_integrity : 4;
                    uint8_t horizontal_accuracy : 3;
                    uint8_t vertical_accuracy : 2;
                    uint8_t velocity_accuracy : 2;
                    uint8_t reserved : 1;
                };
            };
            uint8_t crc_bytes[3];
        };
    };
    uint8_t spare;

    // INFO: fc 09nov26 offsets count from payload bit 0: 40 bits of F.2 header, then G.1
    static constexpr uint16_t kTrafficOffset = 40;
    static constexpr BitField kAddressMappingTableField{8, 6};
    static constexpr BitField kAddressField{14, 24};
    static constexpr BitField kRelayField{39, 1};
    static constexpr BitField kInformationTypeField{kTrafficOffset, 6};
    static constexpr BitField kTelemetryTypeField{kTrafficOffset + 6, 2};
    static constexpr BitField kLatitudeField{kTrafficOffset + 16, 24};
    static constexpr BitField kLongitudeField{kTrafficOffset + 40, 24};
    static constexpr BitField kSpeedField{kTrafficOffset + 64, 8};
    static constexpr BitField kAltitudeField{kTrafficOffset + 72, 14};
    static constexpr BitField kClimbField{kTrafficOffset + 86, 9};
    static constexpr BitField kTrackField{kTrafficOffset + 95, 9};

    static constexpr Exponential kSpeedCode{6, 2, false};
    static constexpr Exponential kAltitudeCode{12, 2, false};
    static constexpr Exponential kClimbCode{6, 2, true};

    // INFO: fc 09nov26 G.1.5: a latitude step is 1/93206 degree, a longitude step 1/46603
    static constexpr int64_t kLatitudeStepsPerDegree = 93206;
    static constexpr int64_t kLongitudeStepsPerDegree = 46603;

    void reset(uint8_t type = kPayloadTypePosition) {
        __builtin_memset(this, 0, sizeof(*this));
        sync[0] = kSync1;
        sync[1] = kSync2;
        data_bytes = kDataBytes;
        payload_type = type;
    }

    uint32_t field(BitField where) const { return read_bits(payload, where); }
    void set_field(BitField where, uint32_t value) { write_bits(payload, where, value); }

    bool is_position() const { return payload_type == kPayloadTypePosition; }

    uint8_t telemetry_type() const { return static_cast<uint8_t>(field(kTelemetryTypeField)); }
    uint8_t information_type() const { return static_cast<uint8_t>(field(kInformationTypeField)); }
    void set_information_header(uint8_t telemetry_type, uint8_t information_type) {
        set_field(kTelemetryTypeField, telemetry_type);
        set_field(kInformationTypeField, information_type);
    }
    bool is_registration() const {
        return payload_type == kPayloadTypeOgnDiagnostics &&
               telemetry_type() == kTelemetryTypeInformation &&
               information_type() == kInformationTypeRegistration;
    }
    const char* information_text() const {
        return reinterpret_cast<const char*>(payload + kInformationTextOffset);
    }
    char* information_text() { return reinterpret_cast<char*>(payload + kInformationTextOffset); }

    uint32_t address() const { return field(kAddressField); }
    void set_address(uint32_t address) { set_field(kAddressField, address); }
    uint8_t address_mapping_table() const {
        return static_cast<uint8_t>(field(kAddressMappingTableField));
    }
    void set_address_mapping_table(uint8_t table) { set_field(kAddressMappingTableField, table); }
    uint32_t table_and_address() const {
        return (static_cast<uint32_t>(address_mapping_table()) << 24) | address();
    }
    bool is_relay() const { return field(kRelayField) != 0; }
    void set_relay(uint8_t relay = 1) { set_field(kRelayField, relay); }

    // INFO: fc 18sep26 G.1.5: 0x800000 in either field is "no 2D fix", not a place on the globe
    static constexpr uint32_t kPositionInvalidCode = 0x800000;
    bool has_position() const {
        return field(kLatitudeField) != kPositionInvalidCode &&
               field(kLongitudeField) != kPositionInvalidCode;
    }
    void set_position_invalid() {
        set_field(kLatitudeField, kPositionInvalidCode);
        set_field(kLongitudeField, kPositionInvalidCode);
    }
    int32_t latitude_steps() const { return read_signed_bits(payload, kLatitudeField); }
    int32_t longitude_steps() const { return read_signed_bits(payload, kLongitudeField); }
    int32_t latitude_1e7() const;
    int32_t longitude_1e7() const;
    void set_latitude_1e7(int32_t latitude);
    void set_longitude_1e7(int32_t longitude);

    // Three fields carry an explicit "invalid" code, and the spec is emphatic
    // that it means UNAVAILABLE, not out-of-range: an over-range value "shall be
    // encoded" as the limit (ADS-L 4 SRD860 issue 2, G.1.7 / G.1.8 / G.1.9). The
    // setters below honour that. The set_*_invalid() calls are how a shell says
    // it genuinely does not know.
    static constexpr uint16_t kSpeedInvalidCode = 0xFF;       // G.1.8, 8 bits
    static constexpr uint16_t kClimbInvalidCode = 0x1FF;      // G.1.9, 9 bits
    static constexpr uint16_t kAltitudeInvalidCode = 0x3FFF;  // G.1.7, 14 bits
    static constexpr int32_t kAltitudeOffsetM = 320;          // G.1.7 encoding offset

    uint16_t speed_q() const;          // [0.25 m/s]
    void set_speed_q(uint16_t speed);  // [0.25 m/s]
    bool has_speed() const { return field(kSpeedField) != kSpeedInvalidCode; }
    void set_speed_invalid() { set_field(kSpeedField, kSpeedInvalidCode); }

    int32_t altitude_m() const;  // [m] HAE (WGS-84 ellipsoid, G.1.7: NOT pressure)
    void set_altitude_m(int32_t altitude);
    bool has_altitude() const { return field(kAltitudeField) != kAltitudeInvalidCode; }
    void set_altitude_invalid() { set_field(kAltitudeField, kAltitudeInvalidCode); }

    int16_t climb_e8() const;  // [0.125 m/s]
    void set_climb_e8(int16_t climb);
    bool has_climb() const { return field(kClimbField) != kClimbInvalidCode; }
    void set_climb_invalid() { set_field(kClimbField, kClimbInvalidCode); }

    uint16_t track_c9() const { return static_cast<uint16_t>(field(kTrackField)); }
    void set_track_c9(uint16_t track) { set_field(kTrackField, track); }

    // The Integrity[2] block, G.1.10 to G.1.15. Every one of these fields has a
    // zero code that means "unknown / no fix", so leaving the block at its
    // initialised value is not a neutral act: it tells a receiver we claim
    // nothing. What we can honestly claim comes out of the receiver's DOP.
    static constexpr uint8_t kDesignAssuranceNone = 0;
    // INFO: fc 23sep26 G.1.14: a figure of merit is 2 * DOP * 6 m, and VFOM takes the same model
    static constexpr uint32_t kMeritPerDopCm = 1200;

    static uint8_t horizontal_accuracy_code(uint32_t hfom_cm);
    static uint8_t vertical_accuracy_code(uint32_t vfom_cm);
    static uint8_t velocity_accuracy_code(uint8_t horizontal_code);

    void set_integrity_unknown();
    void set_integrity_from_dop_e2(uint16_t hdop_e2, uint16_t vdop_e2);

    void scramble();
    void descramble();
    void set_crc();
    uint32_t check_crc() const;

    int correct(const uint8_t* erasures, int max_erasures = 6);
};

static_assert(sizeof(AdslPacket) == 28, "AdslPacket wire size must be 28 bytes");

bool to_observation(const AdslPacket& packet, const events::Stamp& received, int8_t rssi_dbm,
                    model::Source source, model::AircraftObservation& out);
// The timestamp field is quarter seconds inside a 15 s cycle, so the instant a
// burst claims is expressible to 250 ms - and the position it carries has to
// belong to that instant rather than to whenever the receiver last spoke.
static constexpr uint32_t kTimestampCycleS = 15;
static constexpr uint32_t kTimestampQuarterMs = 250;
uint8_t timestamp_code(uint32_t utc_s, int32_t lead_ms);

// INFO: fc 13sep26 timestamp names the UTC instant, since_fix_ms the interval carried over
struct BurstInstant {
    uint32_t utc_s;
    int32_t into_utc_ms;
    int32_t since_fix_ms;
};

// INFO: fc 18oct26 false, packet untouched, without an address: an unactivated unit builds no frame
bool from_own(AdslPacket& packet, const model::Ownship& own, settings::AirAddress address,
              uint8_t address_mapping_table, uint8_t aircraft_category);
bool from_own(AdslPacket& packet, const model::Ownship& own, settings::AirAddress address,
              uint8_t address_mapping_table, uint8_t aircraft_category, const BurstInstant& at);

// INFO: fc 23sep26 F.2.1 leaves payload 66 to OGN, so the set is the menu's: blank, dash, A-Z, 0-9
bool is_callsign_character(char c);
bool from_own_callsign(AdslPacket& packet, settings::AirAddress address,
                       uint8_t address_mapping_table, const char* callsign);
int callsign_of(const AdslPacket& packet, char* out, int capacity);

}

#endif
