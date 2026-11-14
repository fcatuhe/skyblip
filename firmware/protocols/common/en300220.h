#ifndef SKYBLIP_PROTOCOLS_COMMON_EN300220_H
#define SKYBLIP_PROTOCOLS_COMMON_EN300220_H

#include <cstdint>

namespace skyblip::en300220 {

// INFO: fc 14nov26 -2 Table 4 band M: 868.0 to 868.6 MHz, 25 mW e.r.p., 1 % duty cycle
constexpr uint32_t kBandMLowHz = 868000000;
constexpr uint32_t kBandMHighHz = 868600000;
constexpr uint32_t kBandMErpCeilingMw = 25;
constexpr uint32_t kBandMDutyCyclePermille = 10;
// INFO: fc 14nov26 -2 4.4.3.2: "Unless otherwise specified, Tobs is 1 hour"
constexpr uint32_t kDutyCycleWindowMs = 3600000;

// INFO: fc 14nov26 -1 Table 32: S = 10 log RB - 117 dBm, RB the declared receiver bandwidth in kHz
constexpr int32_t kSensitivityOffsetCentiDbm = -11700;
constexpr uint32_t kHzPerKhz = 1000;
// INFO: fc 14nov26 -1 Table 45, -2 Table 17: the CCA threshold above Table 32's sensitivity limit
constexpr int32_t kCcaMarginCentiDb = 1500;
constexpr int32_t kCcaMarginHighPowerCentiDb = 1100;
constexpr uint32_t kCcaHighPowerFloorMw = 100;

// INFO: fc 14nov26 -1 Table 48, -2 Table 18: the polite spectrum access timing limits
constexpr uint32_t kCcaIntervalFloorUs = 160;
constexpr uint32_t kDeferralFloorUs = kCcaIntervalFloorUs;
constexpr uint32_t kDeadTimeCeilingUs = 5000;
constexpr uint32_t kTransmissionCeilingMs = 1000;
constexpr uint32_t kDialogueCeilingMs = 4000;
constexpr uint32_t kCumulativeOnCeilingMs = 100000;
constexpr uint32_t kCumulativeOnWindowMs = 3600000;
constexpr uint32_t kCumulativeOnBandwidthHz = 200000;
constexpr uint32_t kSameFrequencyOffFloorMs = 100;

// INFO: fc 14nov26 10 log10 in hundredths of a dB: log2 in Q16, times 10 log10 2
constexpr int32_t ten_log10_centi(uint32_t value) {
    if (value == 0) return INT32_MIN;
    int whole = 0;
    while ((uint64_t{value} >> (whole + 1)) != 0) whole++;
    uint64_t mantissa =
        whole <= 30 ? uint64_t{value} << (30 - whole) : uint64_t{value} >> (whole - 30);
    int64_t log2_q16 = int64_t{whole} << 16;
    for (int bit = 15; bit >= 0; bit--) {
        mantissa = (mantissa * mantissa) >> 30;
        if (mantissa >= (uint64_t{2} << 30)) {
            mantissa >>= 1;
            log2_q16 += int64_t{1} << bit;
        }
    }
    constexpr int64_t kCentiDbPerOctaveE2 = 30103;
    constexpr int64_t kScale = int64_t{100} << 16;
    return static_cast<int32_t>((log2_q16 * kCentiDbPerOctaveE2 + kScale / 2) / kScale);
}

constexpr int32_t sensitivity_centi_dbm(uint32_t receiver_bandwidth_hz) {
    return ten_log10_centi(receiver_bandwidth_hz) - ten_log10_centi(kHzPerKhz) +
           kSensitivityOffsetCentiDbm;
}

// INFO: fc 14nov26 Table 45's note: a front end receive_gain above 0 dBd hears every signal louder
constexpr int32_t cca_threshold_centi_dbm(uint32_t receiver_bandwidth_hz, uint32_t erp_mw,
                                          int32_t receive_gain_centi_dbd) {
    const int32_t margin =
        erp_mw < kCcaHighPowerFloorMw ? kCcaMarginCentiDb : kCcaMarginHighPowerCentiDb;
    return sensitivity_centi_dbm(receiver_bandwidth_hz) + margin + receive_gain_centi_dbd;
}

// INFO: fc 14nov26 -1 5.21.1: below the threshold "the device proceeds with the transmission"
constexpr bool channel_clear(int32_t level_centi_dbm, int32_t threshold_centi_dbm) {
    return level_centi_dbm < threshold_centi_dbm;
}

}  // namespace skyblip::en300220

#endif
