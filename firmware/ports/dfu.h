#ifndef SKYBLIP_PORTS_DFU_H
#define SKYBLIP_PORTS_DFU_H

#include <array>
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

using SigningKeyHash = std::array<uint8_t, 32>;

class Dfu {
   public:
    virtual ~Dfu() = default;

    // INFO: fc 07sep26 one-shot swap: an image that never calls confirm() is reverted next boot
    virtual void trigger() = 0;

    virtual bool confirm() { return true; }
    virtual bool confirmed() { return true; }

    virtual bool running_version(ImageVersion&) { return false; }
    virtual bool staged_version(ImageVersion&) { return false; }
    virtual bool running_key(SigningKeyHash&) { return false; }
    virtual bool staged_key(SigningKeyHash&) { return false; }

    // INFO: fc 03oct26 the bootloader's rule, built into both images: a release refuses an older
    // one
    virtual bool downgrade_allowed() const { return false; }

    virtual RecoveryPath recovery_path() const { return RecoveryPath::Rebooted; }
    virtual RecoveryPath enter_recovery() { return recovery_path(); }

    // INFO: fc 26sep26 the SMP hook runs on another thread and reads only what was published
    virtual void publish_upload_allowed(bool) {}
    virtual bool upload_finished() { return false; }
    virtual void forget_upload() {}
};

}  // namespace skyblip::ports

#endif
