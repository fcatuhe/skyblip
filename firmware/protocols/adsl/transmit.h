// INFO: fc 15sep26 own-ship transmit policy, ADS-L 4 SRD-860 issue 2 §C.5 and §G.1.16, never LBT
#ifndef SKYBLIP_PROTOCOLS_ADSL_TRANSMIT_H
#define SKYBLIP_PROTOCOLS_ADSL_TRANSMIT_H

#include <cstdint>

#include "protocols/adsl/flight_state.h"

namespace skyblip::adsl {

// INFO: fc 13nov26 §C.5's direct slot, which every burst of ours starts and completes inside
constexpr int kDirectStartMs = 450;
constexpr int kDirectEndMs = 1000;
// INFO: fc 28sep26 §C.2 at 100 kchip/s: 16 preamble + 64 sync + 24 bytes Manchester, 4.64 ms
constexpr uint32_t kAirTimeMs = 5;
// INFO: fc 13sep26 G.1.16 nav age, to the top of the transmit second: the burst is extrapolated
constexpr int32_t kFixMaxAgeMs = 500;
// INFO: fc 13nov26 G.2.11 asks for a Status at least once every 20 s
constexpr uint32_t kStatusPeriodS = 20;

uint32_t ground_second(uint32_t address);

inline uint32_t period_s(bool airborne) {
    return report_period_s(airborne ? FlightState::Airborne : FlightState::Ground);
}
bool on_schedule(uint32_t address, uint32_t utc_s, bool airborne);
// INFO: fc 16sep26 §C.2.5 alternates the channel per transmission, and the clock counts them
inline int channel_in(uint32_t utc_s, bool airborne) {
    return static_cast<int>((utc_s / period_s(airborne)) & 1u);
}

inline bool status_due(uint32_t address, uint32_t utc_s) {
    return utc_s % kStatusPeriodS == ground_second(address);
}
// INFO: fc 13nov26 §C.2.5 alternates Status as it does Traffic, on its own count
inline int status_channel_in(uint32_t utc_s) {
    return static_cast<int>((utc_s / kStatusPeriodS) & 1u);
}

int instant_between(uint32_t address, uint32_t utc_s, int first, int last);
// INFO: fc 13nov26 drawn around the position's instant, so two bursts sharing a dwell never overlap
int status_instant_between(uint32_t address, uint32_t utc_s, int first, int last, int apart_ms);

static_assert(kStatusPeriodS % kGroundReportPeriodS == 0,
              "the Status takes the ground second every other time it comes round");

// Sixty one-minute buckets on a ring that turns by elapsed time, so a bucket that
// fell out of the window contributes nothing without anything having to sweep it.
class AirTime {
   public:
    static constexpr uint32_t kWindowMs = 3600000;
    static constexpr uint8_t kBuckets = 60;
    static constexpr uint32_t kBucketMs = kWindowMs / kBuckets;
    // INFO: fc 15sep26 the hour of EN 300 220-2 V3.3.1 Table 4 band M is the only rule refusing a
    // burst
    static constexpr uint32_t kLimitPermille = 10;
    static constexpr uint32_t kBudgetMs = kWindowMs / 1000 * kLimitPermille;

    void spend(uint32_t now_ms, uint32_t air_ms);

    uint32_t window_ms(uint32_t now_ms) const;
    uint32_t permille(uint32_t now_ms) const { return window_ms(now_ms) * 1000u / kWindowMs; }
    bool may_spend(uint32_t now_ms, uint32_t air_ms) const {
        return window_ms(now_ms) + air_ms <= kBudgetMs;
    }

    uint32_t bursts() const { return bursts_; }
    uint32_t total_ms() const { return total_ms_; }

   private:
    uint32_t bucket_ms_[kBuckets]{};
    // The bucket now_ms falls in, and the instant it opened. The ring turns by
    // ELAPSED time and every age below is an unsigned difference from that
    // instant, because a bucket numbered now_ms / kBucketMs is not a bucket: that
    // number restarts at zero when the counter wraps, and 2^32 ms is not a whole
    // number of minutes either, so numbered buckets throw the whole hour away at
    // the wrap and let the hour straddling it spend the band's allowance twice
    // (ports/clock.h, rule 1).
    uint32_t head_start_ms_{0};
    uint8_t head_{0};
    bool started_{false};
    uint32_t bursts_{0};
    uint32_t total_ms_{0};
};

}

#endif
