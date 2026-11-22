#ifndef SKYBLIP_BOARDS_MESHTRACKER_X1_BOARD_H
#define SKYBLIP_BOARDS_MESHTRACKER_X1_BOARD_H

#include <algorithm>

#include "boards/common/producer.h"
#include "boards/seeed/meshtracker_x1/pins.h"
#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/events/input.h"
#include "core/events/link.h"
#include "core/events/sensor.h"
#include "core/input/contact.h"
#include "core/timing/slot.h"
#include "hardware/parts/ag3335/ag3335.h"
#include "hardware/parts/drv2605/drv2605.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "hardware/parts/spa06/spa06.h"
#include "ports/adsb_receiver.h"
#include "ports/null.h"
#include "ports/roles.h"
#include "runtime/tasks.h"

namespace skyblip::boards {

static_assert(kBarometerConversionCeilingMs < kBarometerPublishPeriodMs,
              "a SPA06 conversion is read or given up before the next one is triggered");

template <class Platform>
class MeshtrackerX1 {
   public:
    using Gnss = parts::Ag3335;

    MeshtrackerX1(Platform& platform, bus::Bus& bus, bus::AdsbQueue& adsb,
                  ports::SolutionRate solution_rate)
        : platform_(platform),
          bus_(bus),
          radio_(platform.spi(io::BusId::Radio), platform.gpio(), platform.delay(),
                 meshtracker_x1::kRadioBusy, meshtracker_x1::kRadioReset, meshtracker_x1::kRadioIrq,
                 meshtracker_x1::kRadioIrqDio),
          receiver_(radio_, platform.clock(), platform.radio_irq(), adsb),
          gnss_(solution_rate, platform.uart(io::BusId::Gnss)),
          barometer_(platform.i2c(io::BusId::Sensor)),
          haptic_(platform.i2c(io::BusId::Sensor), platform.gpio(), platform.delay(),
                  meshtracker_x1::kHapticEnable, meshtracker_x1::kHapticActuator),
          producer_(platform, bus),
          capabilities_(platform.capabilities()) {
        platform_.wire(meshtracker_x1::kPinMap);
        platform_.link().deliver_to(bus);
    }

    // INFO: fc 12nov26 not in the constructor: a global's runs before the watchdog and the wake
    // call
    void probe() {
        probed_ = true;
        // INFO: fc 12nov26 a version read back proves the part, BUSY falling only a pull-down
        if (radio_.probe() == Status::Ok) capabilities_ |= ports::Capability::AdsbReceiver;
        establish_haptic();
        establish_barometer();
    }

    bool probed() const { return probed_; }

    // TODO: fc 12nov26 NRESET held low through SYSTEM OFF, its current unmeasured until #169
    void hold_radio_in_reset() { radio_.hold_in_reset(); }

    // INFO: fc 12nov26 with BUSY stuck the receiver's begin() blocks 7 s, inside setup's 9 s rope
    Status begin() {
        const Status status = platform_.begin();
        if (status != Status::Ok) return status;
        if (ports::has(capabilities_, ports::Capability::Barometer)) (void)barometer_.begin();
        if (!ports::has(capabilities_, ports::Capability::AdsbReceiver)) return Status::Ok;
        return receiver_.begin();
    }

    // INFO: fc 12nov26 SoftRF puts the DRV2605 in standby before its rail drops,
    // nRF52.cpp:3007-3020
    void park() { haptic_.park(); }

    ports::Capabilities capabilities() const { return capabilities_; }
    uint32_t barometer_faults() const { return barometer_faults_; }

    ports::Roles roles() {
        return ports::Roles{
            platform_.clock(),
            null_.rf,
            ports::has(capabilities_, ports::Capability::Link)
                ? static_cast<ports::Link&>(platform_.link())
                : null_.link,
            null_.display,
            ports::has(capabilities_, ports::Capability::Storage)
                ? static_cast<ports::KeyValueStore&>(platform_.key_value())
                : null_.key_value,
            ports::has(capabilities_, ports::Capability::Storage)
                ? static_cast<ports::FlashRegion&>(platform_.log_flash())
                : null_.log_flash,
            // INFO: fc 12nov26 taken before the probes, so the annunciator is always the platform's
            platform_.annunciator(),
            ports::has(capabilities_, ports::Capability::Dfu)
                ? static_cast<ports::Dfu&>(platform_.dfu())
                : null_.dfu,
            ports::has(capabilities_, ports::Capability::DieTemperature)
                ? static_cast<ports::DieTemperature&>(platform_.die_temperature())
                : null_.die_temperature,
            ports::has(capabilities_, ports::Capability::Indicator)
                ? static_cast<ports::Indicator&>(platform_.indicator())
                : null_.indicator,
            ports::has(capabilities_, ports::Capability::Gnss) ? static_cast<ports::Gnss&>(gnss_)
                                                               : null_.gnss,
            platform_.identity(),
            platform_.random(),
            capabilities_,
        };
    }

    void poll(bus::State& state, uint32_t now_ms) {
        receiver_.service(now_ms);
        // INFO: fc 12nov26 serves the host annunciator's guards: a test catches an output left on
        platform_.annunciator().service(now_ms);

        if (ports::has(capabilities_, ports::Capability::Gnss)) {
            gnss_.service(now_ms);
            if (gnss_.poll(now_ms)) {
                bus_.gnss.push(gnss_.solution());
                state.gnss.sky = gnss_.sky();
                state.gnss.reject = gnss_.last_reject();
                state.gnss.rejected = gnss_.rejected();
            }
        }

        poll_battery(now_ms);
        button_active_ = producer_.publish_contact(events::Contact::Button, button_,
                                                   platform_.button_down(), now_ms);
        producer_.publish_clock(state);
        poll_barometer(state.clock, now_ms);
    }

    void poll_battery(uint32_t now_ms) { producer_.poll_battery(capabilities_, now_ms); }

    // INFO: fc 19nov26 the loop sleeps until the earliest of these, or until an edge wakes it
    uint32_t due_in_ms(const timing::ClockState& clock, uint32_t now_ms) const {
        const bool gnss_bringing_up = ports::has(capabilities_, ports::Capability::Gnss) &&
                                      !gnss_.configured() && !gnss_.degraded();
        if (button_active_ || gnss_bringing_up || barometer_converting_)
            return runtime::kServicePassCeilingMs;
        const uint32_t barometer_ms =
            ports::has(capabilities_, ports::Capability::Barometer)
                ? boards::barometer_due_in_ms(clock, now_ms - last_barometer_ms_)
                : runtime::kServiceRestCeilingMs;
        return std::min(producer_.battery_due_in_ms(capabilities_, now_ms), barometer_ms);
    }

    typename Platform::AdsbReceiver& receiver() { return receiver_; }
    const typename Platform::AdsbReceiver& receiver() const { return receiver_; }
    parts::Lr2021& radio() { return radio_; }
    parts::Ag3335& gnss() { return gnss_; }
    parts::Spa06& barometer() { return barometer_; }
    parts::Drv2605& haptic() { return haptic_; }

   private:
    // INFO: fc 12nov26 the SPA06 has no filter, so one conversion a second on the PPS edge
    void poll_barometer(const timing::ClockState& clock, uint32_t now_ms) {
        if (!ports::has(capabilities_, ports::Capability::Barometer)) return;
        if (barometer_converting_) {
            collect_barometer(now_ms);
            return;
        }
        if (!barometer_due(clock, now_ms - last_barometer_ms_)) return;
        last_barometer_ms_ = now_ms;
        barometer_triggered_ms_ = now_ms;
        barometer_converting_ = barometer_.trigger();
        if (!barometer_converting_) barometer_faults_++;
    }

    void collect_barometer(uint32_t now_ms) {
        parts::Spa06::Reading reading{};
        if (barometer_.read(reading)) {
            barometer_converting_ = false;
            bus_.barometer.push(events::BarometerSample{
                reading.pressure_mpa, barometer_triggered_ms_ + parts::Spa06::kConversionMidpointMs,
                reading.temperature_dc, true});
            return;
        }
        if (now_ms - barometer_triggered_ms_ < kBarometerConversionCeilingMs) return;
        barometer_converting_ = false;
        barometer_faults_++;
    }

    void establish_haptic() {
        capabilities_ = ports::without(capabilities_, ports::Capability::Haptic);
        if (haptic_.begin() != Status::Ok) return;
        platform_.annunciator().attach_haptic(haptic_);
        capabilities_ |= ports::Capability::Haptic | ports::Capability::HapticDriver;
    }

    void establish_barometer() {
        capabilities_ = ports::without(capabilities_, ports::Capability::Barometer);
        if (!barometer_.answers()) return;
        capabilities_ |= ports::Capability::Barometer;
    }

    Platform& platform_;
    bus::Bus& bus_;
    parts::Lr2021 radio_;
    typename Platform::AdsbReceiver receiver_;
    Gnss gnss_;
    parts::Spa06 barometer_;
    parts::Drv2605 haptic_;
    input::Contact button_{meshtracker_x1::kButtonHoldMs};
    ports::NullRoles null_{};
    Producer<Platform> producer_;
    bool button_active_{false};
    ports::Capabilities capabilities_;
    bool probed_{false};
    uint32_t last_barometer_ms_{0};
    uint32_t barometer_triggered_ms_{0};
    uint32_t barometer_faults_{0};
    bool barometer_converting_{false};
};

}  // namespace skyblip::boards

#endif
