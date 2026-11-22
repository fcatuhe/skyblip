#ifndef SKYBLIP_RUNTIME_TASKS_H
#define SKYBLIP_RUNTIME_TASKS_H

#include <cstdint>

namespace skyblip::runtime {

// TODO: fc 21nov26 the radio thread's two figures belong to hardware/, but sealed zephyr/rf.h names
// them here until ADS-L's next version
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
static_assert(kServiceRestCeilingMs < kServiceSilenceCeilingMs,
              "a loop with nothing due still ticks every service inside its silence ceiling");

constexpr uint32_t kRadioRxSilenceCeilingMs = 30000;

}  // namespace skyblip::runtime

#endif
