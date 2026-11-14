#ifndef SKYBLIP_CORE_COMMS_ADSB_TRIAL_H
#define SKYBLIP_CORE_COMMS_ADSB_TRIAL_H

#include <array>
#include <cstdint>

#include "core/util/cbor.h"
#include "core/util/result.h"

namespace skyblip::comms {

// TODO: fc 13nov26 adsb trial #293, remove this file once the trial picks the LR2021 settings
struct AdsbTrialProfile {
    int16_t threshold_dbm{-90};
    uint8_t gain_step{0};
    uint8_t boost{0};
    bool front_end_raw{true};
    bool manchester{false};
};

constexpr int kAdsbTrialProfiles = 8;
constexpr uint32_t kAdsbTrialSlotFloorS = 5;
constexpr uint32_t kAdsbTrialSlotCeilingS = 255;
// INFO: fc 13nov26 the LR2021's seven-bit threshold field, read as a signed value plus 74 dB
constexpr uint32_t kAdsbTrialThresholdFloorDbm = 11;
constexpr uint32_t kAdsbTrialThresholdCeilingDbm = 128;
// INFO: fc 13nov26 RadioLib setGain() and setRxPath(): gain steps 0 to 13, boosts 0 to 7
constexpr uint32_t kAdsbTrialGainStepCeiling = 13;
constexpr uint32_t kAdsbTrialBoostCeiling = 7;

// INFO: fc 13nov26 no profile is a stop, so one frame shape both runs and ends a trial
struct AdsbTrialPlan {
    std::array<AdsbTrialProfile, kAdsbTrialProfiles> profiles{};
    int count{0};
    uint32_t slot_s{0};
};

Status read_adsb_trial(const cbor::Reader& frame, AdsbTrialPlan& out);

}  // namespace skyblip::comms

#endif
