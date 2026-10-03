#ifndef SKYBLIP_CORE_STORE_SECTOR_H
#define SKYBLIP_CORE_STORE_SECTOR_H

#include <cstdint>

#include "core/util/result.h"

namespace skyblip::store {

// INFO: fc 20sep26 "SB", so a hex dump of the partition says whose bytes these are
constexpr uint16_t kSectorMagic = 0x5342;
// INFO: fc 20sep26 v1 had no owner and its CRC covered 12 of 16 bytes, so it could not grow one
constexpr uint8_t kSectorVersion = 2;

constexpr uint32_t kSectorHeaderBytes = 16;

// INFO: fc 03oct26 MX25R tSE 40 ms typ (prj.conf), +1 ms poll, +commands; 240 max fits no dwell
constexpr uint32_t kSectorEraseCostMs = 42;
// INFO: fc 03oct26 one page program on the spi1 NOR, bench-settled, not a datasheet figure
constexpr uint32_t kSlotWriteCostMs = 2;
// INFO: fc 03oct26 one program operation, as the slot write, plus 256 bytes more of SPI at 8 MHz
constexpr uint32_t kPageWriteCostMs = kSlotWriteCostMs + 1;
// INFO: fc 03oct26 both candidate parts program 256 B pages: the driver splits a write at each
constexpr uint32_t kPageBytes = 256;

// INFO: fc 20sep26 byte 13, under the CRC: a suffix whose first sector was recycled cannot claim it
constexpr uint8_t kSectorFlagSessionStart = 1u << 0;

enum class SectorOwner : uint8_t { None = 0, Flights = 1, Diagnostics = 2 };

struct SectorHeader {
    uint32_t sequence{0};
    uint32_t session_id{0};
    SectorOwner owner{SectorOwner::None};
    uint8_t version{kSectorVersion};
    uint8_t record_bytes{0};
    bool session_start{false};
};

void encode_sector_header(const SectorHeader& header, uint8_t* out);

// INFO: fc 20sep26 Empty for erased, Crc for torn, Unsupported for a version or owner we refuse
Status decode_sector_header(const uint8_t* raw, SectorHeader& out);

bool erased(const uint8_t* raw, uint32_t len);

}  // namespace skyblip::store

#endif
