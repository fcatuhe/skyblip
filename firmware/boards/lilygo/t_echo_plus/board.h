#ifndef SKYBLIP_BOARDS_T_ECHO_PLUS_BOARD_H
#define SKYBLIP_BOARDS_T_ECHO_PLUS_BOARD_H

#include "boards/lilygo/t_echo_plus/glass.h"
#include "boards/lilygo/t_echo_plus/i2c_scan.h"
#include "boards/lilygo/t_echo_plus/imu_mount.h"
#include "boards/lilygo/t_echo_plus/pins.h"
#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/events/input.h"
#include "core/events/link.h"
#include "core/events/sensor.h"
#include "core/input/contact.h"
#include "core/timing/transmit.h"
#include "hardware/parts/bhi260/bhi260.h"
#include "hardware/parts/drv2605/drv2605.h"
#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "hardware/platform/contract.h"
#include "ports/inventory.h"
#include "ports/null.h"
#include "ports/roles.h"
#include "runtime/tasks.h"

namespace skyblip::boards {

static_assert(t_echo_plus::kGlassW == parts::Ssd1681::kGlassW &&
                  t_echo_plus::kGlassH == parts::Ssd1681::kGlassH,
              "the glass on this board is what its controller drives");

// The T-Echo Plus, assembled once. P is the platform: silicon or host. Swapping
// it changes which io/ backend the parts talk to and nothing else, so there is
// no second copy of this wiring to keep in step.
template <class P>
class TEchoPlus {
   public:
    TEchoPlus(P& platform, bus::Bus& bus)
        : platform_(platform),
          bus_(bus),
          radio_(platform.spi(io::BusId::Radio), platform.gpio(), platform.delay(),
                 t_echo_plus::kRadioBusy, t_echo_plus::kRadioRst, t_echo_plus::kRadioDio1),
          epd_(platform.spi(io::BusId::Epd), platform.gpio(), platform.delay(), t_echo_plus::kEpdDc,
               t_echo_plus::kEpdRst, t_echo_plus::kEpdBusy, t_echo_plus::kEpdBacklight,
               platform.glass_rotation()),
          gnss_(platform.uart(io::BusId::Gnss), platform.uart_rate(io::BusId::Gnss)),
          haptic_(platform.i2c(io::BusId::Sensor), platform.gpio(), t_echo_plus::kHapticEnable),
          imu_(platform.i2c(io::BusId::Sensor)),
          rf_(radio_, platform.clock(), bus.rf),
          capabilities_(platform.capabilities()) {
        platform_.wire(t_echo_plus::kPinMap);
        // The radio is the one part on this board with no bus enumeration of its
        // own, so presence is a register round-trip. Asserting it instead makes a
        // dead MISO look like a healthy radio that never hears anything. It
        // belongs here rather than in begin() because capabilities() has to be
        // final before roles() is taken, and the round-trip needs nothing the
        // rails have not already provided: DS 13.1.1 puts the part in STDBY_RC on
        // reset, and a register write and read back run on that internal clock.
        if (radio_.probe() == Status::Ok) capabilities_ = capabilities_ | ports::Capability::Rf;

        take_bus_inventory();
        identify_panel();
        establish_haptic();
        establish_buzzer();
        establish_inclinometer();
    }

    // Probing is done: capabilities() is already known. This is bring-up, and a part that
    // is present but refuses to start is a degraded state, not a missing
    // capability.
    Status begin() {
        const Status s = platform_.begin();
        if (s != Status::Ok) return s;
        if (ports::has(capabilities_, ports::Capability::Display)) epd_.begin();
        if (!ports::has(capabilities_, ports::Capability::Rf)) return Status::Ok;
        return rf_.begin();
    }

    // The way down, for the parts the board owns rather than the platform: the
    // haptic driver goes to standby and lets go of its enable line before the
    // rail it hangs off collapses. SoftRF does the same two things in the same
    // order (platform/nRF52.cpp:2899-2912). Called from the shutdown sequence,
    // after the services have stopped running.
    void park() { haptic_.park(); }

    ports::Capabilities capabilities() const { return capabilities_; }

    // What answered, by name and address, behind the capability bits. The
    // self-test page reads it: two BME280 addresses, five shipped e-paper
    // signatures and a haptic that may or may not be a waveform driver are
    // exactly the facts a bench cannot get from a PASS.
    const ports::Inventory& inventory() const { return inventory_; }

    ports::Roles roles() {
        return ports::Roles{
            platform_.clock(),
            ports::has(capabilities_, ports::Capability::Rf) ? static_cast<ports::Rf&>(rf_)
                                                             : null_.rf,
            ports::has(capabilities_, ports::Capability::Link)
                ? static_cast<ports::Link&>(platform_.link())
                : null_.link,
            ports::has(capabilities_, ports::Capability::Display)
                ? static_cast<ports::Display&>(epd_)
                : null_.display,
            ports::has(capabilities_, ports::Capability::Storage)
                ? static_cast<ports::KvStore&>(platform_.kv())
                : null_.kv,
            ports::has(capabilities_, ports::Capability::Storage)
                ? static_cast<ports::FlashRegion&>(platform_.log_flash())
                : null_.log_flash,
            // Buzzer OR haptic: they are one role and two parts, and a board with
            // a dead buzzer pin still owes a pilot the pulse it can make.
            ports::has(capabilities_, ports::Capability::Buzzer) ||
                    ports::has(capabilities_, ports::Capability::Haptic)
                ? static_cast<ports::Annunciator&>(platform_.annunciator())
                : null_.annunciator,
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
            capabilities_,
            platform_.device_addr(),
        };
    }

    void poll_baro_on_pps(const timing::ClockState& clock, uint32_t now_ms) {
        if (!ports::has(capabilities_, ports::Capability::Baro)) return;
        if (!baro_due(clock, now_ms)) return;
        last_baro_ms_ = now_ms;
        platform::BaroReading reading{};
        if (platform_.read_baro(reading))
            bus_.baro.push(events::BaroSample{reading.pressure_mpa, now_ms,
                                              reading.temperature_decicelsius,
                                              reading.temperature_valid});
    }

    bool baro_due(const timing::ClockState& clock, uint32_t now_ms) const {
        const uint32_t since = now_ms - last_baro_ms_;
        if (since < runtime::kBaroPeriodMs / 2) return false;
        if (!clock.pps_locked) return since >= runtime::kBaroPeriodMs;
        return clock.ms_since_pps < runtime::kBaroPpsWindowMs ||
               since >= 2 * runtime::kBaroPeriodMs;
    }

    void publish_contact(events::Contact which, input::Contact& contact, bool level,
                         uint32_t now_ms) {
        const input::Contact::Edge edge = contact.update(level, now_ms);
        if (edge == input::Contact::Edge::None) return;
        bus_.input.push(
            events::ContactEvent{which, edge == input::Contact::Edge::Down, contact.edge_ms()});
    }

    void poll_battery(uint32_t now_ms) {
        if (!ports::has(capabilities_, ports::Capability::Battery)) return;
        if (now_ms - last_battery_ms_ < runtime::kBatteryPeriodMs) return;
        last_battery_ms_ = now_ms;
        uint16_t millivolts = 0;
        if (platform_.read_battery_mv(millivolts))
            bus_.battery.push(events::BatterySample{millivolts, platform_.external_power()});
    }

    // The producer side: everything hardware says arrives on the bus, and the
    // clock's PPS phase is refreshed. Identical on both platforms.
    void poll(bus::State& state, uint32_t now_ms) {
        rf_.service(now_ms);
        // A haptic pulse ends on a deadline the adapter owns. On silicon that is a
        // work item the kernel runs; on the host it is this call, so a motor left
        // running is a bug a host test can catch.
        platform_.annunciator().service(now_ms);

        // The connection before the bytes: a frame from a session whose Up is
        // still queued behind it would be answered by a service that does not yet
        // believe there is anyone there.
        events::LinkEvent link_event;
        while (platform_.link().pop_event(link_event)) bus_.link_events.push(link_event);

        events::RxFrame frame;
        while (platform_.link().pop_rx(frame)) {
            if (frame.endpoint == events::Endpoint::Log)
                bus_.log_rx.push(frame);
            else
                bus_.link_rx.push(frame);
        }

        if (ports::has(capabilities_, ports::Capability::Gnss)) {
            gnss_.request_satellites_in_view(state.gnss.levels_wanted);
            gnss_.service(now_ms);
            if (gnss_.poll()) {
                bus_.gnss.push(gnss_.solution());
                state.gnss.sky = gnss_.sky();
                state.gnss.reject = gnss_.reject_reason();
                state.gnss.rejected = gnss_.rejected();
            }
            state.gnss.levels_live = gnss_.satellites_in_view_live();
        }

        poll_battery(now_ms);
        poll_inclinometer(state, now_ms);

        publish_contact(events::Contact::Button, button_, platform_.button_down(), now_ms);
        publish_contact(events::Contact::Pad, pad_, platform_.pad_down(), now_ms);

        const uint64_t now_us = platform_.clock().micros();
        state.clock.pps_locked = platform_.pps().locked();
        state.clock.ms_since_pps = platform_.pps().ms_since(now_us);
        poll_baro_on_pps(state.clock, now_ms);
        // The edge itself, as the surface latched it: whoever reads it later
        // reads an instant that has not gone stale in the meantime. Rebuilding
        // it from the millisecond phase threw away up to a millisecond of the
        // 5 ms jitter guard before the plan was even armed.
        timing::carry_utc_to_edge(state.clock, platform_.pps().last_edge_us());
        // The one place the PPS edge is owned: the bench's interval-error
        // histogram is fed here rather than by a second reader of the pin.
        state.rf.timing_stats.record_edge(state.clock.pps_edge_us, state.clock.pps_locked);
    }

    typename P::Rf& rf() { return rf_; }
    parts::L76k& gnss() { return gnss_; }
    parts::Bhi260& imu() { return imu_; }
    parts::Ssd1681& display() { return epd_; }
    parts::Drv2605& haptic() { return haptic_; }

   private:
    // ports::missing(a, b) is "b without a", which is what this board needs when a
    // probe contradicts what the platform declared.
    ports::Capabilities without(ports::Capability bit) const {
        return ports::missing(bit, capabilities_);
    }

    // The fingerprint is taken where the pins are free, which on silicon is the
    // board port at PRE_KERNEL_1 and on the host is the panel model. Either way
    // the driver is told once, before begin(), and an unread fingerprint leaves
    // the panel Unknown - which is the safest refresh policy and, until somebody
    // reads a Plus on a bench, the honest answer for this board.
    void identify_panel() {
        parts::PanelSignature signature{};
        platform_.read_panel_signature(signature);
        epd_.adopt(signature);
        inventory_.panel = epd_.panel_name();
    }

    void take_bus_inventory() {
        // The enable line first: it costs one register write, and if it turns out
        // to gate the driver's supply rather than its standby, a scan taken before
        // it would miss the part entirely.
        haptic_.power_up();
        inventory_ = t_echo_plus::scan_i2c(platform_.i2c(io::BusId::Sensor));
        inventory_.baro_address = inventory_.has_i2c_address(t_echo_plus::kBaroAddrPrimary)
                                      ? t_echo_plus::kBaroAddrPrimary
                                  : inventory_.has_i2c_address(t_echo_plus::kBaroAddrAlternate)
                                      ? t_echo_plus::kBaroAddrAlternate
                                      : 0;
    }

    // The haptic is a part on a bus, not a pin. Capability::Haptic is granted by
    // the part answering and identifying itself, never by the board being a Plus:
    // P0.08 high with no DRV2605 behind it is a device that claims a vibration
    // motor and cannot vibrate, which is what the alarm's escalation to haptics
    // was until this probe existed.
    //
    // INFO: fc 06aug26 NOBODY HAS SEEN 0x5A ANSWER ON OUR OWN HARDWARE. The
    // address, the identification and the whole pulse sequence come from SoftRF,
    // which identifies the T-Echo Plus by exactly this probe
    // (src/platform/nRF52.cpp:1158-1163, driver at 2112-2133). If a bench unit
    // does not answer, this reports the haptic absent and the alarm loses it
    // honestly, which is still better than the claim that preceded it.
    void establish_haptic() {
        capabilities_ = without(ports::Capability::Haptic);
        inventory_.haptic = ports::HapticKind::None;
        if (!inventory_.has_i2c_address(t_echo_plus::kHapticDriverAddress)) return;
        if (haptic_.begin() != Status::Ok) return;
        platform_.annunciator().attach_haptic(haptic_);
        inventory_.haptic = ports::HapticKind::WaveformDriver;
        capabilities_ = capabilities_ | ports::Capability::Haptic | ports::Capability::HapticDriver;
    }

    // INFO: fc 18sep26 a fitted piezo stage holds P0.06 down, an empty pad follows the pull-up up
    void establish_buzzer() {
        capabilities_ = platform_.buzzer_pin_held_low() ? capabilities_ | ports::Capability::Buzzer
                                                        : without(ports::Capability::Buzzer);
    }

    void establish_inclinometer() {
        capabilities_ = without(ports::Capability::Inclinometer);
        if (!inventory_.has_i2c_address(t_echo_plus::kImuAddress) &&
            !inventory_.has_i2c_address(t_echo_plus::kImuAddressAlternate))
            return;
        if (imu_.probe() != Status::Ok) return;
        capabilities_ = capabilities_ | ports::Capability::Inclinometer;
    }

    void poll_inclinometer(bus::State& state, uint32_t now_ms) {
        state.imu.stage = imu_.stage_text();
        state.imu.fault = imu_.fault_text();
        state.imu.fifo_bytes = imu_.fifo_bytes();
        state.imu.unparsed = imu_.unparsed_events();
        state.imu.error = imu_.hub_error();
        state.imu.interrupt = imu_.interrupt_status();
        state.imu.meta = imu_.meta_event();
        state.imu.sensor_error = imu_.sensor_error();
        state.imu.errored_sensor = imu_.errored_sensor();
        if (!ports::has(capabilities_, ports::Capability::Inclinometer)) return;
        if (imu_.stage() == parts::Bhi260::Stage::Idle) imu_.load(platform_.imu_firmware(), now_ms);
        imu_.service(now_ms);
        if (imu_.poll()) bus_.accel.push(t_echo_plus::device_frame(imu_.acceleration(), now_ms));
    }

    P& platform_;
    bus::Bus& bus_;
    parts::Sx1262 radio_;
    parts::Ssd1681 epd_;
    parts::L76k gnss_;
    parts::Drv2605 haptic_;
    parts::Bhi260 imu_;
    typename P::Rf rf_;
    ports::Inventory inventory_{};
    input::Contact button_{t_echo_plus::kButtonDebounceMs};
    input::Contact pad_{t_echo_plus::kPadSettleMs};
    ports::NullRoles null_{};
    ports::Capabilities capabilities_;
    uint32_t last_baro_ms_{0};
    uint32_t last_battery_ms_{0};
};

}  // namespace skyblip::boards

#endif
