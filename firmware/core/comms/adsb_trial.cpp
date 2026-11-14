#include "core/comms/adsb_trial.h"

#include <initializer_list>
#include <utility>

#include "core/comms/config.h"
#include "core/comms/link_registry.h"

namespace skyblip::comms {

// TODO: fc 13nov26 adsb trial #293, remove this file once the trial picks the LR2021 settings
namespace {

struct Array {
    std::array<uint32_t, kAdsbTrialProfiles> values{};
    int count{0};
    bool present{false};
};

// INFO: fc 13nov26 a count past the profiles is out of range, and a wrong type is invalid
Status read_array(const cbor::Reader& frame, uint32_t key, Array& out) {
    cbor::Entry entry{};
    for (int i = 0; i < frame.count(); i++) {
        if (!frame.entry(i, entry) || entry.key != key) continue;
        if (entry.type != cbor::Type::Array) return Status::Invalid;
        if (entry.argument > static_cast<uint64_t>(kAdsbTrialProfiles)) return Status::OutOfRange;
        out.present = true;
        out.count = static_cast<int>(entry.argument);
        for (int at = 0; at < out.count; at++) {
            const Result<uint32_t> item = cbor::Reader::array_item(entry, at);
            if (!item.ok()) return Status::Invalid;
            out.values[static_cast<size_t>(at)] = item.value();
        }
        return Status::Ok;
    }
    return Status::Ok;
}

Status read_arrays(const cbor::Reader& frame, Array& thresholds, Array& gains, Array& boosts,
                   Array& front_ends, Array& layouts) {
    for (const auto& [key, array] :
         {std::pair<uint32_t, Array*>{link::adsb_trial_key::kThresholdsDbm, &thresholds},
          {link::adsb_trial_key::kGainSteps, &gains},
          {link::adsb_trial_key::kBoosts, &boosts},
          {link::adsb_trial_key::kFrontEnds, &front_ends},
          {link::adsb_trial_key::kLayouts, &layouts}}) {
        const Status status = read_array(frame, key, *array);
        if (status != Status::Ok) return status;
    }
    return Status::Ok;
}

Status check(const Array& array, int count, uint32_t floor, uint32_t ceiling) {
    if (!array.present) return Status::Ok;
    if (array.count != count) return Status::Invalid;
    for (int at = 0; at < count; at++) {
        const uint32_t value = array.values[static_cast<size_t>(at)];
        if (value < floor || value > ceiling) return Status::OutOfRange;
    }
    return Status::Ok;
}

uint32_t value_or(const Array& array, int at, uint32_t fallback) {
    return array.present ? array.values[static_cast<size_t>(at)] : fallback;
}

}  // namespace

Status read_adsb_trial(const cbor::Reader& frame, AdsbTrialPlan& out) {
    out = AdsbTrialPlan{};
    Array thresholds, gains, boosts, front_ends, layouts;
    const Status arrays = read_arrays(frame, thresholds, gains, boosts, front_ends, layouts);
    if (arrays != Status::Ok) return arrays;
    const Result<uint32_t> slot_s = frame.read_unsigned(link::adsb_trial_key::kSlotS);
    if (slot_s.status() != Status::Ok && slot_s.status() != Status::NotFound)
        return Status::Invalid;
    if (!thresholds.present) {
        const bool bare = slot_s.status() == Status::NotFound && !gains.present &&
                          !boosts.present && !front_ends.present && !layouts.present;
        return bare ? Status::Ok : Status::Invalid;
    }
    if (thresholds.count == 0 || !slot_s.ok()) return Status::Invalid;
    if (slot_s.value() < kAdsbTrialSlotFloorS || slot_s.value() > kAdsbTrialSlotCeilingS)
        return Status::OutOfRange;
    const int count = thresholds.count;
    for (const Status status :
         {check(thresholds, count, kAdsbTrialThresholdFloorDbm, kAdsbTrialThresholdCeilingDbm),
          check(gains, count, 0, kAdsbTrialGainStepCeiling),
          check(boosts, count, 0, kAdsbTrialBoostCeiling), check(front_ends, count, 0, 1),
          check(layouts, count, 0, 1)}) {
        if (status != Status::Ok) return status;
    }
    out.count = count;
    out.slot_s = slot_s.value();
    for (int at = 0; at < count; at++) {
        AdsbTrialProfile& profile = out.profiles[static_cast<size_t>(at)];
        profile.threshold_dbm =
            static_cast<int16_t>(-static_cast<int32_t>(thresholds.values[static_cast<size_t>(at)]));
        profile.gain_step = static_cast<uint8_t>(value_or(gains, at, 0));
        profile.boost = static_cast<uint8_t>(value_or(boosts, at, 0));
        profile.front_end_raw = value_or(front_ends, at, 1) == 1;
        profile.manchester = value_or(layouts, at, 0) == 1;
    }
    return Status::Ok;
}

void ConfigService::request_adsb_trial(const cbor::Reader& frame) {
    AdsbTrialPlan plan{};
    const Status read = read_adsb_trial(frame, plan);
    if (read == Status::OutOfRange) {
        refuse(link::Reason::OutOfRange);
    } else if (read != Status::Ok) {
        refuse(link::Reason::Invalid);
    } else {
        adsb_trial_requested_ = plan;
        acknowledge(true, link::Reason::AdsbTrial);
    }
}

}  // namespace skyblip::comms
