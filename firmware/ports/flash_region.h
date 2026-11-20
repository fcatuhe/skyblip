#ifndef SKYBLIP_PORTS_FLASH_REGION_H
#define SKYBLIP_PORTS_FLASH_REGION_H

#include <cstdint>

#include "core/util/result.h"

namespace skyblip::ports {

class FlashRegion {
   public:
    virtual ~FlashRegion() = default;

    // INFO: fc 20nov26 the region came up, where Capability::Storage says storage is meant to be
    virtual bool ready() const = 0;

    virtual uint32_t sector_bytes() const = 0;
    virtual uint32_t sector_count() const = 0;

    virtual Status read(uint32_t offset, uint8_t* out, uint32_t bytes) = 0;
    // INFO: fc 20nov26 NOR: a program only clears bits, so write each erased byte once
    virtual Status write(uint32_t offset, const uint8_t* data, uint32_t bytes) = 0;
    virtual Status erase_sector(uint32_t index) = 0;
};

}  // namespace skyblip::ports

#endif
