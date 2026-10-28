#ifndef SKYBLIP_RUNTIME_TASKS_H
#define SKYBLIP_RUNTIME_TASKS_H

#include <cstdint>

namespace skyblip::runtime {

enum class TaskPriority : uint8_t {
    Rf = 5,
};

constexpr uint32_t kServicePassCeilingMs = 10;
// INFO: fc 03oct26 a stream of '\n' or a ringing PPS pin wakes at most 500 passes a second
constexpr uint32_t kServicePassFloorMs = 2;
// The longest a supervised service may go without reporting progress before the
// loop stops feeding the dog. Five hundred passes: nothing healthy is that slow,
// and it is well inside the hardware rope below.
constexpr uint32_t kServiceSilenceCeilingMs = 5000;
// SoftRF's figure on the same silicon (src/platform/nRF52.cpp:4558). The rope
// the whole device gets: longer than kServiceSilenceCeilingMs so the loop's own refusal
// to feed is what is seen first, and the bite is the backstop under it.
constexpr uint32_t kHardwareWatchdogCeilingMs = 12000;
constexpr uint32_t kRadioRxSilenceCeilingMs = 30000;
constexpr uint32_t kBarometerPublishPeriodMs = 1000;
constexpr uint32_t kBarometerPpsWindowMs = 2 * kServicePassCeilingMs;
// INFO: fc 03oct26 four forced conversions a second feed IIR 4: 0.75 s of lag for 0.1 mA
constexpr uint32_t kBarometerConversionPeriodMs = 250;
// INFO: fc 03oct26 Zephyr's BME280_MEASUREMENT_TIMEOUT_MS, over 3x the DS 9.1 worst case
constexpr uint32_t kBarometerConversionCeilingMs = 150;
// A cell moves over minutes. The gauge needs three readings before it can throw
// out a transient, so a second between them is the slowest cadence that still
// shows the state of charge on the first screen a pilot sees.
constexpr uint32_t kBatteryPeriodMs = 1000;

}  // namespace skyblip::runtime

#endif
