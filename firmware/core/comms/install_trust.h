#ifndef SKYBLIP_CORE_COMMS_INSTALL_TRUST_H
#define SKYBLIP_CORE_COMMS_INSTALL_TRUST_H

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/comms/trusted_browsers.h"
#include "core/crypto/ed25519.h"
#include "core/util/span.h"
#include "ports/dfu.h"

namespace skyblip::comms {

constexpr char kInstallTrustMagic[] = "skyblip:trust:v1";
constexpr size_t kInstallTrustMagicBytes = sizeof(kInstallTrustMagic) - 1;
constexpr size_t kInstallTrustCrcBytes = 4;
constexpr size_t kInstallTrustRecordBytes = kInstallTrustMagicBytes +
                                            crypto::Ed25519::kPublicKeyBytes + 1 +
                                            kBrowserLabelBytes + kInstallTrustCrcBytes;
static_assert(kInstallTrustRecordBytes == 77, "README.md, Trusted browsers, gives the layout");
constexpr uint32_t kInstallTrustPageBytes = 4096;

struct InstallTrustRecord {
    crypto::Ed25519::PublicKey key{};
    char label[kBrowserLabelBytes + 1]{};
};

std::optional<InstallTrustRecord> read_install_trust_record(ConstByteSpan record);

// INFO: fc 16nov26 the first page past the image, which must end before slot0's trailer sector
std::optional<uint32_t> install_trust_page(uint32_t image_end, uint32_t slot_bytes);

// INFO: fc 16nov26 the page is erased only once the browsers blob holding its key has landed
class InstallTrust {
   public:
    bool adopt(ports::Dfu& slots, TrustedBrowsers& browsers);
    void landed() { landed_ = page_.has_value(); }
    bool erase_due() const { return page_.has_value() && landed_; }
    bool erase(ports::Dfu& slots);

   private:
    std::optional<uint32_t> page_{};
    bool landed_{false};
};

}  // namespace skyblip::comms

#endif
