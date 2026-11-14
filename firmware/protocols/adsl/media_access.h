#ifndef SKYBLIP_PROTOCOLS_ADSL_MEDIA_ACCESS_H
#define SKYBLIP_PROTOCOLS_ADSL_MEDIA_ACCESS_H

#include <cstdint>
#include <optional>

namespace skyblip::adsl {

// INFO: fc 14nov26 §D.3: past this since the initial attempt "the device may force transmission"
constexpr uint64_t kForceAfterAttemptUs = 3000000;
// INFO: fc 14nov26 §D.3: after a forced transmission "the device may not transmit" for 2000 ms
constexpr uint64_t kSilenceAfterForceUs = 2000000;

enum class Access : uint8_t { Transmit, Defer, Force, Missed, Silent };

struct ChannelAttempt {
    bool clear{false};
    uint64_t at_us{0};
    uint64_t first_us{0};
    std::optional<uint64_t> forced_us{};
    uint64_t earliest_us{0};
    uint64_t last_us{0};
    bool may_force{false};
};

// INFO: fc 14nov26 EN 300 220-1 5.21.1's random interval, a whole number of units
struct Deferral {
    uint32_t unit_us{0};
    uint32_t units{1};
    uint32_t draw{0};
};

struct AccessVerdict {
    Access access{Access::Missed};
    uint64_t at_us{0};
};

AccessVerdict media_access(const ChannelAttempt& attempt, const Deferral& deferral);

}  // namespace skyblip::adsl

#endif
