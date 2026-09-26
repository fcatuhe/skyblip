// INFO: fc 15sep26 own-ship transmit policy, ADS-L 4 SRD-860 issue 2 §C.5 and §G.1.16, never LBT
#ifndef SKYBLIP_CORE_TIMING_TRANSMIT_H
#define SKYBLIP_CORE_TIMING_TRANSMIT_H

#include "core/model/ownship.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"

namespace skyblip::timing {

inline bool own_ship_transmits(const model::OwnState& own, const ClockState& clock) {
    return own.fix_valid && own.utc_valid && own.tx_settled && clock.utc_valid && clock.pps_locked;
}

class Transmitter {
   public:
    // §C.2 at 100 kchip/s: 16-chip preamble, 64-chip Manchester sync word, then
    // 25 Manchester-encoded bytes = 4.8 ms, rounded up.
    static constexpr uint32_t kAirTimeMs = 5;
    // §G.1.16: at least 1 Hz airborne, 0.1 Hz on the ground.
    static constexpr uint32_t kGroundPeriodS = 10;
    static constexpr uint32_t kAirbornePeriodS = 1;
    // INFO: fc 20sep26 a name never changes in flight, this only bounds how long a contact is hex
    static constexpr uint32_t kCallsignPeriodS = 10;
    // INFO: fc 13sep26 G.1.16 nav age, to the top of the transmit second: the burst is extrapolated
    static constexpr int32_t kFixLagMaxMs = 500;
    // Ours, not the spec's: §C.5 gives the direct slot 450..1000 and requires a
    // burst to complete before the slot ends. Margin between the burst's last
    // chip and the end of the window, absorbing PPS error, the carrier sample
    // and the SPI write. Nothing is owed at the front: a dwell that has opened
    // is tuned, the retune was paid for by the guard before it.
    static constexpr int kCompletionSlackMs = 5;

    enum class Payload : uint8_t { Position, Callsign };

    struct Attempt {
        bool go{false};
        int at_ms{0};
        uint32_t freq_hz{0};
        Payload payload{Payload::Position};
        // The one refusal that is not a rate rule: the hour's air time is spent.
        bool over_budget{false};
    };

    void configure(uint32_t device_addr) { addr_ = device_addr; }

    // The instant this device transmits in the second `utc`, or go=false. Pure:
    // calling it twice with the same arguments gives the same answer.
    Attempt attempt(const SlotPlan& plan, uint32_t utc, uint32_t now_ms, bool airborne,
                    int32_t fix_lag_ms) const;

    void sent(uint32_t utc, uint32_t now_ms, Payload payload = Payload::Position);

    uint32_t sent_count() const { return sent_; }
    const AirTime& air_time() const { return air_; }
    static uint32_t period_s(bool airborne) { return airborne ? kAirbornePeriodS : kGroundPeriodS; }
    // INFO: fc 16sep26 §C.2.5 alternates the channel per transmission, and the clock counts them
    static int slot_in(uint32_t utc, bool airborne) {
        return static_cast<int>((utc / period_s(airborne)) & 1u);
    }
    uint32_t ground_second() const;
    uint32_t callsign_second() const;
    static constexpr int kCallsignSlot = 1;
    static int last_callsign_instant();

   private:
    bool on_schedule(uint32_t utc, bool airborne) const;
    bool on_callsign_schedule(uint32_t utc) const;
    bool spoke_in(uint32_t utc) const;
    bool named_in(uint32_t utc) const;
    static int first_instant_in(int slot);
    static int last_instant_in(int slot);
    // Uniform over the slot's usable width and decorrelated between devices:
    // two aircraft with different addresses do not collide every second, and
    // one aircraft's instant is reproducible in a test.
    int instant_in(int slot, uint32_t utc) const;
    int instant_between(int first, int last, uint32_t utc) const;

    AirTime air_{};
    uint32_t addr_{0};
    uint32_t sent_{0};
    uint32_t last_sent_utc_{0};
    uint32_t last_callsign_utc_{0};
    bool ever_sent_{false};
    bool ever_named_{false};
};

}  // namespace skyblip::timing

#endif
