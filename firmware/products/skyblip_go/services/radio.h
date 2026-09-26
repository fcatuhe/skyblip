#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RADIO_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RADIO_H

#include "core/diag/payload.h"
#include "core/flight/state.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "core/radio/log.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "products/skyblip_go/settings.h"
#include "runtime/service.h"

namespace skyblip::go {

// INFO: fc 15sep26 slot policy only, ports::Rf flies it against absolute deadlines and owns the
// chip
class RadioService : public runtime::Service {
   public:
    RadioService(runtime::Context& context, const Settings& settings)
        : runtime::Service(context), settings_(settings) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;

    ports::RfMode armed_mode() const { return armed_; }
    uint32_t arm_count() const { return arm_count_; }
    const timing::Transmitter& transmitter() const { return transmitter_; }

    const timing::NoiseFloor& noise_floor() const { return noise_; }
    uint32_t duty_permille(uint32_t now_ms) const {
        return transmitter_.air_time().permille(now_ms);
    }
    bool over_budget() const { return over_budget_; }

   private:
    // Both from micros(), which is 64-bit and does not wrap: nothing about where
    // the radio believes it is inside the second reads the 32-bit millisecond
    // counter (ports/clock.h).
    int phase_ms() const;
    uint64_t pps_epoch_us() const;
    // Slot 1 spans the UTC second, so inside its tail the dwell, the burst it
    // carries and the second they are accounted to all belong to the second the
    // slot started in.
    uint64_t dwell_epoch_us() const;
    // INFO: fc 13sep26 negative once the instant is past, and the tail's dwell opened a second ago
    static int ms_until(int dwell_phase_ms, int phase_ms);
    uint32_t slot_utc(uint32_t now_ms) const;
    int32_t fix_lag_ms() const;
    protocol::BurstInstant burst_instant(const timing::Transmitter::Attempt& attempt,
                                         uint64_t tx_at_us, uint32_t utc) const;
    static ports::RfMode mode_for(const timing::SlotPlan& plan);
    static void listen_for(timing::Band band, ports::RfPlan& plan);
    timing::Transmitter::Attempt attempt(const timing::SlotPlan& plan, uint32_t now_ms) const;
    bool transmit_due(const timing::SlotPlan& plan, uint32_t now_ms) const;
    void arm_dwell(const timing::SlotPlan& slot, uint32_t now_ms);
    void record_dwell(const timing::SlotPlan& slot, const timing::Transmitter::Attempt& attempt,
                      int phase, bool armed, bool carries_tx, uint32_t now_ms);
    diag::Refusal refusal_of(const timing::SlotPlan& slot,
                             const timing::Transmitter::Attempt& attempt, bool armed,
                             bool carries_tx) const;
    void log_refusal(radio::Event outcome, const timing::SlotPlan& slot, uint32_t now_ms);
    void publish_dwell(uint32_t now_ms);
    void accrue_armed();
    void collect_outcome(uint32_t now_ms);
    void take_carrier_samples();

    timing::Transmitter transmitter_{};
    timing::NoiseFloor noise_{};
    // The transmit buffer, and the only writer it has: protocol::from_own, out of
    // own-ship state, the device address and the settings. There is deliberately no
    // loopback guard in front of it. SoftRF has one, because a transmission that
    // was the last frame received did happen to it ("$PSRFE,RF loopback is
    // detected on Tx", src/driver/RF.cpp:381-396), and the shape of that firmware
    // is why: one driver owning a shared Tx/Rx buffer pair, with relay and bridge
    // paths that do put received traffic back on air. Here a received frame's only
    // path is the RfEvent queue into TrafficService and the traffic table, and
    // nothing transmits from either. Adding the comparison would put work inside a
    // dwell to defend against a state this composition cannot reach; the property
    // is asserted over the air instead, in test/products/test_rf_timing.cpp.
    protocol::AdslPacket outgoing_{};
    uint8_t outgoing_chips_[protocol::kTxPayloadChipBytes]{};
    ports::RfMode armed_{ports::RfMode::Idle};
    uint32_t armed_freq_{0};
    uint64_t armed_from_us_{0};
    uint64_t armed_until_us_{0};
    uint64_t accounted_us_{0};
    uint64_t armed_us_{0};
    uint32_t arm_count_{0};
    uint64_t tx_end_us_{0};
    uint32_t tx_utc_{0};
    timing::Transmitter::Payload tx_payload_{timing::Transmitter::Payload::Position};
    uint32_t seen_tx_ok_{0};
    uint32_t seen_carrier_samples_{0};
    bool tx_armed_{false};
    bool over_budget_{false};
    bool held_logged_{false};
    const Settings& settings_;
};

}  // namespace skyblip::go

#endif
