#ifndef SKYBLIP_CORE_DFU_PRODUCT_CHECK_H
#define SKYBLIP_CORE_DFU_PRODUCT_CHECK_H

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/dfu/image.h"

namespace skyblip::dfu {

// INFO: fc 10nov26 every product shares one signing key, so an upload must name the running image's
// class
class ProductCheck {
   public:
    static constexpr size_t kProtectedAreaBytes = 128;

    bool begin(const uint8_t* chunk, size_t bytes, size_t upload_bytes,
               const std::optional<ImageClass>& running);
    bool admits(size_t offset, const uint8_t* chunk, size_t bytes);

   private:
    std::optional<ImageClass> expected_;
    bool refused_{false};
    bool verified_{false};
    size_t area_offset_{0};
    size_t area_bytes_{0};
    uint8_t area_[kProtectedAreaBytes]{};
};

}  // namespace skyblip::dfu

#endif
