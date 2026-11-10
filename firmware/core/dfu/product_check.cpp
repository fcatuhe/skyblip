#include "core/dfu/product_check.h"

#include <algorithm>
#include <cstring>

namespace skyblip::dfu {

bool ProductCheck::begin(const uint8_t* chunk, size_t bytes, size_t upload_bytes,
                         const std::optional<ImageClass>& running) {
    *this = ProductCheck{};
    ImageHeader header;
    if (running && read_header(chunk, bytes, header) && header.protected_tlv_bytes > 0 &&
        header.protected_tlv_bytes <= kProtectedAreaBytes &&
        size_t{header.tlv_offset()} + header.protected_tlv_bytes <= upload_bytes) {
        expected_ = running;
        area_offset_ = header.tlv_offset();
        area_bytes_ = header.protected_tlv_bytes;
    }
    return admits(0, chunk, bytes);
}

bool ProductCheck::admits(size_t offset, const uint8_t* chunk, size_t bytes) {
    if (!expected_ || refused_) return false;
    if (verified_) return true;
    const size_t area_end = area_offset_ + area_bytes_;
    const size_t from = std::max(offset, area_offset_);
    const size_t to = std::min(offset + bytes, area_end);
    if (from < to) std::memcpy(area_ + (from - area_offset_), chunk + (from - offset), to - from);
    if (offset + bytes < area_end) return true;
    ImageClass incoming;
    verified_ = find_image_class(area_, area_bytes_, incoming) && incoming == *expected_;
    refused_ = !verified_;
    return verified_;
}

}  // namespace skyblip::dfu
