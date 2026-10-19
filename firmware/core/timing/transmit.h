// INFO: fc 15sep26 own-ship transmit policy, ADS-L 4 SRD-860 issue 2 §C.5 and §G.1.16, never LBT
#ifndef SKYBLIP_CORE_TIMING_TRANSMIT_H
#define SKYBLIP_CORE_TIMING_TRANSMIT_H

#include "core/flight/state.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"

namespace skyblip::timing {

// INFO: fc 18oct26 no address, no burst: only a serial the server signed may go on the air
inline bool ownship_transmits(const model::Ownship& own, const ClockState& clock,
                              const settings::AirAddress& address) {
    return address && own.fix_valid && own.utc_valid && own.tx_settled && clock.utc_valid &&
           clock.pps_locked;
}

class Transmitter {
   public:
    // INFO: fc 28sep26 §C.2 at 100 kchip/s: 16 preamble + 64 sync + 24 bytes Manchester, 4.64 ms
    static constexpr uint32_t kAirTimeMs = 5;
    // INFO: fc 20sep26 a name never changes in flight, this only bounds how long a target is hex
    static constexpr uint32_t kCallsignPeriodS = 10;
    // INFO: fc 13sep26 G.1.16 nav age, to the top of the transmit second: the burst is extrapolated
    static constexpr int32_t kFixMaxAgeMs = 500;
    // INFO: fc 29sep26 §C.5: bench worst 5.5 ms, 0.33 keying + 4.94-5.37 SetTx to TxDone, 1.5 left
    static constexpr int kCompletionGuardMs = 2;

    enum class Payload : uint8_t { Position, Callsign };

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
    // INFO: fc 27sep26 the first burst the dwell owes, position first: a slot-1 dwell can owe both
    Attempt attempt(const SlotPlan& plan, uint32_t utc_s, uint32_t now_ms, bool airborne,
                    int32_t fix_lag_ms) const;

    void sent(uint32_t utc_s, uint32_t now_ms, Payload payload = Payload::Position);

    uint32_t sent_count() const { return sent_; }
    const AirTime& air_time() const { return air_time_; }
    static uint32_t period_s(bool airborne) {
        return flight::report_period_s(airborne ? flight::FlightState::Airborne
                                                : flight::FlightState::Ground);
    }
    // INFO: fc 16sep26 §C.2.5 alternates the channel per transmission, and the clock counts them
    static int slot_in(uint32_t utc_s, bool airborne) {
        return static_cast<int>((utc_s / period_s(airborne)) & 1u);
    }
    uint32_t ground_second() const;
    static constexpr int kCallsignSlot = 1;
    static int last_callsign_instant();

   private:
    bool on_schedule(uint32_t utc_s, bool airborne) const;
    bool on_callsign_schedule(uint32_t utc_s) const;
    bool position_sent_in(uint32_t utc_s) const;
    bool callsign_sent_in(uint32_t utc_s) const;
    static int first_instant_in(int slot);
    static int last_instant_in(int slot);
    // Uniform over the slot's usable width and decorrelated between devices:
    // two aircraft with different addresses do not collide every second, and
    // one aircraft's instant is reproducible in a test.
    int instant_in(int slot, uint32_t utc_s) const;
    int instant_between(int first, int last, uint32_t utc_s) const;

    AirTime air_time_{};
    uint32_t address_{0};
    uint32_t sent_{0};
    uint32_t last_position_utc_s_{0};
    uint32_t last_callsign_utc_s_{0};
    bool ever_sent_position_{false};
    bool ever_sent_callsign_{false};
};

}  // namespace skyblip::timing

#endif
