#ifndef SKYBLIP_PORTS_RF_H
#define SKYBLIP_PORTS_RF_H

#include <cstdint>

#include "core/util/result.h"

namespace skyblip::ports {

enum class RfMode : uint8_t { Idle, RxMband, RxOband };

// One armed dwell, in absolute microseconds on the clock the executor reads.
// A dwell that cannot complete before end_us is refused, never truncated on air:
// ADS-L 4 SRD-860 issue 2 §C.5 requires a burst to finish inside its slot.
//
// A dwell may carry one transmission: the executor receives on freq_hz until
// tx_at_us, puts the burst on air, and returns to receiving for the remainder.
// Splitting that into two plans would leave the slot deaf between them.
struct RfPlan {
    uint64_t start_us{0};
    uint64_t end_us{0};
    RfMode mode{RfMode::Idle};
    uint32_t freq_hz{0};
    // What the receiver's sync detector matches during this dwell, and how many
    // bytes it reads once it has. A window can be shared by two systems
    // (core/protocol/air.h), so this is what the dwell listens FOR, never which
    // protocol it expects to get.
    const uint8_t* sync{nullptr};
    uint8_t sync_bits{0};
    uint8_t rx_len{0};
    // And what the modem has to be programmed to before any of that means
    // anything. The two bands are two modulations, not one modulation on two
    // frequencies (ADS-L 4 SRD-860 issue 2 §C.2 against §C.4), so a dwell that
    // carried only freq_hz retuned the synthesiser and left the receiver
    // framing the wrong band's chip rate. Zero means "whatever the last dwell
    // used", which is what a plan built before this field existed asked for.
    // gaussian_bt_e2 is the Gaussian filter's BT in hundredths; 0 is unshaped.
    uint32_t bitrate{0};
    uint32_t fdev_hz{0};
    uint32_t bandwidth_hz{0};
    uint16_t gaussian_bt_e2{0};
    // The reference's own error, in tenths of a ppm, positive upwards. It travels
    // with the dwell rather than being programmed once at boot because freq_hz
    // is: the executor retunes on every dwell, so a trim that was not part of the
    // plan would be a value the next retune throws away.
    int16_t freq_corr_e1_ppm{0};
    const uint8_t* tx{nullptr};
    uint8_t tx_len{0};
    // INFO: fc 15sep26 the burst keys here, full stop: nothing the receiver hears moves it
    uint64_t tx_at_us{0};
};

struct RfBurst {
    const uint8_t* chips{nullptr};
    uint8_t len{0};
    uint64_t at_us{0};
};

struct RfBursts {
    static constexpr uint8_t kCapacity = 2;
    RfBurst burst[kCapacity]{};
    uint8_t count{0};

    bool full() const { return count == kCapacity; }

    bool holds(uint64_t at_us) const {
        for (uint8_t i = 0; i < count; i++)
            if (burst[i].at_us == at_us) return true;
        return false;
    }

    bool add(const RfBurst& b) {
        if (holds(b.at_us)) return true;
        if (full()) return false;
        uint8_t at = count++;
        for (; at > 0 && burst[at - 1].at_us > b.at_us; at--) burst[at] = burst[at - 1];
        burst[at] = b;
        return true;
    }

    static RfBursts of(const RfPlan& plan) {
        RfBursts bursts{};
        if (plan.tx != nullptr) bursts.add(RfBurst{plan.tx, plan.tx_len, plan.tx_at_us});
        return bursts;
    }
};

// INFO: fc 15sep26 a diagnostic the pilot reads, never a gate: core/timing/README.md
struct RfCarrier {
    int8_t dbm{0};
    uint32_t samples{0};
};

struct RfTransmitter {
    int8_t power_dbm{0};
    int8_t pa_rated_dbm{0};
};

// What the executor measured of its own dwell changes, from the retune's first
// command to the part reporting the new mode: the worst since boot for each kind
// of change, and how many dwells armed ahead were still not listening at their start.
struct RfSwitching {
    uint32_t hop_us{0};
    uint32_t to_oband_us{0};
    uint32_t to_mband_us{0};
    uint32_t late{0};

    void note(RfMode from, uint32_t from_hz, RfMode to, uint32_t to_hz, uint32_t took_us,
              bool late_start) {
        if (late_start) late++;
        uint32_t* worst = nullptr;
        if (to == RfMode::RxOband && from == RfMode::RxMband) worst = &to_oband_us;
        if (to == RfMode::RxMband && from == RfMode::RxOband) worst = &to_mband_us;
        if (to == RfMode::RxMband && from == RfMode::RxMband && to_hz != from_hz) worst = &hop_us;
        if (worst != nullptr && took_us > *worst) *worst = took_us;
    }
};

class Rf {
   public:
    virtual ~Rf() = default;

    virtual Status begin() = 0;
    // INFO: fc 27sep26 the join, queue and refusal rules are core/timing/README.md's
    virtual Status arm(const RfPlan& plan) = 0;
    virtual void abort() = 0;

    virtual RfCarrier carrier() const { return RfCarrier{}; }

    virtual RfSwitching switching() const { return RfSwitching{}; }

    virtual RfTransmitter transmitter() const { return RfTransmitter{}; }

    // Put the transceiver in its lowest-power state until the next begin().
    // Called on the way to SYSTEM OFF: the receiver is armed through most of
    // every second by design, so a radio left in RX is what flattens the pack.
    // Virtual-with-default, so an executor that has nothing to sleep stays
    // valid; both SX1262 executors override it with SetSleep (0x84).
    virtual void sleep() {}
};

}  // namespace skyblip::ports

#endif
