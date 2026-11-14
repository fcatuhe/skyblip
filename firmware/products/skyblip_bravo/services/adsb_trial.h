#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_ADSB_TRIAL_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_ADSB_TRIAL_H

#include <array>
#include <cstdint>
#include <optional>

#include "core/comms/adsb_trial.h"
#include "core/diagnostics/adsb_trial.h"
#include "core/events/adsb.h"
#include "core/model/aircraft.h"
#include "core/util/result.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "ports/adsb_receiver.h"
#include "protocols/adsb/assembly.h"
#include "runtime/service.h"

namespace skyblip::bravo {

// TODO: fc 13nov26 adsb trial #293, remove this file once the trial picks the LR2021 settings
// INFO: fc 13nov26 switches the receiver's profile on the clock's grid and counts each slot apart
class AdsbTrial {
   public:
    // INFO: fc 13nov26 frames queued under the last profile drain within a pass, so a second is
    // spare
    static constexpr uint32_t kRetuneSettleMs = 1000;
    // INFO: fc 13nov26 two records a frame, so four frames keep the chips to a quarter of the ring
    static constexpr uint32_t kChipsFramesPerSecond = 4;
    static constexpr int kAircraftCounted = adsb::Assembly::kMaxAircraft;

    explicit AdsbTrial(runtime::Context& context) : context_(context) {}

    void start(const comms::AdsbTrialPlan& plan, uint32_t now_ms,
               const ports::AdsbReceiverStatistics& receiver);
    std::optional<parts::Lr2021Tuning> tick(uint32_t now_ms,
                                            const ports::AdsbReceiverStatistics& receiver);
    void heard(const events::AdsbFrame& frame, Status verdict, uint32_t now_ms);
    void folded(const model::AircraftObservation& observation);

    bool active() const { return running_ || owed_.has_value(); }
    bool running() const { return running_; }
    bool counting() const { return counting_; }
    int profile() const { return profile_; }

   private:
    int profile_of(uint32_t slot) const;
    void open(uint32_t slot, uint32_t now_ms);
    void close(uint32_t now_ms, const ports::AdsbReceiverStatistics& receiver);
    void sample(const events::AdsbFrame& frame, Status verdict, uint32_t now_ms);
    static parts::Lr2021Tuning tuning_of(const comms::AdsbTrialProfile& profile);

    runtime::Context& context_;
    comms::AdsbTrialPlan plan_{};
    std::optional<parts::Lr2021Tuning> owed_{};
    bool running_{false};
    bool open_{false};
    bool counting_{false};
    uint32_t slot_{0};
    int profile_{0};
    uint32_t settled_at_ms_{0};
    uint32_t dropped_before_{0};
    diagnostics::AdsbSlot tally_{};
    std::array<uint32_t, kAircraftCounted> aircraft_{};
    int aircraft_held_{0};
    uint32_t sampled_second_{0};
    uint32_t sampled_{0};
    uint8_t seq_{0};
};

}  // namespace skyblip::bravo

#endif
