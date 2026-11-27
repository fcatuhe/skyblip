// clang-format off
#ifndef SKYBLIP_CORE_DIAGNOSTICS_LAYOUT_H
#define SKYBLIP_CORE_DIAGNOSTICS_LAYOUT_H

#include <cstdint>

namespace skyblip::diagnostics {

enum class Type : uint8_t {
    None = 0,
    Boot = 1,
    Config = 2,
    Gnss = 3,
    Pps = 4,
    Burst = 5,
    Dwell = 6,
    Flight = 7,
    Power = 8,
    Barometer = 9,
    Motion = 10,
    Contact = 11,
    Link = 12,
    Traffic = 13,
    Write = 14,
    Screen = 15,
    Gap = 16,
    End = 17,
    Duty = 18,
    Switch = 19,
    Loop = 20,
    Adsb = 21,
    Receiver = 22,
    Reception = 23,
    Framing = 24,
};

constexpr uint8_t kHighestType = 24;

enum class Profile : uint8_t {
    Full = 0,
    PowerRun = 1,
    FlightRun = 2,
};

enum class Refusal : uint8_t {
    None = 0,
    OverBudget = 1,
    Unarmed = 2,
    GateClosed = 3,
    OffSchedule = 4,
    HeldForFix = 5,
};

enum class LinkAction : uint8_t {
    Up = 0,
    Down = 1,
    ClaimTaken = 2,
    ClaimReleased = 3,
    Received = 4,
    Sent = 5,
    Dropped = 6,
};

enum class SwitchKind : uint8_t {
    Retune = 0,
    Hop = 1,
    ToOBand = 2,
    ToMBand = 3,
    Wake = 4,
};

enum class LoopService : uint8_t {
    Config = 0,
    Ownship = 1,
    Power = 2,
    Radio = 3,
    Traffic = 4,
    Alarm = 5,
    Nmea = 6,
    FlightLog = 7,
    Capture = 8,
    Screen = 9,
    BoardPoll = 10,
    Bravo = 11,
    Adsb = 12,
    Button = 13,
    Peer = 14,
};

namespace boot_at {
enum : uint32_t {
    kCapabilities = 0,
    kFirmwareBuild = 4,
    kFirmwareRevision = 8,
    kFirmwareMajor = 10,
    kFirmwareMinor = 11,
    kReset = 12,
    kImageState = 13,
    kProfile = 14,
};
}

constexpr uint8_t kBootFlagProfileRecorded = 1u << 2;

namespace config_at {
enum : uint32_t {
    kAddress = 0,
    kBatteryOffsetMv = 4,
    kFrequencyTrimE1Ppm = 6,
    kAircraftType = 8,
    kAddressMappingTable = 9,
    kSettingsVersion = 11,
    kTxPowerDbm = 12,
    kPaRatedDbm = 13,
    kAlarmSetting = 14,
};
}

constexpr uint8_t kConfigFlagMetric = 1u << 3;
constexpr uint8_t kConfigFlagBatteryTrimManual = 1u << 4;
constexpr uint8_t kConfigFlagAlarmSettingRecorded = 1u << 5;

namespace gnss_at {
enum : uint32_t {
    kSolutionPhaseMs = 0,
    kResidualM = 2,
    kHdopE2 = 4,
    kVdopE2 = 6,
    kStageS = 8,
    kSatellites = 10,
    kSatellitesInView = 11,
    kFixMode = 12,
    kReject = 13,
    kStage = 14,
};
}

constexpr uint8_t kGnssFlagFixValid = 1u << 2;
constexpr uint8_t kGnssFlagResidualValid = 1u << 3;
constexpr uint8_t kGnssFlagPpsLocked = 1u << 4;
constexpr uint8_t kGnssFlagGeoidMeasured = 1u << 5;
constexpr uint8_t kGnssFlagTxSettled = 1u << 6;
constexpr uint8_t kGnssFlagLevelsFresh = 1u << 7;

namespace pps_at {
enum : uint32_t {
    kIntervalUs = 0,
    kErrorUs = 4,
    kSamples = 8,
    kHoldoverEvents = 12,
    kSinceEdgeMs = 14,
};
}

constexpr uint8_t kPpsFlagLocked = 1u << 2;
constexpr uint8_t kPpsFlagUtcValid = 1u << 3;

namespace burst_at {
enum : uint32_t {
    kAddress = 0,
    kTxKeyedUs = 4,
    kTxSpanUs = 6,
    kVerdict = 8,
    kSource = 9,
    kBand = 10,
    kChannel = 11,
    kBytes = 12,
    kRssiDbm = 13,
    kKeyOffsetS = 14,
    kTxStageMarginUs = 15,
};
}

constexpr uint8_t kBurstFlagAddressValid = 1u << 2;
constexpr uint8_t kBurstFlagRssiValid = 1u << 3;
constexpr uint8_t kBurstFlagAirborne = 1u << 4;
constexpr uint8_t kBurstFlagTxSpanValid = 1u << 5;
constexpr uint8_t kBurstFlagCallsign = 1u << 6;
constexpr uint8_t kBurstFlagStatus = 1u << 7;

namespace burst_scale {
enum : int32_t {
    kTxStageMarginUs = 10,
};
}

namespace dwell_at {
enum : uint32_t {
    kFrequencyHz = 0,
    kStartMs = 4,
    kEndMs = 6,
    kPhaseMs = 8,
    kDutyPermille = 10,
    kState = 12,
    kBand = 13,
    kRefusal = 14,
    kNoiseDbm = 15,
};
}

constexpr uint8_t kDwellFlagTxAllowed = 1u << 2;
constexpr uint8_t kDwellFlagOwnTxDwell = 1u << 3;
constexpr uint8_t kDwellFlagListenOnly = 1u << 4;
constexpr uint8_t kDwellFlagArmed = 1u << 5;
constexpr uint8_t kDwellFlagBurstArmed = 1u << 6;

namespace flight_at {
enum : uint32_t {
    kSpeedMmS = 0,
    kClimbMmS = 4,
    kAltitudeMslM = 8,
    kHdopE2 = 10,
    kVdopE2 = 12,
    kDeclared = 14,
    kConfirmed = 15,
};
}

constexpr uint8_t kFlightFlagFixValid = 1u << 2;
constexpr uint8_t kFlightFlagRolling = 1u << 3;
constexpr uint8_t kFlightFlagClimbValid = 1u << 4;
constexpr uint8_t kFlightFlagTxSettled = 1u << 5;

namespace power_at {
enum : uint32_t {
    kCellMv = 0,
    kSupplyWarnings = 2,
    kImplausible = 4,
    kChargeWarnings = 6,
    kDieDc = 8,
    kPercent = 10,
    kPowerLevel = 11,
    kCharge = 12,
    kSampleOffsetMv = 15,
};
}

constexpr uint8_t kPowerFlagCharging = 1u << 2;
constexpr uint8_t kPowerFlagExternalPower = 1u << 3;
constexpr uint8_t kPowerFlagBatteryValid = 1u << 4;
constexpr uint8_t kPowerFlagDieValid = 1u << 5;

namespace barometer_at {
enum : uint32_t {
    kPressureMpa = 0,
    kAltitudeMm = 4,
    kClimbMmS = 8,
    kTemperatureDc = 12,
};
}

constexpr uint8_t kBarometerFlagActive = 1u << 2;
constexpr uint8_t kBarometerFlagTemperatureValid = 1u << 3;
constexpr uint8_t kBarometerFlagClimbAdopted = 1u << 4;

namespace motion_at {
enum : uint32_t {
    kSlipMg = 0,
    kNormalMg = 2,
    kLateralMg = 4,
    kLongitudinalMg = 6,
    kMostNormalMg = 8,
    kLeastNormalMg = 10,
    kImuError = 12,
    kSensorError = 13,
};
}

constexpr uint8_t kMotionFlagSlipValid = 1u << 2;
constexpr uint8_t kMotionFlagGloadValid = 1u << 3;
constexpr uint8_t kMotionFlagFitted = 1u << 4;

namespace contact_at {
enum : uint32_t {
    kAtMs = 0,
    kHeldMs = 4,
    kContact = 8,
    kGesture = 9,
};
}

constexpr uint8_t kContactFlagDown = 1u << 2;

namespace link_at {
enum : uint32_t {
    kLinkSession = 0,
    kPayloadBytes = 2,
    kFrameBytes = 4,
    kHolder = 6,
    kDrops = 8,
    kAction = 10,
    kEndpoint = 11,
    kMtuExchangeStartedBy = 12,
};
}

constexpr uint8_t kLinkFlagClaimHeld = 1u << 2;

namespace traffic_at {
enum : uint32_t {
    kAddress = 0,
    kDistanceM = 4,
    kBearingDeg = 6,
    kRelativeAltitudeM = 8,
    kClosingMps = 10,
    kAlarm = 12,
    kSource = 13,
    kRssiDbm = 14,
    kTracked = 15,
};
}

constexpr uint8_t kTrafficFlagAssessed = 1u << 2;
constexpr uint8_t kTrafficFlagDismissed = 1u << 3;
constexpr uint8_t kTrafficFlagInFormation = 1u << 4;
constexpr uint8_t kTrafficFlagPositionValid = 1u << 5;
constexpr uint8_t kTrafficFlagAltitudeRecorded = 1u << 6;
constexpr uint8_t kTrafficFlagRelativeAltitudeValid = 1u << 7;

namespace write_at {
enum : uint32_t {
    kWaitedMs = 0,
    kPhaseMs = 4,
    kRequests = 6,
    kWrites = 8,
    kForced = 10,
    kPlacement = 12,
    kKind = 13,
};
}

constexpr uint8_t kWriteFlagPending = 1u << 2;

namespace screen_at {
enum : uint32_t {
    kSincePageShownMs = 0,
    kPage = 4,
    kMode = 5,
    kPrompt = 6,
    kAlarm = 7,
    kRenderUs = 8,
    kPresentUs = 12,
};
}

constexpr uint8_t kScreenFlagBacklight = 1u << 2;
constexpr uint8_t kScreenFlagPowered = 1u << 3;
constexpr uint8_t kScreenFlagHolding = 1u << 4;

namespace gap_at {
enum : uint32_t {
    kDropped = 0,
    kSpanMs = 4,
    kTotal = 8,
    kCapacity = 12,
};
}

namespace end_at {
enum : uint32_t {
    kRecords = 0,
    kDropped = 4,
};
}

namespace duty_at {
enum : uint32_t {
    kPanelPartialRefreshes = 0,
    kPanelFullRefreshes = 2,
    kBacklightMs = 4,
    kRxArmedMs = 6,
    kTxKeyedMs = 8,
    kBleConnectedMs = 10,
    kAnnunciatorMs = 12,
};
}

namespace switch_at {
enum : uint32_t {
    kToHz = 0,
    kMarginUs = 4,
    kTookUs = 8,
    kGapUs = 10,
    kChange = 12,
};
}

constexpr uint8_t kSwitchFlagArmedAhead = 1u << 2;
constexpr uint8_t kSwitchFlagLate = 1u << 3;

namespace loop_at {
enum : uint32_t {
    kPasses = 0,
    kBusyMs = 2,
    kWorstPassUs = 4,
    kWorstPassPhaseMs = 8,
    kWorstGapMs = 10,
    kWorstGapPhaseMs = 12,
    kWorstService = 14,
    kWorstTickMs = 15,
};
}

namespace adsb_at {
enum : uint32_t {
    kAddress = 0,
    kAdsbVersion = 3,
    kGva = 3,
    kPressureAltitudeFt = 4,
    kHeightDifferenceFt = 8,
    kHeightDifferenceAgeMs = 10,
    kGnssRelativeAltitudeM = 12,
    kPressureRelativeAltitudeM = 14,
};
}

constexpr uint8_t kAdsbFlagAdsbVersionValid = 1u << 2;
constexpr uint8_t kAdsbFlagGvaValid = 1u << 3;
constexpr uint8_t kAdsbFlagPressureAltitudeValid = 1u << 4;
constexpr uint8_t kAdsbFlagHeightDifferenceValid = 1u << 5;
constexpr uint8_t kAdsbFlagGnssRelativeAltitudeValid = 1u << 6;
constexpr uint8_t kAdsbFlagPressureRelativeAltitudeValid = 1u << 7;

namespace adsb_bits {
constexpr uint8_t kAdsbVersionShift = 0;
constexpr uint8_t kAdsbVersionMask = 0x07;
constexpr uint8_t kGvaShift = 3;
constexpr uint8_t kGvaMask = 0x03;
}

namespace receiver_at {
enum : uint32_t {
    kFrames = 0,
    kPartPackets = 2,
    kPartErrors = 4,
    kFramesDropped = 6,
    kRearms = 7,
    kResets = 8,
    kPartResets = 9,
    kCommandErrors = 10,
    kMissedEdges = 11,
    kNoiseDbm = 12,
    kThresholdDbm = 13,
    kPartMode = 14,
    kPartReset = 15,
};
}

constexpr uint8_t kReceiverFlagListening = 1u << 2;
constexpr uint8_t kReceiverFlagIrqArmed = 1u << 3;
constexpr uint8_t kReceiverFlagProbed = 1u << 4;
constexpr uint8_t kReceiverFlagInterruptPending = 1u << 5;
constexpr uint8_t kReceiverFlagFrontEndKept = 1u << 6;

namespace reception_at {
enum : uint32_t {
    kCrc = 0,
    kUnsupported = 2,
    kFolded = 4,
    kPositions = 6,
    kCaptureRefused = 8,
    kAircraft = 10,
    kTracked = 11,
    kReference = 12,
    kFramingResyncs = 13,
    kImpossibleFormats = 14,
};
}

constexpr uint8_t kReceptionFlagFixValid = 1u << 2;

namespace framing_at {
enum : uint32_t {
    kOverflowDrops = 0,
    kResyncDrops = 2,
    kQueueDrops = 4,
    kInFlight = 6,
    kSplitCounts = 8,
    kDrainBusyMs = 10,
    kDrains = 12,
    kStrandedResyncs = 14,
    kShortResyncs = 15,
};
}

}

#endif
