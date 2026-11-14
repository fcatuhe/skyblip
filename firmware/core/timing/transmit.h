// INFO: fc 14nov26 own-ship transmit policy, ADS-L 4 SRD-860 issue 2 §C.5, §D.3 and §G.1.16
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
    // INFO: fc 13nov26 each unit's cycle starts on an even second, c = 2 * (serial % 10)
    static constexpr uint32_t kCycleS = 20;
    static constexpr uint32_t kPositionOffsetsS[2] = {0, 9};
    static constexpr uint32_t kStatusOffsetsS[2] = {2, 11};
    // INFO: fc 13nov26 the name is skyBlip's, so it keeps to slot 1's tail, empty of ADS-L today
    static constexpr uint32_t kCallsignOffsetsS[2] = {4, 14};
    static constexpr int kCallsignSlot = 1;
    // INFO: fc 13nov26 a burst that missed its second tries the same channel two seconds on
    static constexpr uint32_t kRetryStepS = 2;
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
    // INFO: fc 13nov26 the first burst the dwell owes, in Payload order
    Attempt attempt(const SlotPlan& plan, uint32_t utc_s, uint32_t now_ms, bool airborne,
                    int32_t fix_lag_ms) const;

    void sent(uint32_t utc_s, uint32_t now_ms, Payload payload = Payload::Position);

    uint32_t sent_count() const { return sent_; }
    const AirTime& air_time() const { return air_time_; }
    // INFO: fc 13nov26 the second alone picks the channel: A in an even second, B in an odd one
    static int slot_in(uint32_t utc_s) { return static_cast<int>(utc_s & 1u); }
    // INFO: fc 13nov26 the Status takes the channel the second's position leaves free
    static int extra_slot_in(uint32_t utc_s) { return 1 - slot_in(utc_s); }
    static int channel_of(Payload payload, uint32_t utc_s);
    uint32_t cycle_start() const;
    bool turn(Payload payload, uint32_t utc_s, bool airborne) const;
    bool retry(Payload payload, uint32_t utc_s, bool airborne) const;
    // INFO: fc 13nov26 turns that ended unsent as utc_s opens, each counted once
    uint32_t give_up(uint32_t utc_s, bool airborne, bool named);
    static int last_callsign_instant();
    // INFO: fc 14nov26 the last start in a burst's window, which for the name is slot 1's tail
    static int last_instant(Payload payload, int slot);
    // INFO: fc 13nov26 the turn utc_s acts for: its own, or the one a retry in it is for
    uint32_t turn_of(Payload payload, uint32_t utc_s, bool airborne) const;
    // INFO: fc 14nov26 EN 300 220-1 5.21.3.2 3): a deferral's draw, hashed from address and instant
    uint32_t deferral_draw(uint64_t busy_at_us) const;

   private:
    bool sent_in(uint32_t utc_s, Payload payload) const;
    bool sent_since(Payload payload, uint32_t utc_s) const;
    void start(uint32_t utc_s);
    uint32_t cycle_second(uint32_t utc_s) const;
    bool scheduled_on(int slot, uint32_t utc_s, bool airborne) const;
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
    uint32_t closed_s_{0};
    // INFO: fc 13nov26 the first second this transmitter acted in: no turn before it is retried
    uint32_t first_s_{0};
    bool started_{false};
};

static_assert(Transmitter::kCycleS == Transmitter::kStatusPeriodS &&
                  Transmitter::kCycleS == 2 * adsl::kGroundReportPeriodS,
              "one cycle holds G.2.11's Status period and two of G.1.16's ground periods");
static_assert((Transmitter::kPositionOffsetsS[1] - Transmitter::kPositionOffsetsS[0]) % 2 == 1 &&
                  (Transmitter::kStatusOffsetsS[1] - Transmitter::kStatusOffsetsS[0]) % 2 == 1,
              "an odd gap is what alternates a position's and a Status's channel");
static_assert(Transmitter::kCallsignOffsetsS[0] % 2 == 0 &&
                  Transmitter::kCallsignOffsetsS[1] % 2 == 0,
              "the name sits in an even second, where the position is on A and B is ours alone");

}  // namespace skyblip::timing

#endif
