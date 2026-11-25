#ifndef SKYBLIP_CORE_POWER_CABLE_H
#define SKYBLIP_CORE_POWER_CABLE_H

#include <cstdint>
#include <optional>

namespace skyblip::power {

enum class Switch : uint8_t { Button = 0, Cable = 1 };

// INFO: fc 26nov26 outlasts a Lycoming starter's 5-minute rest between cranks, master off
constexpr uint32_t kCableGoneHoldMs = 300000;

// INFO: fc 26nov26 a cable lost in flight costs the cell, never the transmitter: README.md
class CableWatch {
   public:
    bool gone(uint32_t now_ms, bool external_power, bool airborne);

   private:
    std::optional<uint32_t> gone_since_ms_{};
    bool seen_{false};
};

}  // namespace skyblip::power

#endif
