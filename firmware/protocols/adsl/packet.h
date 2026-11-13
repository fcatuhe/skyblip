#ifndef SKYBLIP_PROTOCOLS_ADSL_PACKET_H
#define SKYBLIP_PROTOCOLS_ADSL_PACKET_H

#include <cstdint>
#include <cstring>

#include "protocols/common/bit_field.h"
#include "protocols/common/exponential.h"

namespace skyblip::adsl {

// INFO: fc 13nov26 F.2.2's address mapping tables; 9 to 63 are manufacturer pages 0 to 54
constexpr uint8_t kAddressTableRandom = 0;
constexpr uint8_t kAddressTableIcao = 5;
constexpr uint8_t kAddressTableFlarm = 6;
constexpr uint8_t kAddressTableOgnTracker = 7;
constexpr uint8_t kAddressTableFanet = 8;
constexpr uint8_t kAddressTableManufacturerFirst = 9;
constexpr uint8_t kAddressTableManufacturerLast = 63;

constexpr uint8_t manufacturer_page_table(uint8_t page) {
    return static_cast<uint8_t>(kAddressTableManufacturerFirst + page);
}

// INFO: fc 13nov26 G.1.4 codes "no emergency" as 1, so 0 is "undefined", not "all well"
constexpr uint8_t kEmergencyNone = 1;

// INFO: fc 13nov26 G.1.14 prints HFOM = 2 * HDOP * UERE with a UERE of 6 m
constexpr uint32_t kUereCm = 600;

using wire::BitField;
using wire::Exponential;

struct __attribute__((packed)) Packet {
    static constexpr uint8_t kPayloadBytes = 20;
    static constexpr uint8_t kCrcCoverBytes = 1 + kPayloadBytes;
    static constexpr uint8_t kDataBytes = kCrcCoverBytes + 3;
    // INFO: fc 09nov26 on air the two sync bytes and the length byte go ahead of the data
    static constexpr uint8_t kTxBytes = 3 + kDataBytes;
    static constexpr uint8_t kPayloadTypePosition = 0x02;
    static constexpr uint8_t kPayloadTypeStatus = 0x03;
    // INFO: fc 13nov26 E.1.1 numbers Issue 2 as 1, and the Status payload first appears in Issue 2
    static constexpr uint8_t kProtocolVersionIssue1 = 0;
    static constexpr uint8_t kProtocolVersionIssue2 = 1;
    static constexpr uint8_t kMaxProtocolVersion = kProtocolVersionIssue2;
    static constexpr uint8_t kProtocolVersionMask = 0x0F;
    static constexpr uint8_t kPayloadTypeOgnDiagnostics = 0x42;

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

    // INFO: fc 09nov26 offsets count from payload bit 0: 40 bits of F.2 header, then G.1
    static constexpr uint16_t kTrafficOffset = 40;
    static constexpr BitField kAddressMappingTableField{8, 6};
    static constexpr BitField kAddressField{14, 24};
    static constexpr BitField kRelayField{39, 1};
    static constexpr BitField kLatitudeField{kTrafficOffset + 16, 24};
    static constexpr BitField kLongitudeField{kTrafficOffset + 40, 24};
    static constexpr BitField kSpeedField{kTrafficOffset + 64, 8};
    static constexpr BitField kAltitudeField{kTrafficOffset + 72, 14};
    static constexpr BitField kClimbField{kTrafficOffset + 86, 9};
    static constexpr BitField kTrackField{kTrafficOffset + 95, 9};

    static constexpr uint16_t kStatusOffset = kTrafficOffset;
    static constexpr BitField kMaxProtocolVersionField{kStatusOffset, 4};
    static constexpr BitField kMBandReceiveField{kStatusOffset + 4, 2};
    static constexpr BitField kOBandLdrReceiveField{kStatusOffset + 6, 2};
    static constexpr BitField kOBandHdrReceiveField{kStatusOffset + 8, 2};
    static constexpr BitField kMobileField{kStatusOffset + 10, 2};
    static constexpr BitField kConspicuityReceiveField{kStatusOffset + 12, 8};
    static constexpr BitField kConspicuityTransmitField{kStatusOffset + 20, 8};
    static constexpr BitField kTransponderField{kStatusOffset + 28, 2};
    static constexpr BitField kTrafficUplinkClientField{kStatusOffset + 30, 1};
    static constexpr BitField kFisBClientField{kStatusOffset + 31, 1};

    static constexpr Exponential kSpeedCode{6, 2, false};
    static constexpr Exponential kAltitudeCode{12, 2, false};
    static constexpr Exponential kClimbCode{6, 2, true};

    // INFO: fc 09nov26 G.1.5: a latitude step is 1/93206 degree, a longitude step 1/46603
    static constexpr int64_t kLatitudeStepsPerDegree = 93206;
    static constexpr int64_t kLongitudeStepsPerDegree = 46603;

    void reset(uint8_t type = kPayloadTypePosition) {
        __builtin_memset(this, 0, sizeof(*this));
        payload_type = type;
    }

    uint32_t field(BitField where) const { return wire::read_bits(payload, where); }
    void set_field(BitField where, uint32_t value) { wire::write_bits(payload, where, value); }

    bool is_position() const { return payload_type == kPayloadTypePosition; }
    uint8_t protocol_version() const { return version & kProtocolVersionMask; }
    void set_protocol_version(uint8_t value) {
        version = static_cast<uint8_t>((version & ~kProtocolVersionMask) |
                                       (value & kProtocolVersionMask));
    }

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
    // INFO: fc 13nov26 Issue 1 marked no fix 0xFFFFFF, one step west or south of zero in Issue 2
    static constexpr uint32_t kIssue1PositionInvalidCode = 0xFFFFFF;
    bool has_position() const {
        const uint32_t latitude = field(kLatitudeField);
        const uint32_t longitude = field(kLongitudeField);
        if (latitude == kPositionInvalidCode || longitude == kPositionInvalidCode) return false;
        return protocol_version() != kProtocolVersionIssue1 ||
               (latitude != kIssue1PositionInvalidCode && longitude != kIssue1PositionInvalidCode);
    }
    void set_position_invalid() {
        set_field(kLatitudeField, kPositionInvalidCode);
        set_field(kLongitudeField, kPositionInvalidCode);
    }
    int32_t latitude_steps() const { return wire::read_signed_bits(payload, kLatitudeField); }
    int32_t longitude_steps() const { return wire::read_signed_bits(payload, kLongitudeField); }
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

    static uint8_t horizontal_accuracy_code(uint32_t hfom_cm);
    static uint8_t vertical_accuracy_code(uint32_t vfom_cm);
    // INFO: fc 13nov26 Issue 1 G.1.15 drew GVA 3 at 15 m, where Issue 2 draws it at 10 m
    static uint8_t issue1_vertical_accuracy_code(uint32_t vfom_cm);

    void set_integrity_unknown();

    void scramble();
    void descramble();
    void set_crc();
    uint32_t check_crc() const;

    int correct(const uint8_t* erasures, int max_erasures = 6);
};

static_assert(sizeof(Packet) == Packet::kDataBytes, "Packet is the data the CRC covers");

// The timestamp field is quarter seconds inside a 15 s cycle, so the instant a
// burst claims is expressible to 250 ms - and the position it carries has to
// belong to that instant rather than to whenever the receiver last spoke.
static constexpr uint32_t kTimestampCycleS = 15;
static constexpr uint32_t kTimestampQuarterMs = 250;
uint8_t timestamp_code(uint32_t utc_s, int32_t lead_ms);

uint32_t hfom_cm(uint16_t hdop_e2);

// INFO: fc 13nov26 E.1.1: the lowest issue whose encoding of this clear payload means the same
uint8_t lowest_version(const Packet& packet);
bool issue2_category(uint8_t aircraft_category);

struct Integrity {
    uint8_t source_integrity{0};
    uint8_t design_assurance{0};
    uint8_t navigation_integrity{0};
    uint8_t horizontal_accuracy{0};
    uint8_t vertical_accuracy{0};
    uint8_t velocity_accuracy{0};
    // INFO: fc 13nov26 E.1.1: a VFOM of 10 to 15 m is GVA 2 in Issue 2 and 3 in Issue 1
    bool vertical_accuracy_issue2_only{false};
};

struct Traffic {
    uint32_t address{0};
    uint8_t timestamp{0};
    uint8_t flight_state{0};
    uint8_t aircraft_category{0};
    uint8_t emergency{0};
    bool position_valid{false};
    int32_t latitude_1e7{0};
    int32_t longitude_1e7{0};
    bool altitude_valid{false};
    int32_t altitude_m{0};
    bool speed_valid{false};
    uint16_t speed_q{0};
    bool climb_valid{false};
    int16_t climb_e8{0};
    uint16_t track_c9{0};
    Integrity integrity;
};

// INFO: fc 13nov26 the F.2.2 table is the integrator's registry assignment, so it has no default
void encode_traffic(Packet& packet, uint8_t address_mapping_table, const Traffic& traffic);
bool decode_traffic(const Packet& packet, Traffic& out);

enum class ReceiveClass : uint8_t { None = 0, Occasional = 1, Partial = 2, Full = 3 };
ReceiveClass receive_class(int heard_permille);

namespace conspicuity {
constexpr uint8_t kFlarm = 1u << 0;
constexpr uint8_t kAdsb1090 = 1u << 4;
}  // namespace conspicuity

// INFO: fc 13nov26 a G.2 field left out sends its zero code: no Mobile, no transponder, no FIS-B
struct StatusReport {
    ReceiveClass m_band{ReceiveClass::None};
    ReceiveClass o_band_ldr{ReceiveClass::None};
    ReceiveClass o_band_hdr{ReceiveClass::None};
    uint8_t conspicuity_received{0};
    uint8_t conspicuity_transmitted{0};
    bool traffic_uplink_client{false};
};

// INFO: fc 13nov26 padded to Traffic's 24 bytes, so a receiver whose sync fixes the length hears it
void encode_status(Packet& packet, uint32_t address, uint8_t address_mapping_table,
                   const StatusReport& report);

}

#endif
