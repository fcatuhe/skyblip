#ifndef SKYBLIP_CORE_PROTOCOL_CALLSIGN_H
#define SKYBLIP_CORE_PROTOCOL_CALLSIGN_H

#include "core/settings/address.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl_edge/registration.h"

namespace skyblip::protocol {

// INFO: fc 23sep26 F.2.1 leaves payload 66 to OGN, so the set is the menu's: blank, dash, A-Z, 0-9
bool is_callsign_character(char c);
bool from_own_callsign(adsl::Packet& packet, settings::AirAddress address,
                       uint8_t address_mapping_table, const char* callsign);
int callsign_of(const adsl::Packet& packet, char* out, int capacity);

}

#endif
