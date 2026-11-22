#ifndef SKYBLIP_BOARDS_COMMON_PRODUCER_H
#define SKYBLIP_BOARDS_COMMON_PRODUCER_H

#include <cstdint>

#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/events/input.h"
#include "core/events/sensor.h"
#include "core/input/contact.h"
#include "core/power/battery.h"
#include "core/timing/slot.h"
#include "ports/capabilities.h"
#include "runtime/service.h"
#include "runtime/tasks.h"

namespace skyblip::boards {

constexpr uint32_t kBarometerPublishPeriodMs = 1000;
constexpr uint32_t kBarometerPpsWindowMs = 2 * runtime::kServicePassCeilingMs;
// INFO: fc 03oct26 Zephyr's BME280_MEASUREMENT_TIMEOUT_MS, over 3x the DS 9.1 worst case
constexpr uint32_t kBarometerConversionCeilingMs = 150;

// INFO: fc 19nov26 published on the PPS window when locked, a second after the last otherwise
inline bool barometer_due(const timing::ClockState& clock, uint32_t since_published_ms) {
    if (since_published_ms < kBarometerPublishPeriodMs / 2) return false;
    if (!clock.pps_locked) return since_published_ms >= kBarometerPublishPeriodMs;
    return clock.since_edge_ms < kBarometerPpsWindowMs ||
           since_published_ms >= 2 * kBarometerPublishPeriodMs;
}

// INFO: fc 19nov26 a locked PPS edge wakes the loop itself, so only the fallback needs a pass
inline uint32_t barometer_due_in_ms(const timing::ClockState& clock, uint32_t since_published_ms) {
    return runtime::remaining_ms(since_published_ms, clock.pps_locked
                                                         ? 2 * kBarometerPublishPeriodMs
                                                         : kBarometerPublishPeriodMs);
}

// INFO: fc 19nov26 what both boards read the same way: the cell, the contacts and the PPS edge
template <class Platform>
class Producer {
   public:
    Producer(Platform& platform, bus::Bus& bus) : platform_(platform), bus_(bus) {}

    void poll_battery(ports::Capabilities capabilities, uint32_t now_ms) {
        if (!ports::has(capabilities, ports::Capability::Battery)) return;
        if (now_ms - last_battery_ms_ < power::kBatteryPeriodMs) return;
        last_battery_ms_ = now_ms;
        uint16_t divider_mv = 0;
        // INFO: fc 05oct26 refused or not, the reading and VBUS go out: core/power judges it
        platform_.read_divider_mv(divider_mv);
        bus_.battery.push(events::BatterySample{divider_mv, platform_.external_power()});
    }

    uint32_t battery_due_in_ms(ports::Capabilities capabilities, uint32_t now_ms) const {
        if (!ports::has(capabilities, ports::Capability::Battery))
            return runtime::kServiceRestCeilingMs;
        return runtime::remaining_ms(now_ms - last_battery_ms_, power::kBatteryPeriodMs);
    }

    // INFO: fc 19nov26 true while the contact is down or settling, which only passes can time
    bool publish_contact(events::Contact which, input::Contact& contact, bool level,
                         uint32_t now_ms) {
        const input::Contact::Edge edge = contact.update(level, now_ms);
        if (edge != input::Contact::Edge::None)
            bus_.input.push(
                events::ContactEvent{which, edge == input::Contact::Edge::Down, contact.edge_ms()});
        return level || contact.down();
    }

    void publish_clock(bus::State& state) {
        const uint64_t now_us = platform_.clock().microseconds();
        state.clock.pps_locked = platform_.pps().locked();
        // INFO: fc 19nov26 the latched edge, as rebuilding it from the phase lost up to 1 ms
        timing::take_edge(state.clock, platform_.pps().last_edge_us(), now_us);
        // INFO: fc 19nov26 the one reader of the PPS edge, so the bench histogram is fed here
        state.rf.timing_statistics.record_edge(state.clock.pps_edge_us, state.clock.pps_locked);
    }

   private:
    Platform& platform_;
    bus::Bus& bus_;
    uint32_t last_battery_ms_{0};
};

}  // namespace skyblip::boards

#endif
