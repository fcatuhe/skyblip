#include "core/power/reset_reason.h"

namespace skyblip::power {

ResetReason classify(ResetCause causes) {
    if (has_cause(causes, ResetCause::Watchdog)) return ResetReason::Watchdog;
    if (has_cause(causes, ResetCause::Lockup)) return ResetReason::Lockup;
    if (has_cause(causes, ResetCause::Brownout)) return ResetReason::Brownout;
    if (has_cause(causes, ResetCause::Debug)) return ResetReason::Debug;
    if (has_cause(causes, ResetCause::Pin)) return ResetReason::Pin;
    if (has_cause(causes, ResetCause::Software)) return ResetReason::Software;
    // INFO: fc 20nov26 below the reset pin, a deliberate reset, and above PowerOn, which a VBUS
    // wake also sets
    if (has_cause(causes, ResetCause::UsbVbus)) return ResetReason::ChargerWake;
    if (has_cause(causes, ResetCause::LowPowerWake)) return ResetReason::LowPowerWake;
    if (has_cause(causes, ResetCause::PowerOn)) return ResetReason::PowerOn;
    return ResetReason::Unknown;
}

const char* to_string(ResetReason reason) {
    switch (reason) {
        case ResetReason::PowerOn: return "POWER ON";
        case ResetReason::Pin: return "RESET PIN";
        case ResetReason::Brownout: return "BROWNOUT";
        case ResetReason::Software: return "SOFT RESET";
        case ResetReason::Watchdog: return "WATCHDOG";
        case ResetReason::Lockup: return "CPU LOCKUP";
        case ResetReason::ChargerWake: return "CHARGER WAKE";
        case ResetReason::LowPowerWake: return "BUTTON WAKE";
        case ResetReason::Debug: return "DEBUGGER";
        case ResetReason::Unknown: break;
    }
    return "UNKNOWN";
}

}  // namespace skyblip::power
