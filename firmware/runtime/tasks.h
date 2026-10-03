#ifndef SKYBLIP_RUNTIME_TASKS_H
#define SKYBLIP_RUNTIME_TASKS_H

#include <cstdint>

namespace skyblip::runtime {

enum class TaskPrio : uint8_t {
    Rf = 5,
    Gnss = 4,
    Services = 3,
    Comms = 2,
    Ui = 1,
};

constexpr uint32_t kServiceStepMs = 10;
// INFO: fc 03oct26 a stream of '\n' or a ringing PPS pin wakes at most 500 passes a second
constexpr uint32_t kServicePassFloorMs = 2;
// The longest a supervised service may go without reporting progress before the
// loop stops feeding the dog. Five hundred passes: nothing healthy is that slow,
// and it is well inside the hardware rope below.
constexpr uint32_t kTaskWatchdogMs = 5000;
// SoftRF's figure on the same silicon (src/platform/nRF52.cpp:4558). The rope
// the whole device gets: longer than kTaskWatchdogMs so the loop's own refusal
// to feed is what is seen first, and the bite is the backstop under it.
constexpr uint32_t kHardwareWatchdogMs = 12000;
constexpr uint32_t kRadioNoRxReinitMs = 30000;
constexpr uint32_t kPpsLossListenOnlyMs = 60000;
constexpr uint32_t kBaroPeriodMs = 1000;
constexpr uint32_t kBaroPpsWindowMs = 2 * kServiceStepMs;
// INFO: fc 03oct26 four forced conversions a second feed IIR 4: 0.75 s of lag for 0.1 mA
constexpr uint32_t kBaroConversionPeriodMs = 250;
// INFO: fc 03oct26 Zephyr's BME280_MEASUREMENT_TIMEOUT_MS, over 3x the DS 9.1 worst case
constexpr uint32_t kBaroConversionCeilingMs = 150;
// A cell moves over minutes. The gauge needs three readings before it can throw
// out a transient, so a second between them is the slowest cadence that still
// shows the state of charge on the first screen a pilot sees.
constexpr uint32_t kBatteryPeriodMs = 1000;

}  // namespace skyblip::runtime

#endif
