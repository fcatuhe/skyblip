#include "core/comms/config.h"
#include "core/dfu/update.h"
#include "core/util/format.h"

namespace skyblip::comms {

const char* pending_title(Pending pending) {
    switch (pending) {
        case Pending::Set: return "SETTINGS";
        case Pending::Dfu: return "FIRMWARE";
        case Pending::Recovery: return "RECOVERY";
        case Pending::PowerOff: return "POWER OFF";
        case Pending::EraseLog: return "ERASE LOGS";
        case Pending::GnssColdStart: return "ERASE GNSS DATA";
        case Pending::Register: return "ACTIVATE";
        case Pending::Capture: return "CAPTURE";
        case Pending::None: break;
    }
    return "";
}

const char* pending_detail(Pending pending) {
    switch (pending) {
        case Pending::Set: return "CHANGE TO THE SETTINGS THAT THE PHONE SENT";
        case Pending::Dfu: return "INSTALL THE FIRMWARE THAT THE PHONE SENDS";
        case Pending::Recovery: return "START THE USB BOOTLOADER";
        case Pending::PowerOff: return "STOP THE DEVICE";
        case Pending::EraseLog: return "ERASE ALL FLIGHT DATA ON THE DEVICE";
        case Pending::GnssColdStart: return "ERASE THE SATELLITE DATA IN THE RECEIVER";
        case Pending::Register: return "SEND THE KEY OF THIS UNIT TO SKYBLIP.EU";
        case Pending::Capture: return "START OR STOP THE DIAGNOSTIC CAPTURE";
        case Pending::None: break;
    }
    return "";
}

// INFO: fc 15nov26 three rows of sixteen at double height, so the profile and the price each fit
int format_capture_detail(char* out, CaptureProfile profile, uint32_t evicts_flights) {
    if (profile == CaptureProfile::Stop) return format_string(out, "STOP THE CAPTURE");
    int written = format_string(out, "START A ");
    written += format_string(out + written, capture_profile_word(profile));
    if (profile == CaptureProfile::Full) written += format_string(out + written, " CAPTURE");
    if (evicts_flights == 0) return written;
    written += format_string(out + written, ", ERASES ");
    written += format_unsigned(out + written, evicts_flights);
    return written + format_string(out + written, evicts_flights == 1 ? " FLIGHT" : " FLIGHTS");
}

int format_dfu_detail(char* out, size_t capacity, const ports::ImageVersion& version) {
    int written = format_string(out, "INSTALL ");
    written += dfu::format_version(version, out + written, capacity - static_cast<size_t>(written));
    written += format_string(out + written, " FROM THE PHONE");
    out[written] = 0;
    return written;
}

bool ConfigService::prompt_allowed_in(flight::FlightState state) const {
    // INFO: fc 18oct26 activation is the exception: a unit with no serial transmits nothing
    return state != flight::FlightState::Airborne || register_exempt();
}

cbor::Reader ConfigService::pending_set() const {
    return cbor::Reader(ConstByteSpan(pending_set_frame_, static_cast<size_t>(pending_set_bytes_)));
}

}  // namespace skyblip::comms
