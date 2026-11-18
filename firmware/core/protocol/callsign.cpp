#include "core/protocol/callsign.h"

namespace skyblip::protocol {

bool is_callsign_character(char c) {
    return c == ' ' || c == '-' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

bool from_own_callsign(adsl::Packet& packet, settings::AirAddress address,
                       uint8_t address_mapping_table, const char* callsign) {
    if (!address) return false;
    int count = 0;
    while (count < adsl_edge::kInformationTextBytes && callsign[count] != 0 &&
           is_callsign_character(callsign[count]))
        count++;
    adsl_edge::encode_registration(packet, *address & settings::kAddressMask, address_mapping_table,
                                   callsign, count);
    return true;
}

int callsign_of(const adsl::Packet& packet, char* out, int capacity) {
    out[0] = 0;
    if (!adsl_edge::is_registration(packet) || capacity <= 1) return 0;
    const char* text = adsl_edge::information_text(packet);
    int count = 0;
    while (count < adsl_edge::kInformationTextBytes && text[count] != 0) {
        if (!is_callsign_character(text[count])) return 0;
        count++;
    }
    if (count >= capacity) count = capacity - 1;
    for (int i = 0; i < count; i++) out[i] = text[i];
    out[count] = 0;
    return count;
}

}
