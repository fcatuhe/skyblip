#ifndef SKYBLIP_CORE_COMMS_LINK_REGISTRY_H
#define SKYBLIP_CORE_COMMS_LINK_REGISTRY_H

#include <cstddef>
#include <cstdint>

namespace skyblip::link {

constexpr uint32_t kCommandKey = 0;

enum class Command : uint8_t {
    Acknowledgement = 0,
    Get = 1,
    Config = 2,
    Defaults = 3,
    Set = 4,
    Status = 5,
    Timing = 6,
    Flash = 7,
    Radio = 8,
    Diagnostics = 9,
    Update = 10,
    Dfu = 11,
    Recovery = 12,
    PowerOff = 13,
    Challenge = 14,
    Register = 15,
    Registration = 16,
    Unlock = 17,
    List = 18,
    Read = 19,
    Erase = 20,
    Log = 21,
    Session = 22,
    Chunk = 23,
    GnssColdStart = 24,
    Ownship = 25,
};

enum class Reason : uint8_t {
    Confirm = 0,
    ConfirmDfu = 1,
    ConfirmRecovery = 2,
    ConfirmPowerOff = 3,
    ConfirmGnssColdStart = 4,
    ConfirmEraseLog = 5,
    ConfirmRegister = 6,
    Cancelled = 7,
    Expired = 8,
    InFlight = 9,
    LowPower = 10,
    Claimed = 11,
    NoCommand = 12,
    UnknownCommand = 13,
    NoVersion = 14,
    Older = 15,
    NothingStaged = 16,
    NotApproved = 17,
    WrongKey = 18,
    NoStatistics = 19,
    Install = 20,
    Dfu = 21,
    Recovery = 22,
    PowerOff = 23,
    EraseLog = 24,
    GnssColdStart = 25,
    Unlock = 26,
    Keyless = 27,
    NoSession = 28,
    BadSignature = 29,
    SmallMtu = 30,
    Flash = 31,
    Invalid = 32,
    OutOfRange = 33,
    UnknownLog = 34,
    NoDiagnostics = 35,
    NoStorage = 36,
    Busy = 37,
    FlightsOnly = 38,
    ReadFailed = 40,
    Erased = 41,
};

namespace acknowledgement_key {
enum : uint32_t {
    kAcknowledge = 1,
    kPending = 2,
    kReason = 3,
    kBy = 4,
};
}

namespace config_key {
enum : uint32_t {
    kVersion = 1,
    kAddress = 2,
    kAddressMappingTable = 3,
    kAircraftType = 4,
    kAlarmEnabled = 5,
    kAlarmVolume = 6,
    kUnits = 7,
    kCallsign = 8,
    kBatteryOffsetMv = 9,
    kFrequencyTrimE1Ppm = 10,
};
}

namespace status_key {
enum : uint32_t {
    kFlight = 1,
    kUpload = 2,
    kBatteryPercent = 3,
    kCharging = 4,
    kPowerLevel = 5,
    kWentDarkFlat = 6,
    kDieTemperatureC = 7,
};
}

namespace timing_key {
enum : uint32_t {
    kPart = 1,
    kMore = 2,
    kPpsUs = 3,
    kPpsWorstUs = 4,
    kPpsSamples = 5,
    kDwellUs = 6,
    kDwellWorstUs = 7,
    kDwellSamples = 8,
    kHoldoverEvents = 9,
    kMissed = 10,
    kRefused = 11,
    kSwitchHopUs = 12,
    kSwitchToOBandUs = 13,
    kSwitchToMBandUs = 14,
    kLateDwells = 15,
};
}

namespace flash_key {
enum : uint32_t {
    kWritable = 1,
    kRequests = 2,
    kWrites = 3,
    kForced = 4,
    kWorstWaitMs = 5,
    kPending = 6,
    kBudgetMs = 7,
    kBoundMs = 8,
};
}

namespace diagnostics_key {
enum : uint32_t {
    kGroup = 1,
    kPart = 2,
    kMore = 3,
    kUptimeS = 4,
    kReset = 5,
    kLinkDrops = 6,
    kNoiseDbm = 7,
    kDutyPermille = 8,
    kRxOk = 9,
    kRxBad = 10,
    kRxWait = 11,
    kRxType = 12,
    kRxUnframed = 13,
    kRxMiskeyed = 14,
    kRxNoise = 15,
    kRxNamed = 16,
    kTxOk = 17,
    kTxLost = 18,
    kTxNamed = 19,
    kTxKeyedUs = 20,
    kTxSpanUs = 21,
    kRangeRefused = 22,
    kDio1Armed = 23,
    kDio1Edges = 24,
    kDio1Missed = 25,
    kTracked = 26,
    kAlarm = 27,
    kFixes = 28,
    kValid = 29,
    kBaud = 30,
    kConfig = 31,
    kRaises = 32,
    kRaiseFallbacks = 33,
    kSolutionPhaseMs = 34,
    kOverruns = 35,
    kIdentified = 36,
    kFirmware = 37,
    kReject = 38,
    kRejected = 39,
    kResidualM = 40,
    kCellMv = 41,
    kPercent = 42,
    kBatteryValid = 43,
    kCharging = 44,
    kPowerLevel = 45,
    kSupplyWarnings = 46,
    kImplausible = 47,
    kDieTemperatureC = 48,
    kCharge = 49,
    kChargeWarnings = 50,
};
}

namespace update_key {
enum : uint32_t {
    kImage = 1,
    kFrom = 2,
    kTo = 3,
    kSettings = 4,
    kSwapPowered = 5,
    kImu = 6,
    kImuDigest = 7,
    kDowngrade = 8,
    kKey = 9,
};
}

namespace update_bytes {
constexpr size_t kImuDigest = 8;
constexpr size_t kKey = 4;
}

namespace dfu_key {
enum : uint32_t {
    kVersion = 1,
};
}

namespace challenge_key {
enum : uint32_t {
    kNonce = 1,
    kServerKeyId = 2,
};
}

namespace challenge_bytes {
constexpr size_t kNonce = 16;
constexpr size_t kServerKeyId = 4;
}

namespace register_key {
enum : uint32_t {
    kServerNonce = 1,
    kSignature = 2,
};
}

namespace register_bytes {
constexpr size_t kServerNonce = 16;
constexpr size_t kSignature = 64;
}

namespace registration_key {
enum : uint32_t {
    kDeviceId = 1,
    kPublicKey = 2,
    kVersion = 3,
    kServerKeyId = 4,
    kRelease = 5,
    kSignature = 6,
};
}

namespace registration_bytes {
constexpr size_t kDeviceId = 8;
constexpr size_t kPublicKey = 32;
constexpr size_t kServerKeyId = 4;
constexpr size_t kSignature = 64;
}

namespace unlock_key {
enum : uint32_t {
    kSerial = 1,
    kSignature = 2,
};
}

namespace unlock_bytes {
constexpr size_t kSignature = 64;
}

namespace list_key {
enum : uint32_t {
    kLog = 1,
    kIndex = 2,
};
}

namespace read_key {
enum : uint32_t {
    kLog = 1,
    kSession = 2,
    kFrom = 3,
    kCount = 4,
};
}

namespace erase_key {
enum : uint32_t {
    kLog = 1,
};
}

namespace log_key {
enum : uint32_t {
    kLog = 1,
    kAcknowledge = 2,
    kReason = 3,
    kSessions = 4,
    kTruncated = 5,
};
}

namespace session_key {
enum : uint32_t {
    kLog = 1,
    kIndex = 2,
    kOf = 3,
    kSession = 4,
    kRecords = 5,
    kClosed = 6,
    kTruncated = 7,
};
}

namespace chunk_key {
enum : uint32_t {
    kLog = 1,
    kSession = 2,
    kFrom = 3,
    kEndsSession = 4,
    kRecords = 5,
};
}

namespace ownship_key {
enum : uint32_t {
    kAircraftType = 1,
    kWidth = 2,
    kHotSpot = 3,
    kRows = 4,
};
}

namespace defaults_key = config_key;

namespace set_key = config_key;

namespace radio_key = diagnostics_key;

constexpr uint64_t host_keys(Command command) {
    switch (command) {
        case Command::Acknowledgement:
        case Command::Get:
        case Command::Config:
        case Command::Defaults:
        case Command::Status:
        case Command::Timing:
        case Command::Flash:
        case Command::Radio:
        case Command::Diagnostics:
        case Command::Update:
        case Command::Recovery:
        case Command::PowerOff:
        case Command::Challenge:
        case Command::Registration:
        case Command::Log:
        case Command::Session:
        case Command::Chunk:
        case Command::GnssColdStart: return 0x1;
        case Command::Set: return 0x7FF;
        case Command::Dfu:
        case Command::Erase: return 0x3;
        case Command::Register:
        case Command::Unlock:
        case Command::List: return 0x7;
        case Command::Read:
        case Command::Ownship: return 0x1F;
    }
    return 0;
}

}

#endif
