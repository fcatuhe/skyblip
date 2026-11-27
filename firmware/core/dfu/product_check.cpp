#include "core/dfu/product_check.h"

#include <algorithm>
#include <cstring>

namespace skyblip::dfu {

// INFO: fc 27nov26 img_mgmt keeps the upload before a first chunk it refuses, to resume it, so the
// check keeps it too
bool ProductCheck::begin(const uint8_t* chunk, size_t bytes, size_t upload_bytes,
                         const std::optional<ImageClass>& running) {
    ProductCheck upload;
    ImageHeader header;
    if (running && read_header(chunk, bytes, header) && header.protected_tlv_bytes > 0 &&
        header.protected_tlv_bytes <= kProtectedAreaBytes &&
        size_t{header.unprotected_tlv_offset()} + kTlvInfoBytes <= upload_bytes) {
        upload.expected_ = running;
        upload.header_ = header;
        upload.upload_bytes_ = upload_bytes;
        upload.area_offset_ = header.tlv_offset();
        upload.area_bytes_ = header.protected_tlv_bytes + kTlvInfoBytes;
    }
    if (!upload.admits(0, chunk, bytes)) return false;
    *this = upload;
    return true;
}

bool ProductCheck::admits(size_t offset, const uint8_t* chunk, size_t bytes) {
    if (!expected_ || refused_) return false;
    if (verified_) return true;
    const size_t area_end = area_offset_ + area_bytes_;
    const size_t from = std::max(offset, area_offset_);
    const size_t to = std::min(offset + bytes, area_end);
    if (from < to) std::memcpy(area_ + (from - area_offset_), chunk + (from - offset), to - from);
    if (offset + bytes < area_end) return true;
    verified_ = verify();
    refused_ = !verified_;
    return verified_;
}

// INFO: fc 16nov26 an upload ends at its image, so no swap carries forged bytes to the trust page
bool ProductCheck::verify() const {
    ImageClass incoming;
    const std::optional<uint32_t> end =
        image_end(header_, area_ + header_.protected_tlv_bytes, kTlvInfoBytes);
    return expected_ && find_image_class(area_, header_.protected_tlv_bytes, incoming) &&
           incoming == *expected_ && end == upload_bytes_;
}

}  // namespace skyblip::dfu
