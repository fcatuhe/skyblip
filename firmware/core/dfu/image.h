#ifndef SKYBLIP_CORE_DFU_IMAGE_H
#define SKYBLIP_CORE_DFU_IMAGE_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/util/sha256.h"
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

bool find_image_digest(const uint8_t* tlvs, size_t bytes, Sha256::Digest& out);

class ImageReader {
   public:
    virtual ~ImageReader() = default;
    virtual uint32_t bytes() const = 0;
    virtual bool read(uint32_t offset, uint8_t* out, size_t bytes) = 0;
};

// INFO: fc 13nov26 MCUboot hashes the header, the body and the protected TLVs, and so does this
ports::StagedCheck check_image(ImageReader& image);

}  // namespace skyblip::dfu

#endif
