// core/protocol/adsl.h: ADS-L 4 SRD-860 Issue 2 iConspicuity (position) packet.
#ifndef SKYBLIP_CORE_PROTOCOL_ADSL_H
#define SKYBLIP_CORE_PROTOCOL_ADSL_H

#include <cstdint>
#include <cstring>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/util/little_endian.h"

namespace skyblip::protocol {

struct __attribute__((packed)) AdslPacket {
    static constexpr uint8_t kTxBytes = 27;
    static constexpr uint8_t kSync1 = 0x72;
    static constexpr uint8_t kSync2 = 0x4B;
    static constexpr uint8_t kDataBytes = kTxBytes - 3;      // 24: Version+payload+CRC
    static constexpr uint8_t kCrcCoverBytes = kTxBytes - 6;  // 21: Version+payload
    // INFO: fc 20sep26 F.2.1 leaves payload 66 to OGN, layout: nrf52-ogn-tracker/src/adsl.h:120
    static constexpr uint8_t kPayloadTypePosition = 0x02;
    static constexpr uint8_t kPayloadTypeOgnDiagnostics = 0x42;
    static constexpr uint8_t kTelemetryTypeInformation = 1;
    static constexpr uint8_t kInformationTypeRegistration = 5;
    static constexpr int kInformationHeaderByte = 5;
    static constexpr int kInformationTextByte = 6;
    static constexpr int kInformationTextBytes = 14;

    uint8_t sync[2];
    uint8_t data_bytes;
    union {
        uint8_t data[kDataBytes];  // Version..CRC, what the CRC and the FEC address
        struct __attribute__((packed)) {
            uint8_t version;  // Version[4]/Signature[1]/Key[2]/Reserved[1]
            union {
                uint32_t payload_words[5];
                uint8_t payload[20];
                struct __attribute__((packed)) {
                    uint8_t payload_type;      // 0x02 = iConspicuity; bit7 = unicast
                    uint8_t address_bytes[4];  // Address[30]/Reserved[1]/RelayForward[1]
                    // Meta[2]: Time[6]/FlightState[2]/Cat[5]/Emergency[3]
                    uint8_t timestamp : 6;          // [0.25 s]
                    uint8_t flight_state : 2;       // 0=unknown,1=ground,2=airborne
                    uint8_t aircraft_category : 5;  // ADS-L category
                    uint8_t emergency : 3;          // 1 = OK
                    // Position[11]: Lat[24]/Lon[24]/Speed[8]/Alt[14]/Climb[9]/Track[9]
                    uint8_t position_bytes[11];
                    // Integrity[2]: SIL[2]/SDA[2]/NIC[4]/NACp[3]/GVA[2]/NACv[2]/Reserved[1]
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

    void reset(uint8_t type = kPayloadTypePosition) {
        sync[0] = kSync1;
        sync[1] = kSync2;
        data_bytes = kTxBytes - 3;
        version = 0x00;
        for (int i = 0; i < 5; i++) payload_words[i] = 0;
        payload_type = type;
        spare = 0;
        crc_bytes[0] = crc_bytes[1] = crc_bytes[2] = 0;
    }

    bool is_position() const { return payload_type == kPayloadTypePosition; }

    uint8_t telemetry_type() const {
        return static_cast<uint8_t>(payload[kInformationHeaderByte] >> 6);
    }
    uint8_t information_type() const {
        return static_cast<uint8_t>(payload[kInformationHeaderByte] & 0x3F);
    }
    void set_information_header(uint8_t telemetry_type, uint8_t information_type) {
        payload[kInformationHeaderByte] =
            static_cast<uint8_t>((telemetry_type << 6) | (information_type & 0x3F));
    }
    bool is_registration() const {
        return payload_type == kPayloadTypeOgnDiagnostics &&
               telemetry_type() == kTelemetryTypeInformation &&
               information_type() == kInformationTypeRegistration;
    }
    const char* information_text() const {
        return reinterpret_cast<const char*>(payload + kInformationTextByte);
    }
    char* information_text() { return reinterpret_cast<char*>(payload + kInformationTextByte); }

    uint32_t address() const { return (get_u32(address_bytes) >> 6) & settings::kAddressMask; }
    void set_address(uint32_t address) {
        put_u32(address_bytes, (get_u32(address_bytes) & 0xC000003F) | (address << 6));
    }
    uint8_t address_mapping_table() const { return address_bytes[0] & 0x3F; }
    void set_address_mapping_table(uint8_t table) {
        address_bytes[0] = (address_bytes[0] & 0xC0) | table;
    }
    uint32_t table_and_address() const {
        return (static_cast<uint32_t>(address_mapping_table()) << 24) | address();
    }
    bool is_relay() const { return address_bytes[3] & 0x80; }
    void set_relay(uint8_t relay = 1) {
        address_bytes[3] = (address_bytes[3] & 0x7F) | (relay << 7);
    }

    static int32_t cordic_to_1e7(int32_t cordic) {
        return static_cast<int32_t>((static_cast<int64_t>(cordic) * 900007296 + (1 << 29)) >> 30);
    }
    static int32_t cordic_from_1e7(int32_t degrees_1e7) {
        return static_cast<int32_t>((static_cast<int64_t>(degrees_1e7) * 5003959 + (1 << 21)) >>
                                    22);
    }

    int32_t latitude_cordic() const {
        int32_t cordic = static_cast<int32_t>(get_u24(position_bytes));
        cordic <<= 8;
        cordic >>= 1;
        return cordic;
    }
    int32_t longitude_cordic() const {
        int32_t cordic = static_cast<int32_t>(get_u24(position_bytes + 3));
        cordic <<= 8;
        return cordic;
    }
    void set_latitude_cordic(int32_t latitude) {
        latitude = (latitude + 0x40) >> 7;
        put_u24(position_bytes, latitude);
    }
    void set_longitude_cordic(int32_t longitude) {
        longitude = (longitude + 0x80) >> 8;
        put_u24(position_bytes + 3, longitude);
    }
    // INFO: fc 18sep26 G.1.5: 0x800000 in either field is "no 2D fix", not a place on the globe
    static constexpr uint32_t kPositionInvalidCode = 0x800000;
    bool has_position() const {
        return get_u24(position_bytes) != kPositionInvalidCode &&
               get_u24(position_bytes + 3) != kPositionInvalidCode;
    }
    void set_position_invalid() {
        put_u24(position_bytes, kPositionInvalidCode);
        put_u24(position_bytes + 3, kPositionInvalidCode);
    }
    int32_t latitude_1e7() const { return cordic_to_1e7(latitude_cordic()); }
    int32_t longitude_1e7() const { return cordic_to_1e7(longitude_cordic()); }
    void set_latitude_1e7(int32_t latitude) { set_latitude_cordic(cordic_from_1e7(latitude)); }
    void set_longitude_1e7(int32_t longitude) { set_longitude_cordic(cordic_from_1e7(longitude)); }

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
    bool has_speed() const { return position_bytes[6] != kSpeedInvalidCode; }
    void set_speed_invalid() { position_bytes[6] = kSpeedInvalidCode; }

    int32_t altitude_m() const;  // [m] HAE (WGS-84 ellipsoid, G.1.7: NOT pressure)
    void set_altitude_m(int32_t altitude);
    bool has_altitude() const {
        return (position_bytes[8] & 0x3F) != 0x3F || position_bytes[7] != 0xFF;
    }
    void set_altitude_invalid() {
        position_bytes[7] = 0xFF;
        position_bytes[8] = static_cast<uint8_t>((position_bytes[8] & 0xC0) | 0x3F);
    }

    int16_t climb_e8() const;  // [0.125 m/s]
    void set_climb_e8(int16_t climb);
    bool has_climb() const;
    void set_climb_invalid() { write_climb_code(kClimbInvalidCode); }

    uint16_t track_c9() const;  // 9-bit cordic (512 == 360 deg)
    void set_track_c9(uint16_t track);

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

    void write_climb_code(uint16_t code);

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
bool is_callsign_char(char c);
bool from_own_callsign(AdslPacket& packet, settings::AirAddress address,
                       uint8_t address_mapping_table, const char* callsign);
int callsign_of(const AdslPacket& packet, char* out, int capacity);

}

#endif
