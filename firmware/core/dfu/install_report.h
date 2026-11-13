#ifndef SKYBLIP_CORE_DFU_INSTALL_REPORT_H
#define SKYBLIP_CORE_DFU_INSTALL_REPORT_H

#include <cstddef>
#include <cstdint>

#include "core/dfu/update.h"
#include "core/power/reset_reason.h"
#include "ports/dfu.h"

// TODO: fc 13nov26 troubleshooting for #278, retired with #294 once Bluetooth updates land
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

constexpr size_t kInstallReportBytes = 9;

size_t to_blob(const InstallReport& report, uint8_t* out, size_t capacity);
bool from_blob(const uint8_t* blob, size_t bytes, InstallReport& out);

}  // namespace skyblip::dfu

#endif
