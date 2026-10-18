#include "core/comms/config.h"

namespace skyblip::comms {

const char* pending_title(Pending pending) {
    switch (pending) {
        case Pending::Set: return "SETTINGS";
        case Pending::Dfu: return "FIRMWARE";
        case Pending::Recovery: return "RECOVERY";
        case Pending::PowerOff: return "POWER OFF";
        case Pending::EraseLog: return "ERASE LOGS";
        case Pending::GnssCold: return "GNSS RESET";
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
        case Pending::GnssCold: return "THROW AWAY THE STORED SATELLITE DATA";
        case Pending::Register: return "SEND THE KEY OF THIS UNIT TO SKYBLIP.EU";
        case Pending::None: break;
    }
    return "";
}

}  // namespace skyblip::comms
