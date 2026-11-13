// INFO: fc 15sep26 own-ship transmit policy, ADS-L 4 SRD-860 issue 2 §C.5 and §G.1.16, never LBT
#ifndef SKYBLIP_CORE_TIMING_TRANSMIT_H
#define SKYBLIP_CORE_TIMING_TRANSMIT_H

#include "core/flight/state.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "protocols/adsl/transmit.h"

namespace skyblip::timing {

// INFO: fc 18oct26 no address, no burst: only a serial the server signed may go on the air
inline bool ownship_transmits(const model::Ownship& own, const ClockState& clock,
                              const settings::AirAddress& address) {
    return address && own.fix_valid && own.utc_valid && own.tx_settled && clock.utc_valid &&
           clock.pps_locked;
}

class Transmitter {
   public:
    static constexpr uint32_t kAirTimeMs = adsl::kAirTimeMs;
    static constexpr uint32_t kStatusPeriodS = adsl::kStatusPeriodS;
    // INFO: fc 13nov26 each unit's cycle starts at its ground second, and holds every burst it owes
    static constexpr uint32_t kCycleS = 20;
    // INFO: fc 13nov26 of every placement, the most even per channel: gaps of 7, 6, 7 and 7, 7, 6 s
    static constexpr uint32_t kPositionOffsetsS[2] = {0, 9};
    static constexpr uint32_t kStatusOffsetsS[2] = {2, 13};
    static constexpr uint32_t kCallsignOffsetsS[2] = {7, 16};
    static constexpr int32_t kFixMaxAgeMs = adsl::kFixMaxAgeMs;
    // INFO: fc 29sep26 §C.5: bench worst 5.5 ms, 0.33 keying + 4.94-5.37 SetTx to TxDone, 1.5 left
    static constexpr int kCompletionGuardMs = 2;

    enum class Payload : uint8_t { Position, Status, Callsign };
    static constexpr int kPayloads = 3;

    struct Attempt {
        bool go{false};
        int at_ms{0};
        uint32_t frequency_hz{0};
        Payload payload{Payload::Position};
        // The one refusal that is not a rate rule: the hour's air time is spent.
        bool over_budget{false};
    };

    void configure(uint32_t address) { address_ = address; }

    // The instant this device transmits in the second `utc_s`, or go=false. Pure:
    // calling it twice with the same arguments gives the same answer.
    Attempt attempt(const SlotPlan& plan, uint32_t utc_s, uint32_t now_ms, bool airborne,
                    int32_t fix_lag_ms, Payload payload) const;
    // INFO: fc 13nov26 the first burst the dwell owes, in Payload order: a dwell can owe two
    Attempt attempt(const SlotPlan& plan, uint32_t utc_s, uint32_t now_ms, bool airborne,
                    int32_t fix_lag_ms) const;

    void sent(uint32_t utc_s, uint32_t now_ms, Payload payload = Payload::Position);

    uint32_t sent_count() const { return sent_; }
    const AirTime& air_time() const { return air_time_; }
    // INFO: fc 13nov26 the second alone picks the channel: A in an even second, B in an odd one
    static int slot_in(uint32_t utc_s) { return static_cast<int>(utc_s & 1u); }
    // INFO: fc 13nov26 Status and the name take the channel the second's position leaves free
    static int extra_slot_in(uint32_t utc_s) { return 1 - slot_in(utc_s); }
    // INFO: fc 13nov26 serial % 10, and skyblip.eu hands out serials densely from 1
    uint32_t ground_second() const;
    bool position_second(uint32_t utc_s, bool airborne) const;
    bool status_second(uint32_t utc_s) const;
    bool callsign_second(uint32_t utc_s) const;

   private:
    bool sent_in(uint32_t utc_s, Payload payload) const;
    uint32_t cycle_second(uint32_t utc_s) const;
    bool due(Payload payload, const SlotPlan& plan, uint32_t utc_s, bool airborne,
             int32_t fix_lag_ms) const;
    static int first_instant_in(int slot);
    static int last_instant_in(int slot);
    // Uniform over the slot's usable width and decorrelated between devices:
    // two aircraft with different addresses do not collide every second, and
    // one aircraft's instant is reproducible in a test.
    int instant_in(int slot, uint32_t utc_s) const;
    int extra_instant_in(int slot, uint32_t utc_s, Payload payload) const;

    AirTime air_time_{};
    uint32_t address_{0};
    uint32_t sent_{0};
    uint32_t last_utc_s_[kPayloads]{};
    bool ever_sent_[kPayloads]{};
};

static_assert(Transmitter::kCycleS == Transmitter::kStatusPeriodS &&
                  Transmitter::kCycleS == 2 * adsl::kGroundReportPeriodS,
              "one cycle holds G.2.11's Status period and two of G.1.16's ground periods");
static_assert((Transmitter::kPositionOffsetsS[1] - Transmitter::kPositionOffsetsS[0]) % 2 == 1 &&
                  (Transmitter::kStatusOffsetsS[1] - Transmitter::kStatusOffsetsS[0]) % 2 == 1 &&
                  (Transmitter::kCallsignOffsetsS[1] - Transmitter::kCallsignOffsetsS[0]) % 2 == 1,
              "an odd gap is what alternates each payload's channel under the parity rule");

}  // namespace skyblip::timing

#endif
