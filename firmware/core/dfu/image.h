#ifndef SKYBLIP_CORE_DFU_IMAGE_H
#define SKYBLIP_CORE_DFU_IMAGE_H

#include <cstddef>
#include <cstdint>

#include "ports/dfu.h"

namespace skyblip::dfu {

constexpr size_t kImageHeaderBytes = 32;

struct ImageHeader {
    ports::ImageVersion version{};
    uint16_t header_bytes{0};
    uint32_t body_bytes{0};

    uint32_t tlv_offset() const { return header_bytes + body_bytes; }
};

bool read_header(const uint8_t* bytes, size_t len, ImageHeader& out);

bool find_key_hash(const uint8_t* tlvs, size_t len, ports::SigningKeyHash& out);

uint32_t key_prefix(const ports::SigningKeyHash& key);

}  // namespace skyblip::dfu

#endif
