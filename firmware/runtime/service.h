#ifndef SKYBLIP_RUNTIME_SERVICE_H
#define SKYBLIP_RUNTIME_SERVICE_H

#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/diag/recorder.h"
#include "core/util/result.h"
#include "ports/roles.h"

namespace skyblip::runtime {

// INFO: fc 20sep26 many producers and one consumer, which is neither a bus queue nor blackboard
struct Context {
    ports::Roles& roles;
    bus::Bus& bus;
    bus::State& state;
    diag::Recorder& diag;

    diag::Instant instant(uint32_t now_ms) const {
        diag::Instant at{};
        at.at_s = state.traffic_now_s(now_ms);
        at.utc_dated = (state.clock.pps_locked && state.clock.utc_s != 0) || state.own.utc_valid;
        at.phase_valid = state.clock.pps_locked && state.clock.ms_since_pps < 1000;
        at.into_ms = at.phase_valid ? static_cast<uint16_t>(state.clock.ms_since_pps)
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

    // Whether this service made progress this pass. The loop feeds the watchdog
    // only on behalf of services that say yes, so a service that knows it is
    // wedged takes the device down rather than riding along on a loop that is
    // still spinning. Default: reaching tick() is progress enough.
    virtual bool progressing(uint32_t /*now_ms*/) const { return true; }

   protected:
    Context& context_;
};

}  // namespace skyblip::runtime

#endif
