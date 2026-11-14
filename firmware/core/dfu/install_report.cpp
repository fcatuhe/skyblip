#include "core/dfu/install_report.h"

#include "protocols/common/little_endian.h"

namespace skyblip::dfu {

namespace {

constexpr uint8_t kLayout = 1;
constexpr uint8_t kMostBoots = 255;

uint8_t counted(uint8_t count) { return count < kMostBoots ? count + 1 : count; }

}  // namespace

void InstallReport::booted(bool on_probation, power::ResetReason reason) {
    boots = counted(boots);
    reset = reason;
    if (!on_probation) return;
    probation_boots = counted(probation_boots);
    proof = Proof::Booted;
}

bool InstallReport::proved(Proof reached) {
    if (reached <= proof) return false;
    proof = reached;
    return true;
}

size_t to_blob(const InstallReport& report, uint8_t* out, size_t capacity) {
    if (capacity < kInstallReportBytes) return 0;
    out[0] = kLayout;
    out[1] = static_cast<uint8_t>(report.request.check);
    out[2] = static_cast<uint8_t>(report.request.swap);
    wire::put_uint16(out + 3, static_cast<uint16_t>(report.request.error));
    out[5] = report.boots;
    out[6] = report.probation_boots;
    out[7] = static_cast<uint8_t>(report.proof);
    out[8] = static_cast<uint8_t>(report.reset);
    return kInstallReportBytes;
}

bool from_blob(const uint8_t* blob, size_t bytes, InstallReport& out) {
    if (bytes != kInstallReportBytes || blob[0] != kLayout) return false;
    out = InstallReport{};
    out.request.check = static_cast<ports::StagedCheck>(blob[1]);
    out.request.swap = static_cast<ports::SwapType>(blob[2]);
    out.request.error = wire::get_int16(blob + 3);
    out.boots = blob[5];
    out.probation_boots = blob[6];
    out.proof = static_cast<Proof>(blob[7]);
    out.reset = static_cast<power::ResetReason>(blob[8]);
    return true;
}

}  // namespace skyblip::dfu
