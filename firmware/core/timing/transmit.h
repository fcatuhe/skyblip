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
    // INFO: fc 20sep26 a name never changes in flight, this only bounds how long a target is hex
    static constexpr uint32_t kCallsignPeriodS = 10;
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
    static uint32_t period_s(bool airborne) { return adsl::period_s(airborne); }
    static int slot_in(uint32_t utc_s, bool airborne) { return adsl::channel_in(utc_s, airborne); }
    uint32_t ground_second() const;
    uint32_t status_second() const { return ground_second(); }
    // INFO: fc 13nov26 half a ground period after the address's own second, which the Status holds
    uint32_t callsign_second() const {
        return (ground_second() + kCallsignPeriodS / 2) % kCallsignPeriodS;
    }
    // INFO: fc 13nov26 §C.2.5 alternates Status as it does Traffic, on its own count
    static int status_slot_in(uint32_t utc_s) {
        return static_cast<int>((utc_s / kStatusPeriodS) & 1u);
    }
    static constexpr int kCallsignSlot = 1;
    static int last_callsign_instant();

   private:
    bool sent_in(uint32_t utc_s, Payload payload) const;
    bool due(Payload payload, const SlotPlan& plan, uint32_t utc_s, bool airborne,
             int32_t fix_lag_ms) const;
    static int first_instant_in(int slot);
    static int last_instant_in(int slot);
    // Uniform over the slot's usable width and decorrelated between devices:
    // two aircraft with different addresses do not collide every second, and
    // one aircraft's instant is reproducible in a test.
    int instant_in(int slot, uint32_t utc_s) const;
    int instant_between(int first, int last, uint32_t utc_s) const;
    int status_instant_in(int slot, uint32_t utc_s) const;

    AirTime air_time_{};
    uint32_t address_{0};
    uint32_t sent_{0};
    uint32_t last_utc_s_[kPayloads]{};
    bool ever_sent_[kPayloads]{};
};

static_assert(Transmitter::kCallsignPeriodS == adsl::kGroundReportPeriodS &&
                  Transmitter::kStatusPeriodS % adsl::kGroundReportPeriodS == 0,
              "the name's second is never the ground second the position and the Status use");

}  // namespace skyblip::timing

#endif
