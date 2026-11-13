#include "core/protocol/callsign.h"

namespace skyblip::protocol {

bool is_callsign_character(char c) {
    return c == ' ' || c == '-' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

bool from_own_callsign(adsl::Packet& packet, settings::AirAddress address,
                       uint8_t address_mapping_table, const char* callsign) {
    if (!address) return false;
    packet.reset(adsl::Packet::kPayloadTypeOgnDiagnostics);
    packet.set_address(*address & settings::kAddressMask);
    packet.set_address_mapping_table(address_mapping_table);
    set_information_header(packet, kTelemetryTypeInformation, kInformationTypeRegistration);

    char* text = information_text(packet);
    int count = 0;
    while (count < kInformationTextBytes && callsign[count] != 0 &&
           is_callsign_character(callsign[count])) {
        text[count] = callsign[count];
        count++;
    }
    while (count < kInformationTextBytes) text[count++] = 0;
    packet.set_protocol_version(adsl::lowest_version(packet));
    return true;
}

int callsign_of(const adsl::Packet& packet, char* out, int capacity) {
    out[0] = 0;
    if (!is_registration(packet) || capacity <= 1) return 0;
    const char* text = information_text(packet);
    int count = 0;
    while (count < kInformationTextBytes && text[count] != 0) {
        if (!is_callsign_character(text[count])) return 0;
        count++;
    }
    if (count >= capacity) count = capacity - 1;
    for (int i = 0; i < count; i++) out[i] = text[i];
    out[count] = 0;
    return count;
}

}
