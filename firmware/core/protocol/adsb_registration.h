#ifndef SKYBLIP_CORE_PROTOCOL_ADSB_REGISTRATION_H
#define SKYBLIP_CORE_PROTOCOL_ADSB_REGISTRATION_H

#include <cstddef>
#include <cstdint>

namespace skyblip::protocol {

// INFO: fc 13nov26 the ADS-B charset has no dash, so FJABC from France's address block is F-JABC
int adsb_registration(char* out, size_t capacity, const char* callsign, uint32_t address);

}  // namespace skyblip::protocol

#endif
