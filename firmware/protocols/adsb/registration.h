#ifndef SKYBLIP_PROTOCOLS_ADSB_REGISTRATION_H
#define SKYBLIP_PROTOCOLS_ADSB_REGISTRATION_H

#include <cstddef>
#include <cstdint>

namespace skyblip::adsb {

// INFO: fc 13nov26 the ADS-B charset has no dash, so FJABC from France's address block is F-JABC
int registration(char* out, size_t capacity, const char* callsign, uint32_t address);

}

#endif
