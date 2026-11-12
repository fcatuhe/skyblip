#ifndef SKYBLIP_BOARDS_MESHTRACKER_X1_BOARD_H
#define SKYBLIP_BOARDS_MESHTRACKER_X1_BOARD_H

#include "boards/seeed/meshtracker_x1/pins.h"
#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/events/input.h"
#include "core/events/link.h"
#include "core/events/sensor.h"
#include "core/input/contact.h"
#include "core/timing/slot.h"
#include "hardware/io/i2c_scan.h"
#include "hardware/parts/ag3335/ag3335.h"
#include "hardware/parts/drv2605/drv2605.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "hardware/parts/spa06/spa06.h"
#include "hardware/parts/ysn8900/ysn8900.h"
#include "ports/adsb_receiver.h"
#include "ports/inventory.h"
#include "ports/null.h"
#include "ports/roles.h"
#include "runtime/tasks.h"

namespace skyblip::boards {

static_assert(runtime::kBarometerConversionCeilingMs < runtime::kBarometerPublishPeriodMs,
              "a SPA06 conversion is read or given up before the next one is triggered");

template <class Platform>
class MeshtrackerX1 {
   public:
    MeshtrackerX1(Platform& platform, bus::Bus& bus, bus::AdsbQueue& adsb)
        : platform_(platform),
          bus_(bus),
          radio_(platform.spi(io::BusId::Radio), platform.gpio(), platform.delay(),
                 meshtracker_x1::kRadioBusy, meshtracker_x1::kRadioReset, meshtracker_x1::kRadioIrq,
                 meshtracker_x1::kRadioIrqDio),
          receiver_(radio_, platform.clock(), platform.radio_irq(), adsb),
          gnss_(platform.uart(io::BusId::Gnss)),
          barometer_(platform.i2c(io::BusId::Sensor)),
          haptic_(platform.i2c(io::BusId::Sensor), platform.gpio(), platform.delay(),
                  meshtracker_x1::kHapticEnable, meshtracker_x1::kHapticActuator),
          rtc_(platform.i2c(io::BusId::Rtc)),
          capabilities_(platform.capabilities()) {}

    // INFO: fc 12nov26 not in the constructor: a global's runs before the watchdog and the wake
    // call
    void probe() {
        probed_ = true;
        // INFO: fc 12nov26 a version read back proves the part, BUSY falling only a pull-down
        if (radio_.probe() == Status::Ok) capabilities_ |= ports::Capability::AdsbReceiver;
        take_bus_inventory();
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
        inventory_.rtc_fitted = rtc_.answers();
        if (ports::has(capabilities_, ports::Capability::Barometer)) (void)barometer_.begin();
        if (!ports::has(capabilities_, ports::Capability::AdsbReceiver)) return Status::Ok;
        return receiver_.begin();
    }

    // INFO: fc 12nov26 SoftRF puts the DRV2605 in standby before its rail drops,
    // nRF52.cpp:3007-3020
    void park() { haptic_.park(); }

    ports::Capabilities capabilities() const { return capabilities_; }
    const ports::Inventory& inventory() const { return inventory_; }
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
            capabilities_,
        };
    }

    void poll(bus::State& state, uint32_t now_ms) {
        receiver_.service(now_ms);
        // INFO: fc 12nov26 serves the host annunciator's guards: a test catches an output left on
        platform_.annunciator().service(now_ms);

        events::LinkEvent link_event;
        while (platform_.link().pop_event(link_event)) bus_.link_events.push(link_event);
        events::RxFrame frame;
        while (platform_.link().pop_rx(frame)) {
            if (frame.endpoint == events::Endpoint::Log)
                bus_.log_rx.push(frame);
            else if (frame.endpoint == events::Endpoint::Peer)
                bus_.peer_rx.push(frame);
            else
                bus_.link_rx.push(frame);
        }

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
        publish_contact(platform_.button_down(), now_ms);

        const uint64_t now_us = platform_.clock().microseconds();
        state.clock.pps_locked = platform_.pps().locked();
        state.clock.since_edge_ms = platform_.pps().since_edge_ms(now_us);
        poll_barometer(state.clock, now_ms);
        timing::carry_utc_to_edge(state.clock, platform_.pps().last_edge_us());
        state.rf.timing_statistics.record_edge(state.clock.pps_edge_us, state.clock.pps_locked);
    }

    void poll_battery(uint32_t now_ms) {
        if (!ports::has(capabilities_, ports::Capability::Battery)) return;
        if (now_ms - last_battery_ms_ < runtime::kBatteryPeriodMs) return;
        last_battery_ms_ = now_ms;
        uint16_t divider_mv = 0;
        // INFO: fc 12nov26 refused or not, the reading and VBUS go out: core/power judges it
        platform_.read_battery_mv(divider_mv);
        bus_.battery.push(events::BatterySample{divider_mv, platform_.external_power()});
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
        if (!barometer_due(clock, now_ms)) return;
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
        if (now_ms - barometer_triggered_ms_ < runtime::kBarometerConversionCeilingMs) return;
        barometer_converting_ = false;
        barometer_faults_++;
    }

    bool barometer_due(const timing::ClockState& clock, uint32_t now_ms) const {
        const uint32_t since_published_ms = now_ms - last_barometer_ms_;
        if (since_published_ms < runtime::kBarometerPublishPeriodMs / 2) return false;
        if (!clock.pps_locked) return since_published_ms >= runtime::kBarometerPublishPeriodMs;
        return clock.since_edge_ms < runtime::kBarometerPpsWindowMs ||
               since_published_ms >= 2 * runtime::kBarometerPublishPeriodMs;
    }

    void publish_contact(bool level, uint32_t now_ms) {
        const input::Contact::Edge edge = button_.update(level, now_ms);
        if (edge == input::Contact::Edge::None) return;
        bus_.input.push(events::ContactEvent{
            events::Contact::Button, edge == input::Contact::Edge::Down, button_.edge_ms()});
    }

    void take_bus_inventory() {
        haptic_.power_up();
        io::scan_i2c(platform_.i2c(io::BusId::Sensor), inventory_);
    }

    void establish_haptic() {
        capabilities_ = ports::without(capabilities_, ports::Capability::Haptic);
        inventory_.haptic = ports::HapticKind::None;
        if (!inventory_.has_i2c_address(parts::Drv2605::kAddress)) return;
        if (haptic_.begin() != Status::Ok) return;
        platform_.annunciator().attach_haptic(haptic_);
        inventory_.haptic = ports::HapticKind::WaveformDriver;
        capabilities_ |= ports::Capability::Haptic | ports::Capability::HapticDriver;
    }

    void establish_barometer() {
        capabilities_ = ports::without(capabilities_, ports::Capability::Barometer);
        if (!barometer_.answers()) return;
        capabilities_ |= ports::Capability::Barometer;
        inventory_.barometer_address = inventory_.has_i2c_address(parts::Spa06::kAddressPrimary)
                                           ? parts::Spa06::kAddressPrimary
                                           : parts::Spa06::kAddressAlternate;
    }

    Platform& platform_;
    bus::Bus& bus_;
    parts::Lr2021 radio_;
    typename Platform::AdsbReceiver receiver_;
    parts::Ag3335 gnss_;
    parts::Spa06 barometer_;
    parts::Drv2605 haptic_;
    parts::Ysn8900 rtc_;
    ports::Inventory inventory_{};
    input::Contact button_{meshtracker_x1::kButtonHoldMs};
    ports::NullRoles null_{};
    ports::Capabilities capabilities_;
    bool probed_{false};
    uint32_t last_barometer_ms_{0};
    uint32_t barometer_triggered_ms_{0};
    uint32_t barometer_faults_{0};
    bool barometer_converting_{false};
    uint32_t last_battery_ms_{0};
};

}  // namespace skyblip::boards

#endif
