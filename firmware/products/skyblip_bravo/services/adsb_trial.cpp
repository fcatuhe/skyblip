#include "products/skyblip_bravo/services/adsb_trial.h"

#include <algorithm>

#include "core/diagnostics/recorder.h"

namespace skyblip::bravo {

// TODO: fc 13nov26 adsb trial #293, remove this file once the trial picks the LR2021 settings
namespace {
constexpr uint32_t kCountedCeilingS = UINT8_MAX;

static_assert(comms::kAdsbTrialGainStepCeiling == parts::lr2021::kGainStepCeiling &&
                  comms::kAdsbTrialBoostCeiling == parts::lr2021::kRxBoostCeiling,
              "the link takes the gain steps and the boosts the LR2021 does");

uint16_t saturated_add(uint16_t count, uint32_t more) {
    return static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, count + more));
}
}  // namespace

void AdsbTrial::start(const comms::AdsbTrialPlan& plan, uint32_t now_ms,
                      const ports::AdsbReceiverStatistics& receiver) {
    if (open_) close(now_ms, receiver);
    plan_ = plan;
    open_ = false;
    counting_ = false;
    running_ = plan.count > 0;
    if (!running_) owed_ = parts::Lr2021Tuning{};
}

std::optional<parts::Lr2021Tuning> AdsbTrial::tick(uint32_t now_ms,
                                                   const ports::AdsbReceiverStatistics& receiver) {
    if (running_) {
        const uint32_t slot = context_.instant(now_ms).at_s / plan_.slot_s;
        if (!open_ || slot != slot_) {
            if (open_) close(now_ms, receiver);
            open(slot, now_ms);
        }
        if (!counting_ && static_cast<int32_t>(now_ms - settled_at_ms_) >= 0) {
            counting_ = true;
            dropped_before_ = receiver.dropped;
        }
    }
    std::optional<parts::Lr2021Tuning> owed = owed_;
    owed_.reset();
    return owed;
}

void AdsbTrial::heard(const events::AdsbFrame& frame, Status verdict, uint32_t now_ms) {
    if (!counting_) return;
    tally_.frames = saturated_add(tally_.frames, 1);
    if (verdict == Status::Unsupported) {
        tally_.unsupported = saturated_add(tally_.unsupported, 1);
    } else if (verdict != Status::Ok) {
        tally_.crc = saturated_add(tally_.crc, 1);
    }
    sample(frame, verdict, now_ms);
}

void AdsbTrial::folded(const model::AircraftObservation& observation) {
    if (!counting_) return;
    tally_.folded = saturated_add(tally_.folded, 1);
    if (observation.position_valid) tally_.positions = saturated_add(tally_.positions, 1);
    const uint32_t* first = aircraft_.data();
    const uint32_t* end = first + aircraft_held_;
    if (std::find(first, end, observation.address) != end) return;
    if (aircraft_held_ < kAircraftCounted)
        aircraft_[static_cast<size_t>(aircraft_held_++)] = observation.address;
}

// INFO: fc 13nov26 the order turns back on itself every cycle, so a drift in traffic hits both
// ends
int AdsbTrial::profile_of(uint32_t slot) const {
    const uint32_t count = static_cast<uint32_t>(plan_.count);
    const uint32_t place = slot % count;
    return static_cast<int>((slot / count) % 2 == 0 ? place : count - 1 - place);
}

void AdsbTrial::open(uint32_t slot, uint32_t now_ms) {
    open_ = true;
    counting_ = false;
    slot_ = slot;
    profile_ = profile_of(slot);
    settled_at_ms_ = now_ms + kRetuneSettleMs;
    const comms::AdsbTrialProfile& profile = plan_.profiles[static_cast<size_t>(profile_)];
    tally_ = diagnostics::AdsbSlot{};
    tally_.profile = static_cast<uint8_t>(profile_);
    // INFO: fc 13nov26 comms::read_adsb_trial keeps a threshold within -128 to -11 dBm
    tally_.threshold_dbm = static_cast<int8_t>(profile.threshold_dbm);
    tally_.gain_step = profile.gain_step;
    tally_.boost = profile.boost;
    tally_.front_end_raw = profile.front_end_raw;
    tally_.manchester = profile.manchester;
    aircraft_held_ = 0;
    owed_ = tuning_of(profile);
}

void AdsbTrial::close(uint32_t now_ms, const ports::AdsbReceiverStatistics& receiver) {
    open_ = false;
    if (!counting_) return;
    counting_ = false;
    const uint32_t settled_at_ms = settled_at_ms_;
    tally_.counted_s =
        static_cast<uint8_t>(std::min<uint32_t>(kCountedCeilingS, (now_ms - settled_at_ms) / 1000));
    tally_.aircraft = static_cast<uint8_t>(std::min<int>(aircraft_held_, UINT8_MAX));
    tally_.fifo_dropped = receiver.dropped != dropped_before_;
    (void)context_.diagnostics.record(tally_, context_.instant(now_ms));
}

void AdsbTrial::sample(const events::AdsbFrame& frame, Status verdict, uint32_t now_ms) {
    if (!context_.diagnostics.armed()) return;
    const uint32_t second = now_ms / 1000;
    if (second != sampled_second_) {
        sampled_second_ = second;
        sampled_ = 0;
    }
    if (sampled_ >= kChipsFramesPerSecond) return;
    sampled_++;
    seq_++;
    diagnostics::AdsbChips half{};
    half.seq = seq_;
    half.rssi_dbm = frame.rssi_dbm;
    half.rssi_valid = frame.rssi_valid;
    half.verdict = verdict == Status::Ok            ? diagnostics::AdsbChipsVerdict::Decoded
                   : verdict == Status::Unsupported ? diagnostics::AdsbChipsVerdict::Unsupported
                                                    : diagnostics::AdsbChipsVerdict::Crc;
    const diagnostics::Instant at = context_.instant(now_ms);
    for (const bool tail : {false, true}) {
        half.tail = tail;
        half.chips = {};
        const size_t first = tail ? diagnostics::kAdsbChipsPerRecord : 0;
        const size_t bytes =
            std::min<size_t>(diagnostics::kAdsbChipsPerRecord, frame.chips.size() - first);
        std::copy_n(frame.chips.begin() + static_cast<long>(first), bytes, half.chips.begin());
        (void)context_.diagnostics.record(half, at);
    }
}

parts::Lr2021Tuning AdsbTrial::tuning_of(const comms::AdsbTrialProfile& profile) {
    parts::Lr2021Tuning tuning{};
    tuning.detection_threshold_dbm = profile.threshold_dbm;
    tuning.gain_step = profile.gain_step;
    tuning.boost = profile.boost;
    tuning.front_end_raw = profile.front_end_raw;
    tuning.layout =
        profile.manchester ? parts::Lr2021Layout::Manchester : parts::Lr2021Layout::Chips;
    return tuning;
}

}  // namespace skyblip::bravo
