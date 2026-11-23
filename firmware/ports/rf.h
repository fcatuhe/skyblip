#ifndef SKYBLIP_PORTS_RF_H
#define SKYBLIP_PORTS_RF_H

#include <algorithm>
#include <cstdint>

#include "core/util/result.h"

namespace skyblip::ports {

enum class RfMode : uint8_t { Idle, RxMBand, RxOBand };

// INFO: fc 22nov26 the receive dwell can outlast the slot that bounds its burst
struct RfPlan {
    uint64_t start_us{0};
    uint64_t end_us{0};
    RfMode mode{RfMode::Idle};
    uint32_t frequency_hz{0};
    // INFO: fc 20nov26 what the dwell listens for, not a protocol: two systems can share a window
    const uint8_t* sync{nullptr};
    uint8_t sync_bits{0};
    uint8_t rx_bytes{0};
    // INFO: fc 20nov26 modulation differs by band (SRD-860 §C.2, §C.4), and 0 keeps the last
    uint32_t bitrate{0};
    uint32_t deviation_hz{0};
    uint32_t bandwidth_hz{0};
    // INFO: fc 20nov26 0 is unshaped
    uint16_t gaussian_bt_e2{0};
    // INFO: fc 20nov26 positive upwards, per dwell since each retune would drop a boot trim
    int16_t frequency_correction_e1_ppm{0};
    const uint8_t* tx{nullptr};
    uint8_t tx_bytes{0};
    // INFO: fc 14nov26 the burst keys here on a channel assessed clear, else it is reported Busy
    uint64_t tx_at_us{0};
    // INFO: fc 14nov26 ADS-L §D.3's forced transmission: keyed irrespective of carrier detect
    bool tx_forced{false};
    uint64_t tx_last_key_us{UINT64_MAX};
};

struct RfBurst {
    // INFO: fc 22nov26 the simulator keys up to one 5 ms step late, the bench at most 0.33 ms
    static constexpr uint64_t kKeyLatenessCeilingUs = 5000;

    const uint8_t* chips{nullptr};
    uint8_t bytes{0};
    uint64_t at_us{0};
    bool forced{false};
    uint64_t last_key_us{UINT64_MAX};

    static RfBurst of(const RfPlan& plan) {
        return {plan.tx, plan.tx_bytes, plan.tx_at_us, plan.tx_forced,
                std::min(plan.tx_last_key_us, plan.end_us - 1)};
    }

    bool key_expired(uint64_t now_us) const {
        return now_us > last_key_us || (now_us > at_us && now_us - at_us >= kKeyLatenessCeilingUs);
    }
};

struct RfBursts {
    static constexpr uint8_t kMaxBursts = 2;
    static constexpr uint64_t kNever = UINT64_MAX;
    RfBurst burst[kMaxBursts]{};
    uint8_t count{0};

    bool full() const { return count == kMaxBursts; }

    // INFO: fc 29sep26 none while one is on air: the next stages into the buffer it is sent from
    uint64_t stage_at_us(uint8_t keyed, uint8_t done, uint64_t lead_us) const {
        if (keyed != done || keyed >= count) return kNever;
        const uint64_t at_us = burst[keyed].at_us;
        return at_us > lead_us ? at_us - lead_us : 0;
    }

    int index_of(uint64_t at_us) const {
        for (uint8_t i = 0; i < count; i++)
            if (burst[i].at_us == at_us) return i;
        return -1;
    }

    bool holds(uint64_t at_us) const { return index_of(at_us) >= 0; }

    void remove(uint8_t index) {
        if (index >= count) return;
        for (uint8_t i = index; i + 1 < count; i++) burst[i] = burst[i + 1];
        count--;
    }

    bool add(const RfBurst& added) {
        if (holds(added.at_us)) return true;
        if (full()) return false;
        uint8_t at = count++;
        for (; at > 0 && burst[at - 1].at_us > added.at_us; at--) burst[at] = burst[at - 1];
        burst[at] = added;
        return true;
    }

    static RfBursts of(const RfPlan& plan) {
        RfBursts bursts{};
        if (plan.tx != nullptr) bursts.add(RfBurst::of(plan));
        return bursts;
    }
};

// INFO: fc 15sep26 a diagnostic the pilot reads, never a gate: core/timing/README.md
struct RfCarrier {
    int8_t level_dbm{0};
    uint32_t samples{0};
};

struct RfTransmitter {
    int8_t power_dbm{0};
    int8_t pa_rated_dbm{0};
};

struct RfDio1 {
    bool armed{false};
    uint32_t edges{0};
    uint32_t missed{0};
};

struct RfSwitch {
    RfMode from{RfMode::Idle};
    RfMode to{RfMode::Idle};
    uint32_t from_hz{0};
    uint32_t to_hz{0};
    uint64_t previous_end_us{0};
    uint64_t began_us{0};
    uint64_t ready_us{0};
    uint64_t start_us{0};
    bool armed_ahead{false};

    uint32_t took_us() const { return static_cast<uint32_t>(ready_us - began_us); }
    uint32_t gap_us() const {
        return previous_end_us != 0 && began_us > previous_end_us
                   ? static_cast<uint32_t>(began_us - previous_end_us)
                   : 0;
    }
    int64_t margin_us() const {
        return static_cast<int64_t>(start_us) - static_cast<int64_t>(ready_us);
    }
    bool late_start() const { return armed_ahead && ready_us > start_us; }
};

// INFO: fc 20nov26 worst since boot, from a retune's first command to the part reporting its mode
struct RfSwitching {
    uint32_t hop_us{0};
    uint32_t to_o_band_us{0};
    uint32_t to_m_band_us{0};
    uint32_t late_starts{0};
    uint32_t switches{0};
    // INFO: fc 19nov26 dwells the radio would not enter: wake, configure or receive went unanswered
    uint32_t refused{0};

    void refuse() { refused++; }
    RfSwitch last{};

    void note(const RfSwitch& change) {
        switches++;
        last = change;
        if (change.late_start()) late_starts++;
        uint32_t* worst = nullptr;
        const RfMode from = change.from;
        const RfMode to = change.to;
        if (to == RfMode::RxOBand && from == RfMode::RxMBand) worst = &to_o_band_us;
        if (to == RfMode::RxMBand && from == RfMode::RxOBand) worst = &to_m_band_us;
        if (to == RfMode::RxMBand && from == RfMode::RxMBand && change.to_hz != change.from_hz)
            worst = &hop_us;
        if (worst != nullptr && change.took_us() > *worst) *worst = change.took_us();
    }
};

class Rf {
   public:
    virtual ~Rf() = default;

    virtual Status begin() = 0;
    // INFO: fc 27sep26 the join, queue and refusal rules are core/timing/README.md's
    virtual Status arm(const RfPlan& plan) = 0;
    virtual void abort() = 0;
    // INFO: fc 14nov26 drops a burst not yet keyed, from the flying or the queued dwell
    virtual Status withdraw(uint64_t tx_at_us) = 0;

    virtual RfCarrier carrier() const { return RfCarrier{}; }

    virtual RfSwitching switching() const { return RfSwitching{}; }

    virtual RfTransmitter transmitter() const { return RfTransmitter{}; }

    virtual RfDio1 dio1() const { return RfDio1{}; }

    // INFO: fc 08nov26 terminal: once parked, the executor arms, recovers and reinitialises nothing
    virtual void sleep() {}

    // INFO: fc 08nov26 SYSTEM OFF drives the radio itself, so it waits for this to hand the bus
    // over
    virtual bool parked() const { return true; }

    // INFO: fc 20nov26 takes the bus from an executor not parked in time, so SYSTEM OFF goes on
    virtual void abandon() {}
};

}  // namespace skyblip::ports

#endif
