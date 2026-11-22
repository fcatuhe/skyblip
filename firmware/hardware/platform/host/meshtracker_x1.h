#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_MESHTRACKER_X1_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_MESHTRACKER_X1_H

#include "hardware/parts/ag3335/model.h"
#include "hardware/parts/drv2605/model.h"
#include "hardware/parts/lr2021/model.h"
#include "hardware/parts/spa06/model.h"
#include "hardware/platform/host/adsb_receiver.h"
#include "hardware/platform/host/platform.h"
#include "ports/null.h"

namespace skyblip::platform::host {

// INFO: fc 12nov26 the X1's two driven parts on lines: the radio's three, and the haptic enable
class MeshtrackerX1Gpio : public io::Gpio {
   public:
    MeshtrackerX1Gpio(models::Lr2021& radio, models::Drv2605& haptic)
        : radio_(radio), haptic_(haptic) {}

    void set(int pin, bool level) override {
        if (io::Gpio* owner = route(pin)) owner->set(pin, level);
    }
    bool get(int pin) override {
        io::Gpio* owner = route(pin);
        return owner != nullptr && owner->get(pin);
    }
    void mode_output(int pin) override {
        if (io::Gpio* owner = route(pin)) owner->mode_output(pin);
    }
    void mode_input(int pin, bool pullup) override {
        if (io::Gpio* owner = route(pin)) owner->mode_input(pin, pullup);
    }

    void wire(const io::PinMap& map) {
        for (int i = 0; i < map.count; i++) {
            const io::PinRole& role = map.roles[i];
            switch (role.function) {
                case io::PinFunction::RadioBusy: radio_.busy_pin = role.pin; break;
                case io::PinFunction::RadioReset: radio_.reset_pin = role.pin; break;
                case io::PinFunction::RadioIrq: radio_.irq_pin = role.pin; break;
                case io::PinFunction::HapticEnable: haptic_.enable_pin = role.pin; break;
                default: break;
            }
        }
    }

   private:
    io::Gpio* route(int pin) {
        if (pin == radio_.busy_pin || pin == radio_.reset_pin || pin == radio_.irq_pin)
            return &radio_;
        if (pin == haptic_.enable_pin) return &haptic_;
        return nullptr;
    }

    models::Lr2021& radio_;
    models::Drv2605& haptic_;
};

class MeshtrackerX1 {
   public:
    using AdsbReceiver = host::AdsbReceiver;

    static constexpr ports::Capabilities kFullyFitted =
        ports::Capability::AdsbReceiver | ports::Capability::Gnss | ports::Capability::Barometer |
        ports::Capability::Link | ports::Capability::Storage | ports::Capability::Dfu |
        ports::Capability::Buzzer | ports::Capability::Haptic | ports::Capability::Contacts |
        ports::Capability::Battery | ports::Capability::Indicator;

    explicit MeshtrackerX1(ports::Capabilities fitted = kFullyFitted) : fitted_(fitted) {
        radio_.miso_dead = !ports::has(fitted, ports::Capability::AdsbReceiver);
        barometer_.attach_clock(clock_);
        barometer_.answers = ports::has(fitted, ports::Capability::Barometer);
        haptic_.answers = ports::has(fitted, ports::Capability::Haptic);
        battery_.fitted = ports::has(fitted, ports::Capability::Battery);
        log_flash_.set_fitted(ports::has(fitted, ports::Capability::Storage));
        sensor_bus_.attach(models::Spa06::kAddress, barometer_);
        sensor_bus_.attach(models::Drv2605::kAddress, haptic_);
    }

    // INFO: fc 12nov26 the reset causes, the button and the cell need no bring-up on the host
    void begin_wake() {}

    Status begin() {
        begins++;
        return begin_result;
    }

    io::Spi& spi(io::BusId) { return radio_; }
    io::Uart& uart(io::BusId) { return gnss_; }
    io::I2c& i2c(io::BusId) { return sensor_bus_; }
    io::Gpio& gpio() { return gpio_; }
    io::Delay& delay() { return radio_; }
    void wire(const io::PinMap& map) { gpio_.wire(map); }
    int radio_irq() const { return radio_.irq_pin; }

    host::Clock& clock() { return clock_; }
    host::Link& link() { return link_; }
    host::KeyValueStore& key_value() { return key_value_; }
    host::FlashRegion& log_flash() { return log_flash_; }
    host::Annunciator& annunciator() { return annunciator_; }
    host::Indicator& indicator() { return indicator_; }
    host::Dfu& dfu() { return dfu_; }
    host::DieTemperature& die_temperature() { return die_temperature_; }
    host::Pps& pps() { return pps_; }
    host::Watchdog& watchdog() { return watchdog_; }
    host::SystemPower& system_power() { return system_power_; }
    ports::KeylessIdentity& identity() { return identity_; }
    host::Random& random() { return random_; }
    host::Battery& battery() { return battery_; }

    models::Lr2021& radio() { return radio_; }
    models::Ag3335& gnss() { return gnss_; }
    models::Spa06& barometer() { return barometer_; }
    models::Drv2605& haptic() { return haptic_; }

    bool button_down() const { return button_down_; }
    void set_button_down(bool down) { button_down_ = down; }
    bool read_divider_mv(uint16_t& out_mv) { return battery_.read_mv(out_mv); }
    bool external_power() const { return battery_.external_power; }

    // INFO: fc 12nov26 the board probes these three itself, as it does on silicon
    Status begin_result{Status::Ok};
    int begins{0};

    ports::Capabilities capabilities() const {
        return ports::without(fitted_, ports::Capability::AdsbReceiver |
                                           ports::Capability::Barometer |
                                           ports::Capability::Haptic);
    }

   private:
    ports::Capabilities fitted_;
    host::Clock clock_{};
    models::Lr2021 radio_{};
    models::Ag3335 gnss_{};
    models::Spa06 barometer_{};
    models::Drv2605 haptic_{};
    I2cBus sensor_bus_{};
    MeshtrackerX1Gpio gpio_{radio_, haptic_};
    host::Link link_{};
    host::KeyValueStore key_value_{};
    host::FlashRegion log_flash_{};
    host::Annunciator annunciator_{};
    host::Indicator indicator_{};
    host::Watchdog watchdog_{};
    host::Dfu dfu_{watchdog_};
    host::DieTemperature die_temperature_{};
    host::Pps pps_{clock_};
    host::SystemPower system_power_{};
    host::Battery battery_{};
    ports::KeylessIdentity identity_{host::Identity::kDeviceId, host::Identity::kBluetoothTag};
    host::Random random_{};
    bool button_down_{false};
};

}  // namespace skyblip::platform::host

#endif
