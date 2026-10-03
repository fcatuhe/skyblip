// Harness, not a test: the moves the settings-write cases share, a pass at a time
// on the whole product, so a case can say which millisecond of the second a write
// landed on.
#ifndef SKYBLIP_TEST_SUPPORT_SETTINGS_WRITES_H
#define SKYBLIP_TEST_SUPPORT_SETTINGS_WRITES_H

#include "core/timing/durable_write.h"
#include "test/support/product_rig.h"

namespace skyblip::settings_writes {

// One service pass at a time, so a case can say which millisecond of the second
// the write landed on.
inline void step_one(Rig& rig, uint32_t& t, uint32_t by_ms = 10) {
    rig.platform.clock().set_millis(t);
    rig.product.step(t);
    t += by_ms;
}

inline void step_until(Rig& rig, uint32_t& t, uint32_t until_ms) {
    while (t < until_ms) step_one(rig, t);
}

// Stationary timed solutions: core/flight answers Ground to those, and the UTC
// they carry is what anchors the second the write has to be placed inside. Both
// helpers leave t on a whole second, so a case can name the phase it wants.
inline void stand_on_the_ground(Rig& rig, uint32_t& t) { rig.seconds(t, 3, /*speed_mm_s=*/0, 0); }

inline void fly(Rig& rig, uint32_t& t) { rig.seconds(t, 16, /*speed_mm_s=*/50000, 1200); }

// The change a pilot makes on the panel, and the one a phone makes over the link,
// arrive at the same flag: comms::ConfigService is the single writer of the blob
// and this is how both editors say the struct moved.
inline void change_volume(Rig& rig, uint8_t to) {
    rig.settings().alarm_volume = to;
    rig.product.config().config().note_settings_changed();
}

inline uint32_t writes(Rig& rig) { return rig.platform.kv().writes(); }

// The phase of the second the device believes it is at, read off the radio's own
// published view rather than recomputed here.
inline int published_phase(Rig& rig) { return rig.state().rf.dwell.phase_ms; }

// The radio's view of the second, published at the instant a pass began.
inline void publish_dwell(Rig& rig, uint32_t pass_ms, int phase_ms) {
    timing::ClockState anchored{};
    anchored.utc_valid = true;
    anchored.pps_locked = true;
    rig.state().rf.plan = timing::Scheduler::plan(phase_ms, anchored);
    rig.state().rf.dwell = timing::DwellPhase{pass_ms, phase_ms, true, false};
}

// Runs until the write count moves, and answers the millisecond it moved on.
inline uint32_t wait_for_write(Rig& rig, uint32_t& t, uint32_t give_up_after_ms) {
    const uint32_t before = writes(rig);
    const uint32_t deadline = t + give_up_after_ms;
    while (t < deadline) {
        step_one(rig, t);
        if (writes(rig) != before) return t;
    }
    return 0;
}

}  // namespace skyblip::settings_writes

#endif
