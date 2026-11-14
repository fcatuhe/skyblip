#ifndef SKYBLIP_CORE_PROTOCOL_CALLSIGN_H
#define SKYBLIP_CORE_PROTOCOL_CALLSIGN_H

#include <cstdint>

#include "core/settings/address.h"
#include "protocols/adsl/packet.h"
#include "protocols/common/bit_field.h"

namespace skyblip::protocol {

// INFO: fc 20sep26 F.2.1 leaves payload 66 to OGN, layout: nrf52-ogn-tracker/src/adsl.h:120
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
    return packet.payload_type == adsl::Packet::kPayloadTypeOgnDiagnostics &&
           telemetry_type(packet) == kTelemetryTypeInformation &&
           information_type(packet) == kInformationTypeRegistration;
}
inline const char* information_text(const adsl::Packet& packet) {
    return reinterpret_cast<const char*>(packet.payload + kInformationTextOffset);
}
inline char* information_text(adsl::Packet& packet) {
    return reinterpret_cast<char*>(packet.payload + kInformationTextOffset);
}

// INFO: fc 23sep26 F.2.1 leaves payload 66 to OGN, so the set is the menu's: blank, dash, A-Z, 0-9
bool is_callsign_character(char c);
bool from_own_callsign(adsl::Packet& packet, settings::AirAddress address,
                       uint8_t address_mapping_table, const char* callsign);
int callsign_of(const adsl::Packet& packet, char* out, int capacity);

}

#endif
