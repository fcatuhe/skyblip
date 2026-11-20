#ifndef SKYBLIP_RUNTIME_TASKS_H
#define SKYBLIP_RUNTIME_TASKS_H

#include <cstdint>

namespace skyblip::runtime {

enum class TaskPriority : uint8_t {
    Rf = 5,
};

constexpr uint32_t kServicePassCeilingMs = 10;
// INFO: fc 19nov26 the longest the loop sleeps with nothing due, well inside the silence ceiling
constexpr uint32_t kServiceRestCeilingMs = 1000;
// INFO: fc 03oct26 a stream of '\n' or a ringing PPS pin wakes at most 500 passes a second
constexpr uint32_t kServicePassFloorMs = 2;
// INFO: fc 20nov26 five hundred passes without progress: nothing healthy is that slow
constexpr uint32_t kServiceSilenceCeilingMs = 5000;
// INFO: fc 20nov26 SoftRF's nRF52 figure (src/platform/nRF52.cpp:4558), above
// kServiceSilenceCeilingMs so the loop refuses to feed first
constexpr uint32_t kHardwareWatchdogCeilingMs = 12000;
static_assert(kServiceRestCeilingMs < kServiceSilenceCeilingMs,
              "a loop with nothing due still ticks every service inside its silence ceiling");
constexpr uint32_t kRadioRxSilenceCeilingMs = 30000;
constexpr uint32_t kBarometerPublishPeriodMs = 1000;
constexpr uint32_t kBarometerPpsWindowMs = 2 * kServicePassCeilingMs;
// INFO: fc 03oct26 four forced conversions a second feed IIR 4: 0.75 s of lag for 0.1 mA
constexpr uint32_t kBarometerConversionPeriodMs = 250;
// INFO: fc 03oct26 Zephyr's BME280_MEASUREMENT_TIMEOUT_MS, over 3x the DS 9.1 worst case
constexpr uint32_t kBarometerConversionCeilingMs = 150;
// INFO: fc 20nov26 the gauge needs three readings to reject a transient, so this still shows charge
// on the first screen
constexpr uint32_t kBatteryPeriodMs = 1000;

}  // namespace skyblip::runtime

#endif
