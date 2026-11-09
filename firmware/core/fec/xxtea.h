#ifndef SKYBLIP_CORE_FEC_XXTEA_H
#define SKYBLIP_CORE_FEC_XXTEA_H

#include <cstdint>

namespace skyblip::fec {

using XxteaKey = uint32_t[4];

constexpr XxteaKey kXxteaZeroKey = {0, 0, 0, 0};

// INFO: fc 09nov26 ADS-L E.2 fixes six rounds, not the 6 + 52 / n of the published cipher
void xxtea_encrypt(uint32_t* words, uint32_t count, const XxteaKey& key, uint32_t rounds);
void xxtea_decrypt(uint32_t* words, uint32_t count, const XxteaKey& key, uint32_t rounds);

}

#endif
