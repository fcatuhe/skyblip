#ifndef SKYBLIP_RUNTIME_SERVICE_H
#define SKYBLIP_RUNTIME_SERVICE_H

#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/diagnostics/recorder.h"
#include "core/util/result.h"
#include "ports/roles.h"
#include "runtime/tasks.h"

namespace skyblip::runtime {

constexpr uint32_t remaining_ms(uint32_t since_ms, uint32_t period_ms) {
    return since_ms >= period_ms ? 0 : period_ms - since_ms;
}

// INFO: fc 20sep26 many producers and one consumer, which is neither a bus queue nor blackboard
struct Context {
    ports::Roles& roles;
    bus::Bus& bus;
    bus::State& state;
    diagnostics::Recorder& diagnostics;

    diagnostics::Instant instant(uint32_t now_ms) const {
        diagnostics::Instant at{};
        at.at_s = state.traffic_now_s(now_ms);
        at.utc_dated = state.utc_source(now_ms).has_value();
        at.phase_valid = state.clock.pps_locked && state.clock.since_edge_ms < 1000;
        at.into_ms = at.phase_valid ? static_cast<uint16_t>(state.clock.since_edge_ms)
                                    : static_cast<uint16_t>(now_ms % 1000);
        return at;
    }
};

class Service {
   public:
    explicit Service(Context& context) : context_(context) {}
    virtual ~Service() = default;

    virtual Status setup() { return Status::Ok; }
    virtual void tick(uint32_t now_ms) = 0;

    // INFO: fc 20nov26 false withholds the watchdog check-in, so a wedged service resets the device
    virtual bool progressing(uint32_t /*now_ms*/) const { return true; }

    // INFO: fc 19nov26 the longest the loop may sleep before this service needs a pass
    virtual uint32_t due_in_ms(uint32_t /*now_ms*/) const { return kServicePassCeilingMs; }

   protected:
    Context& context_;
};

}  // namespace skyblip::runtime

#endif
