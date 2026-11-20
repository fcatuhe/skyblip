#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_FLASH_REGION_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_FLASH_REGION_H

#include <cstring>
#include <vector>

#include "ports/flash_region.h"

namespace skyblip::platform::host {

// INFO: fc 20nov26 erased flash reads 0xFF and a program only clears bits, as on NOR, so an
// overwrite fails here too
class FlashRegion : public ports::FlashRegion {
   public:
    // INFO: fc 20nov26 log_partition in boards/lilygo/t_echo_plus/t_echo_plus.dts, in 4 KB sectors
    static constexpr uint32_t kSectorBytes = 4096;
    static constexpr uint32_t kSectorCount = 298;
    // INFO: fc 03oct26 the driver programs a write one 256 B page at a time (spi_nor.c write)
    static constexpr uint32_t kPageBytes = 256;

    explicit FlashRegion(uint32_t sector_count = kSectorCount)
        : sector_count_(sector_count),
          bytes_(static_cast<size_t>(sector_count) * kSectorBytes, 0xFF) {}

    bool ready() const override { return fitted_; }
    void set_fitted(bool on) { fitted_ = on; }

    uint32_t sector_bytes() const override { return kSectorBytes; }
    uint32_t sector_count() const override { return sector_count_; }

    Status read(uint32_t offset, uint8_t* out, uint32_t bytes) override {
        if (!fitted_) return Status::Down;
        if (offset + bytes > bytes_.size()) return Status::OutOfRange;
        std::memcpy(out, bytes_.data() + offset, bytes);
        reads++;
        read_bytes += bytes;
        return Status::Ok;
    }

    Status write(uint32_t offset, const uint8_t* data, uint32_t bytes) override {
        if (!fitted_) return Status::Down;
        if (offset + bytes > bytes_.size()) return Status::OutOfRange;
        if (dead_) return Status::Down;
        uint32_t programmed = bytes;
        if (cut_after_bytes_ >= 0) {
            programmed = static_cast<uint32_t>(cut_after_bytes_) < bytes
                             ? static_cast<uint32_t>(cut_after_bytes_)
                             : bytes;
            cut_after_bytes_ = -1;
            dead_ = true;
        }
        for (uint32_t i = 0; i < programmed; i++) bytes_[offset + i] &= data[i];
        writes++;
        if (programmed > 0)
            programs += (offset + programmed - 1) / kPageBytes - offset / kPageBytes + 1;
        write_bytes += programmed;
        return dead_ ? Status::Down : Status::Ok;
    }

    Status erase_sector(uint32_t index) override {
        if (!fitted_) return Status::Down;
        if (index >= sector_count_) return Status::OutOfRange;
        if (dead_) return Status::Down;
        std::memset(bytes_.data() + static_cast<size_t>(index) * kSectorBytes, 0xFF, kSectorBytes);
        erases++;
        return Status::Ok;
    }

    void cut_power_after(int32_t bytes) { cut_after_bytes_ = bytes; }
    bool dead() const { return dead_; }

    const std::vector<uint8_t>& bytes() const { return bytes_; }
    void restore(const std::vector<uint8_t>& image) { bytes_ = image; }

    uint32_t reads{0};
    uint32_t writes{0};
    uint32_t programs{0};
    uint32_t erases{0};
    uint32_t read_bytes{0};
    uint32_t write_bytes{0};

   private:
    uint32_t sector_count_;
    std::vector<uint8_t> bytes_;
    int32_t cut_after_bytes_{-1};
    bool dead_{false};
    bool fitted_{true};
};

}  // namespace skyblip::platform::host

#endif
