#ifndef SKYBLIP_CORE_DFU_IMAGE_H
#define SKYBLIP_CORE_DFU_IMAGE_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "ports/dfu.h"

namespace skyblip::dfu {

constexpr size_t kImageHeaderBytes = 32;

using ImageClass = std::array<uint8_t, 16>;

struct ImageHeader {
    ports::ImageVersion version{};
    uint16_t header_bytes{0};
    uint16_t protected_tlv_bytes{0};
    uint32_t body_bytes{0};

    uint32_t tlv_offset() const { return header_bytes + body_bytes; }
};

bool read_header(const uint8_t* data, size_t bytes, ImageHeader& out);

bool find_key_hash(const uint8_t* tlvs, size_t bytes, ports::SigningKeyHash& out);

bool find_image_class(const uint8_t* tlvs, size_t bytes, ImageClass& out);

}  // namespace skyblip::dfu

#endif
