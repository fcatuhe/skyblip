#ifndef SKYBLIP_PROTOCOLS_ADSL_EDGE_REGISTRATION_H
#define SKYBLIP_PROTOCOLS_ADSL_EDGE_REGISTRATION_H

#include <cstdint>

#include "protocols/adsl/packet.h"
#include "protocols/common/bit_field.h"

namespace skyblip::adsl_edge {

// INFO: fc 18nov26 F.2.1 lists 66 as OGN Diagnostics and leaves its format outside its scope
constexpr uint8_t kPayloadTypeOgnDiagnostics = 0x42;

// INFO: fc 20sep26 OGN's layout, nrf52-ogn-tracker/src/adsl.h:120: TelemType 1, InfoType 5, text
constexpr uint8_t kTelemetryTypeInformation = 1;
constexpr uint8_t kInformationTypeRegistration = 5;
constexpr int kInformationHeaderOffset = 5;
constexpr int kInformationTextOffset = 6;
constexpr int kInformationTextBytes = 14;
constexpr wire::BitField kInformationTypeField{adsl::Packet::kTrafficOffset, 6};
constexpr wire::BitField kTelemetryTypeField{adsl::Packet::kTrafficOffset + 6, 2};

inline uint8_t telemetry_type(const adsl::Packet& packet) {
    return static_cast<uint8_t>(packet.field(kTelemetryTypeField));
}
inline uint8_t information_type(const adsl::Packet& packet) {
    return static_cast<uint8_t>(packet.field(kInformationTypeField));
}
inline void set_information_header(adsl::Packet& packet, uint8_t telemetry_type,
                                   uint8_t information_type) {
    packet.set_field(kTelemetryTypeField, telemetry_type);
    packet.set_field(kInformationTypeField, information_type);
}
inline bool is_registration(const adsl::Packet& packet) {
    return packet.payload_type == kPayloadTypeOgnDiagnostics &&
           telemetry_type(packet) == kTelemetryTypeInformation &&
           information_type(packet) == kInformationTypeRegistration;
}
inline const char* information_text(const adsl::Packet& packet) {
    return reinterpret_cast<const char*>(packet.payload + kInformationTextOffset);
}
inline char* information_text(adsl::Packet& packet) {
    return reinterpret_cast<char*>(packet.payload + kInformationTextOffset);
}

// INFO: fc 18nov26 the table has no default, as in encode_traffic: it is the integrator's
// assignment
void encode_registration(adsl::Packet& packet, uint32_t address, uint8_t address_mapping_table,
                         const char* text, int length);

}

#endif
