#include "protocols/adsl_edge/registration.h"

namespace skyblip::adsl_edge {

void encode_registration(adsl::Packet& packet, uint32_t address, uint8_t address_mapping_table,
                         const char* text, int length) {
    packet.reset(kPayloadTypeOgnDiagnostics);
    packet.set_address(address);
    packet.set_address_mapping_table(address_mapping_table);
    set_information_header(packet, kTelemetryTypeInformation, kInformationTypeRegistration);
    char* out = information_text(packet);
    for (int i = 0; i < kInformationTextBytes; i++) out[i] = i < length ? text[i] : 0;
    packet.set_protocol_version(adsl::lowest_version(packet));
}

}
