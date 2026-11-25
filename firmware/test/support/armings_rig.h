// Harness, not a test: RadioService alone on a recording ports::Rf, every plan it hands the
// executor kept for reading, under a clock a case sets and can let creep.
#ifndef SKYBLIP_TEST_SUPPORT_ARMINGS_RIG_H
#define SKYBLIP_TEST_SUPPORT_ARMINGS_RIG_H

#include <cstdint>

#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/diagnostics/recorder.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/identity.h"
#include "hardware/platform/host/random.h"
#include "ports/null.h"
#include "ports/rf.h"
#include "products/skyblip_go/services/radio.h"
#include "products/skyblip_go/settings.h"
#include "runtime/service.h"
#include "test/support/receiver_at_rate.h"

namespace skyblip {

// Silicon's clock moves while a pass runs, and the host's stands still unless told to creep.
struct CreepingClock : platform::host::Clock {
    uint64_t microseconds() const override {
        const uint64_t now_us = Clock::microseconds() + crept_us_;
        crept_us_ += creep_us;
        return now_us;
    }
    void set_microseconds(uint64_t now_us) {
        Clock::set_microseconds(now_us);
        crept_us_ = 0;
    }
    uint64_t creep_us{0};

   private:
    mutable uint64_t crept_us_{0};
};

// The policy alone, with every plan it hands the executor kept for reading.
struct Armings {
    CreepingClock clock{};
    struct Recorder : ports::Rf {
        Status begin() override { return Status::Ok; }
        Status arm(const ports::RfPlan& plan) override {
            last = plan;
            arms++;
            if (plan.tx != nullptr) with_tx++;
            if (!refuse && handed < kKept) {
                plans[handed] = plan;
                armed_at_us[handed] = clock->microseconds();
                handed++;
            }
            return refuse ? Status::OutOfRange : Status::Ok;
        }
        void abort() override { aborts++; }
        // INFO: fc 19nov26 each plan armed is a dwell entered and a channel read, unless told
        ports::RfCarrier carrier() const override {
            return ports::RfCarrier{-110, deaf ? 0 : arms};
        }
        ports::RfSwitching switching() const override {
            ports::RfSwitching started{};
            started.switches = stalled ? 0 : arms;
            started.refused = refused_dwells;
            return started;
        }
        Status withdraw(uint64_t tx_at_us) override {
            if (listening) return Status::WouldBlock;
            withdrawn++;
            withdrawn_at_us = tx_at_us;
            return Status::Ok;
        }
        static constexpr uint32_t kKept = 32;
        platform::host::Clock* clock{nullptr};
        ports::RfPlan last{};
        ports::RfPlan plans[kKept]{};
        uint64_t armed_at_us[kKept]{};
        uint32_t handed{0};
        uint32_t arms{0};
        uint32_t with_tx{0};
        uint32_t aborts{0};
        uint32_t withdrawn{0};
        uint64_t withdrawn_at_us{0};
        bool refuse{false};
        bool deaf{false};
        bool stalled{false};
        // INFO: fc 19nov26 the executor already reading the channel, so the burst is its own
        bool listening{false};
        uint32_t refused_dwells{0};
    } rf{};
    bus::Bus bus{};
    bus::State state{};
    platform::host::Identity identity{platform::host::Identity::kSerial};
    platform::host::Random random{};
    ports::NullRoles null{};
    ReceiverAtRate receiver{};
    ports::Roles roles{clock,
                       rf,
                       null.link,
                       null.display,
                       null.key_value,
                       null.log_flash,
                       null.annunciator,
                       null.dfu,
                       null.die_temperature,
                       null.indicator,
                       receiver,
                       identity,
                       random,
                       ports::Capability::Rf};
    diagnostics::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    traffic::VersionTable versions{};
    go::RadioService radio{context, settings, versions};

    Armings() {
        rf.clock = &clock;
        state.activation.serial = 0x7E57;
    }

    static constexpr uint64_t kEdgeUs = 30000000;

    void tick_at(int phase_ms) { tick_in(0, phase_ms); }

    void tick_in(uint32_t second, int phase_ms) {
        const uint64_t edge_us = kEdgeUs + static_cast<uint64_t>(second) * 1000000;
        const uint64_t now_us = edge_us + static_cast<uint64_t>(phase_ms) * 1000;
        clock.set_microseconds(now_us);
        state.clock.pps_locked = true;
        state.clock.utc_valid = true;
        state.clock.pps_edge_us = edge_us;
        if (solving) state.own.fix_at_ms = static_cast<uint32_t>(now_us / 1000);
        radio.tick(static_cast<uint32_t>(now_us / 1000));
    }

    // INFO: fc 18nov26 these cases are about the arming, so the newest solution is the pass's own
    void ready_to_transmit(uint32_t utc_s) {
        state.own.fix_valid = true;
        state.own.speed_valid = true;
        state.own.track_valid = true;
        state.own.utc_valid = true;
        state.own.tx_settled = true;
        state.own.flight_state = 2;
        state.own.utc_s = utc_s;
        state.own.fix_dated = true;
        solving = true;
    }

    bool solving{false};
};

}  // namespace skyblip

#endif
