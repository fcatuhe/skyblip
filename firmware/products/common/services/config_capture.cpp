#include "products/common/services/config_service.h"

namespace skyblip::common {

namespace {
constexpr uint32_t kLastProfileCode = static_cast<uint32_t>(comms::CaptureProfile::Stop);
}

void ConfigService::request_capture(const cbor::Reader& frame) {
    const Result<uint32_t> code = frame.read_unsigned(link::capture_key::kProfile);
    const bool stop = code.ok() && code.value() == kLastProfileCode;
    if (in_flight()) {
        refuse(link::Reason::InFlight);
    } else if (!code.ok()) {
        refuse(link::Reason::Invalid);
    } else if (code.value() > kLastProfileCode) {
        refuse(link::Reason::OutOfRange);
    } else if (!capture_available_) {
        refuse(link::Reason::NoStorage);
    } else if (capture_armed_ && !stop) {
        refuse(link::Reason::Capturing);
    } else if (!capture_armed_ && stop) {
        refuse(link::Reason::NotCapturing);
    } else {
        staged_capture_ = static_cast<comms::CaptureProfile>(code.value());
        stage(Pending::Capture, link::Reason::ConfirmCapture);
        (void)format_capture_detail(prompt_detail_, staged_capture_,
                                    diagnostics_.capture_evicts_flights);
    }
}

}  // namespace skyblip::common
