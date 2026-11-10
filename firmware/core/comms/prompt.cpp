#include "core/comms/config.h"

namespace skyblip::comms {

const char* pending_title(Pending pending) {
    switch (pending) {
        case Pending::Set: return "SETTINGS";
        case Pending::Dfu: return "FIRMWARE";
        case Pending::Recovery: return "RECOVERY";
        case Pending::PowerOff: return "POWER OFF";
        case Pending::EraseLog: return "ERASE LOGS";
        case Pending::GnssColdStart: return "GNSS RESET";
        case Pending::Register: return "ACTIVATE";
        case Pending::None: break;
    }
    return "";
}

const char* pending_detail(Pending pending) {
    switch (pending) {
        case Pending::Set: return "APPLY THE SETTINGS THE PHONE SENT";
        case Pending::Dfu: return "INSTALL THE IMAGE THE PHONE SENDS";
        case Pending::Recovery: return "REBOOT INTO THE USB BOOTLOADER";
        case Pending::PowerOff: return "SHUT THE DEVICE DOWN";
        case Pending::EraseLog: return "DELETE EVERY FLIGHT ON THE DEVICE";
        case Pending::GnssColdStart: return "THROW AWAY THE STORED SATELLITE DATA";
        case Pending::Register: return "SEND THE KEY OF THIS UNIT TO SKYBLIP.EU";
        case Pending::None: break;
    }
    return "";
}

bool ConfigService::prompt_allowed_in(flight::FlightState state) const {
    // INFO: fc 18oct26 activation is the exception: a unit with no serial transmits nothing
    if (state == flight::FlightState::Ground || register_exempt()) return true;
    return pending_ == Pending::Set && settings_allowed(pending_set(), state);
}

// INFO: fc 10nov26 a unit with no fix grades no traffic, so a prompt there hides no advisory
bool ConfigService::settings_allowed(const cbor::Reader& frame, flight::FlightState state) const {
    if (state == flight::FlightState::Ground) return true;
    return state == flight::FlightState::Unknown && store_.pilot_settings_only(frame);
}

cbor::Reader ConfigService::pending_set() const {
    return cbor::Reader(ConstByteSpan(pending_set_frame_, static_cast<size_t>(pending_set_bytes_)));
}

}  // namespace skyblip::comms
