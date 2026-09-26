#ifndef SKYBLIP_PORTS_DFU_H
#define SKYBLIP_PORTS_DFU_H

#include <cstdint>

namespace skyblip::ports {

enum class RecoveryPath { Rebooted, PowerOffToFinish };

struct ImageVersion {
    uint8_t major{0};
    uint8_t minor{0};
    uint16_t revision{0};
    uint32_t build{0};
};

constexpr bool operator==(const ImageVersion& a, const ImageVersion& b) {
    return a.major == b.major && a.minor == b.minor && a.revision == b.revision &&
           a.build == b.build;
}
constexpr bool operator!=(const ImageVersion& a, const ImageVersion& b) { return !(a == b); }

class Dfu {
   public:
    virtual ~Dfu() = default;

    // INFO: fc 07sep26 one-shot swap: an image that never calls confirm() is reverted next boot
    virtual void trigger() = 0;

    virtual bool confirm() { return true; }
    virtual bool confirmed() { return true; }

    virtual bool running_version(ImageVersion&) { return false; }
    virtual bool staged_version(ImageVersion&) { return false; }

    virtual RecoveryPath enter_recovery() { return RecoveryPath::Rebooted; }

    // INFO: fc 26sep26 the SMP hook runs on another thread and reads only what was published
    virtual void publish_upload_allowed(bool) {}
};

}  // namespace skyblip::ports

#endif
