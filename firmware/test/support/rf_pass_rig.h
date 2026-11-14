// Harness, not a test: the radio service, its host executor and the traffic service, by hand.
#ifndef SKYBLIP_TEST_SUPPORT_RF_PASS_RIG_H
#define SKYBLIP_TEST_SUPPORT_RF_PASS_RIG_H

#include <cstdint>

#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/diagnostics/recorder.h"
#include "hardware/platform/host/platform.h"
#include "ports/null.h"
#include "products/common/services/traffic.h"
#include "products/skyblip_go/features.h"
#include "products/skyblip_go/services/radio.h"
#include "products/skyblip_go/settings.h"
#include "runtime/service.h"

namespace skyblip {

// The radio policy and its executor with the board's job done by hand, so that
// the two halves of a service pass - the instant the PPS phase is read off the
// hardware, and the instant the policy uses it - can be pulled apart. On silicon
// they are separated by however long the services ahead of the radio took.
// Every part below the services is the real one: the driver, the chip model, the
// executor and the service that turns radio events into counters.
struct Pass {
    platform::host::Platform platform{};
    models::Sx1262& chip{platform.chips().radio};
    parts::Sx1262 radio{chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin};
    bus::Bus bus{};
    bus::State state{};
    platform::host::Rf rf{radio, platform.clock(), bus.rf};
    ports::NullRoles null{};
    ports::Roles roles{platform.clock(),
                       rf,
                       null.link,
                       null.display,
                       null.key_value,
                       null.log_flash,
                       null.annunciator,
                       null.dfu,
                       null.die_temperature,
                       null.indicator,
                       null.gnss,
                       platform.identity(),
                       ports::Capability::Rf};
    diagnostics::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    go::RadioService radio_service{context, settings};
    common::TrafficService traffic_service{context, go::kFeatures, settings.own_icao};

    Status begin() {
        const Status status = rf.begin();
        if (status != Status::Ok) return status;
        state.own.fix_valid = true;
        state.own.utc_valid = true;
        state.own.tx_settled = true;
        state.own.flight_state = 2;
        state.clock.utc_valid = true;
        state.activation.serial = 0x7E57;
        return radio_service.setup();
    }

    // Exactly what boards/lilygo/t_echo_plus/board.h does at the top of a pass.
    void poll_clock(uint64_t now_us) {
        platform.clock().set_microseconds(now_us);
        state.clock.pps_locked = platform.pps().locked();
        state.clock.since_edge_ms = platform.pps().since_edge_ms(now_us);
        state.clock.pps_edge_us = platform.pps().last_edge_us();
    }

    void whole_pass(uint64_t now_us) {
        poll_clock(now_us);
        services_at(now_us);
    }

    void services_at(uint64_t now_us) {
        platform.clock().set_microseconds(now_us);
        const uint32_t now_ms = static_cast<uint32_t>(now_us / 1000);
        state.own.utc_s = now_ms / 1000;
        state.own.fix_at_ms = now_ms;
        rf.service(now_ms);
        radio_service.tick(now_ms);
        traffic_service.tick(now_ms);
    }

    // The executor between service passes: it runs on its own thread on silicon,
    // so it sees time the service list does not.
    void executor_until(uint64_t until_us, uint64_t step_us = 100) {
        for (uint64_t now_us = platform.clock().microseconds() + step_us; now_us <= until_us;
             now_us += step_us) {
            platform.clock().set_microseconds(now_us);
            rf.service(static_cast<uint32_t>(now_us / 1000));
            if (chip.tx_pending && first_tx_us == 0) first_tx_us = now_us;
        }
    }

    uint64_t first_tx_us{0};
};

}  // namespace skyblip

#endif
