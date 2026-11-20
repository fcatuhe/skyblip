// INFO: fc 20nov26 a flash write stalls the core, so it goes only inside an armed receive dwell
#ifndef SKYBLIP_CORE_TIMING_DURABLE_WRITE_H
#define SKYBLIP_CORE_TIMING_DURABLE_WRITE_H

#include <cstdint>

#include "core/timing/slot.h"

namespace skyblip::timing {

// INFO: fc 20nov26 the radio's own stamped view of the second, never a second copy of the slot map
struct DwellPhase {
    uint32_t at_ms{0};
    int phase_ms{0};
    bool armed{false};
    bool burst_armed{false};
};

// INFO: fc 20nov26 Forced is the one fault: the bound is spent and the write goes anyway
enum class DurableWriteVerdict : uint8_t { Idle = 0, Hold = 1, Place = 2, Forced = 3 };

class DurableWriteWindow {
   public:
    // INFO: fw 04aug26 nRF52840 PS v1.8 NVMC: tWRITE a word, tERASEPAGE a page, both stall the core
    static constexpr uint32_t kWordWriteUs = 41;
    static constexpr uint32_t kPageEraseMs = 85;

    // INFO: fw 04aug26 an NVS garbage collection, a page erase and both blobs copied, unmeasured
    static constexpr uint32_t kBlobWords = (64 + 462 + 3) / 4;
    static constexpr uint32_t kWorstWriteMs =
        kPageEraseMs + (kBlobWords * kWordWriteUs + 999) / 1000;

    // INFO: fw 04aug26 the longest single stall, prj.conf's partial erase slice
    static constexpr uint32_t kPartialEraseMs = 4;

    // INFO: fc 20nov26 PromptAnswer::kAnswerSettleMs and a pass, so six quick taps make one write
    static constexpr uint32_t kSettleMs = 750;

    // INFO: fc 20nov26 from the first unwritten change: a dying cell loses only the one in progress
    static constexpr uint32_t kDeferCeilingMs = 3000;

    // INFO: fc 20nov26 ten passes: refuse a radio that stopped publishing rather than guess
    static constexpr uint32_t kViewStaleMs = 100;

    static_assert(kWorstWriteMs + static_cast<uint32_t>(kSwitchRetuneMs) <
                      static_cast<uint32_t>(dwell_of(kUplinkWindowMs).width_ms()),
                  "one settings write no longer fits inside the uplink dwell");
    static_assert(kPartialEraseMs < static_cast<uint32_t>(kSwitchRetuneMs),
                  "a forced write could move a dwell edge past the retune a switch keeps");
    static_assert(kDeferCeilingMs > kSettleMs + 1000,
                  "the settle and one whole second could spend the bound between them");

    // INFO: fc 20nov26 the first request starts the bound, and each one restarts the settle
    void request(uint32_t now_ms);

    DurableWriteVerdict decide(const SlotPlan& plan, const DwellPhase& dwell,
                               uint32_t now_ms) const;

    void placed(uint32_t now_ms, bool forced);
    // INFO: fc 25sep26 stays pending with its bound restarted, and counts in neither ratio term
    void refused(uint32_t now_ms, bool forced);

    bool pending() const { return pending_; }

    // INFO: fc 20sep26 the wait kDeferCeilingMs bounds, measured from the oldest unwritten change
    uint32_t waited_ms(uint32_t now_ms) const { return pending_ ? now_ms - first_request_ms_ : 0; }

    uint32_t requests() const { return requests_; }
    uint32_t writes() const { return writes_; }
    uint32_t forced() const { return forced_; }
    uint32_t worst_wait_ms() const { return worst_wait_ms_; }

    static bool free_at(const SlotPlan& plan, int phase_ms, uint32_t cost_ms);

    static bool free_now(const SlotPlan& plan, const DwellPhase& dwell, uint32_t now_ms,
                         uint32_t cost_ms);

   private:
    static bool placeable(const SlotPlan& plan, const DwellPhase& dwell, uint32_t now_ms);

    uint32_t requests_{0};
    uint32_t writes_{0};
    uint32_t forced_{0};
    uint32_t worst_wait_ms_{0};
    uint32_t first_request_ms_{0};
    uint32_t last_request_ms_{0};
    bool pending_{false};
};

}  // namespace skyblip::timing

#endif
