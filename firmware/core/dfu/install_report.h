#ifndef SKYBLIP_CORE_DFU_INSTALL_REPORT_H
#define SKYBLIP_CORE_DFU_INSTALL_REPORT_H

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/dfu/update.h"
#include "core/power/reset_reason.h"
#include "ports/dfu.h"

namespace skyblip::dfu {

struct InstallReport {
    ports::SwapRequest request{};
    uint8_t boots{0};
    uint8_t probation_boots{0};
    Proof proof{Proof::None};
    power::ResetReason reset{power::ResetReason::Unknown};

    void booted(bool on_probation, power::ResetReason reason);
    bool proved(Proof reached);
};

struct Landing {
    ports::StagedCheck check{ports::StagedCheck::Unchecked};
    std::optional<ports::SigningKeyHash> staged_key{};
};

constexpr size_t kInstallReportBytes = 9;

size_t to_blob(const InstallReport& report, uint8_t* out, size_t capacity);
bool from_blob(const uint8_t* blob, size_t bytes, InstallReport& out);

}  // namespace skyblip::dfu

#endif
