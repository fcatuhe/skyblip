#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_PLATFORM_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_PLATFORM_H

#include "core/util/span.h"
#include "hardware/parts/bme280/model.h"
#include "hardware/parts/ssd1681/panel.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "hardware/platform/contract.h"
#include "hardware/platform/host/annunciator.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/die_temperature.h"
#include "hardware/platform/host/flash_region.h"
#include "hardware/platform/host/indicator.h"
#include "hardware/platform/host/io.h"
#include "hardware/platform/host/kvstore.h"
#include "hardware/platform/host/link.h"
#include "hardware/platform/host/rf.h"
#include "hardware/platform/host/system_power.h"
#include "hardware/platform/host/watchdog.h"
#include "ports/capabilities.h"
#include "ports/dfu.h"
#include "ports/die_temperature.h"

namespace skyblip::platform::host {

class Dfu : public ports::Dfu {
   public:
    void trigger() override { triggered++; }
    bool confirm() override {
        confirms++;
        if (confirm_fails) return false;
        image_confirmed = true;
        return true;
    }
    bool confirmed() override { return image_confirmed; }
    bool running_version(ports::ImageVersion& out) override {
        if (!has_running) return false;
        out = running;
        return true;
    }
    bool staged_version(ports::ImageVersion& out) override {
        if (!has_staged) return false;
        out = staged;
        return true;
    }
    ports::RecoveryPath enter_recovery() override {
        recoveries++;
        return recovery_path;
    }
    ports::RecoveryPath recovery_path{ports::RecoveryPath::Rebooted};

    int triggered{0};
    int confirms{0};
    int recoveries{0};
    bool image_confirmed{true};
    bool confirm_fails{false};
    bool has_running{false};
    bool has_staged{false};
    ports::ImageVersion running{};
    ports::ImageVersion staged{};
};

class Baro {
   public:
    bool ready() const { return present; }
    bool read_baro(BaroReading& out) const {
        if (!present) return false;
        out.pressure_mpa = chip.pressure_mpa();
        out.temperature_decicelsius = chip.temperature_decicelsius();
        out.temperature_valid = true;
        return true;
    }

    models::Bme280 chip;
    bool present{true};
};

// The cell the world charges and drains. Millivolts is the only thing the board
// can read on silicon, so it is the only thing settable here.
class Battery {
   public:
    bool ready() const { return present; }
    bool read_mv(uint16_t& out_mv) const {
        if (!present) return false;
        out_mv = millivolts;
        return true;
    }

    bool present{true};
    uint16_t millivolts{4050};
    bool external_power{false};
};

// The same PPS surface the silicon platform offers: a phase, and the instant the
// edge itself arrived. The modelled receiver pulses on the whole second of the
// clock the caller advances, so the edge is that second - not the phase rounded
// to the millisecond whoever asked happened to ask on.
class Pps {
   public:
    explicit Pps(const Clock& clock) : clock_(clock) {}

    bool locked() const { return locked_; }
    void set_locked(bool on) { locked_ = on; }
    uint32_t ms_since(uint64_t now_us) const {
        return locked_ ? static_cast<uint32_t>(now_us / 1000 % 1000) : 0;
    }

    uint64_t last_edge_us() const {
        if (!locked_) return 0;
        const uint64_t now_us = clock_.micros();
        return now_us - now_us % kSecondUs;
    }

   private:
    static constexpr uint64_t kSecondUs = 1000000;

    const Clock& clock_;
    bool locked_{true};
};

// The host platform: the same role surface the silicon platform offers, backed
// by part models and a clock the caller advances. A board cannot tell them apart.
class Platform {
   public:
    using Rf = host::Rf;
    using Link = host::Link;

    static constexpr ports::Capabilities kFullyFitted =
        ports::Capability::Display | ports::Capability::Gnss | ports::Capability::Baro |
        ports::Capability::Link | ports::Capability::Storage | ports::Capability::Dfu |
        ports::Capability::Buzzer | ports::Capability::Haptic | ports::Capability::Contacts |
        ports::Capability::Battery | ports::Capability::Indicator | ports::Capability::Inclinometer;

    // A host board can be fitted with less than everything, which is how the
    // degraded paths get exercised without a soldering iron.
    explicit Platform(ports::Capabilities fitted = kFullyFitted, uint32_t device_addr = kDeviceAddr)
        : fitted_(fitted), device_addr_(device_addr) {
        chips_.epd.attach_clock(clock_);
        buzzer_pin_held_low_ = ports::has(fitted, ports::Capability::Buzzer);
        baro_.present = ports::has(fitted, ports::Capability::Baro);
        battery_.present = ports::has(fitted, ports::Capability::Battery);
        log_flash_.set_present(ports::has(fitted, ports::Capability::Storage));
        wire_i2c();
    }

    static Status begin() { return Status::Ok; }
    void wire(const io::PinMap& map) { gpio_.wire(map); }

    io::Spi& spi(io::BusId id) {
        if (id == io::BusId::Epd) return chips_.epd;
        return chips_.radio;
    }
    io::Uart& uart(io::BusId) { return chips_.gnss; }
    // Deliberately the null port, even though chips_.gnss can retune: this
    // platform is a bench, not a T-Echo, and the product rig feeds fixes onto the
    // bus rather than through the receiver model. A rate port here would have the
    // driver walk its candidates against a silence that is the rig's, not a
    // receiver's. Autobaud is proven where the model IS the wire,
    // test/hardware/test_l76k.cpp.
    static io::UartRate& uart_rate(io::BusId) { return io::kFixedUartRate; }
    io::Gpio& gpio() { return gpio_; }
    io::I2c& i2c(io::BusId) { return i2c_; }

    host::Clock& clock() { return clock_; }
    host::Link& link() { return link_; }
    host::KvStore& kv() { return kv_; }
    host::FlashRegion& log_flash() { return log_flash_; }
    host::Annunciator& annunciator() { return annunciator_; }
    host::Indicator& indicator() { return indicator_; }
    host::Dfu& dfu() { return dfu_; }
    host::Baro& baro() { return baro_; }
    host::Battery& battery() { return battery_; }
    host::DieTemperature& die_temperature() { return die_temperature_; }
    host::Pps& pps() { return pps_; }
    host::Watchdog& watchdog() { return watchdog_; }
    host::SystemPower& system_power() { return system_power_; }
    bool button_down() const { return gpio_.button_down; }
    bool pad_down() const { return gpio_.pad_down; }

    static parts::GlassRotation glass_rotation() { return parts::GlassRotation::Deg0; }

    // The panel fingerprint, as the silicon platform's board port takes it: 11
    // bytes of register 0x2D then 10 of 0x2E. The virtual glass carries which lot
    // it is from, so a host test flies a panel that cannot be powered off after a
    // partial update without a soldering iron.
    bool read_panel_signature(parts::PanelSignature& out) const {
        out = chips_.epd.signature;
        return out.read;
    }

    static ConstByteSpan imu_firmware() { return ConstByteSpan(kImuFirmware); }

    bool buzzer_pin_held_low() const { return buzzer_pin_held_low_; }
    void set_buzzer_pin_held_low(bool held) { buzzer_pin_held_low_ = held; }
    bool read_baro(BaroReading& out) { return baro_.read_baro(out); }
    bool read_battery_mv(uint16_t& out_mv) { return battery_.read_mv(out_mv); }
    bool external_power() const { return battery_.external_power; }
    static constexpr uint32_t kDeviceAddr = 0x5B5AFEu;
    uint32_t device_addr() const { return device_addr_; }
    Chips& chips() { return chips_; }
    Gpio& board_gpio() { return gpio_; }
    // The bus itself, so a test can fit a unit that came off the line with the
    // other barometer address, or with a part nobody expected.
    I2cBus& i2c_bus() { return i2c_; }

    ports::Capabilities capabilities() const { return fitted_; }

   private:
    void wire_i2c() {
        if (ports::has(fitted_, ports::Capability::Baro)) i2c_.answer(kBaroAddress, true);
        if (ports::has(fitted_, ports::Capability::Haptic))
            i2c_.attach(models::Drv2605::kAddress, chips_.haptic);
        if (ports::has(fitted_, ports::Capability::Inclinometer))
            i2c_.attach(models::Bhi260::kAddress, chips_.imu);
        else
            i2c_.answer(kImuAddress, true);
        i2c_.answer(kRtcAddress, true);
    }

    static constexpr uint8_t kImuFirmware[] = {0x2B, 0x66, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44};

    static constexpr uint8_t kBaroAddress = 0x76;
    static constexpr uint8_t kImuAddress = 0x28;
    static constexpr uint8_t kRtcAddress = 0x51;

    Chips chips_{};
    Gpio gpio_{chips_};
    I2cBus i2c_{};
    host::Clock clock_{};
    host::Link link_{};
    host::KvStore kv_{};
    host::FlashRegion log_flash_{};
    host::Annunciator annunciator_{};
    host::Indicator indicator_{};
    host::Dfu dfu_{};
    host::Baro baro_{};
    host::Battery battery_{};
    host::DieTemperature die_temperature_{};
    host::Pps pps_{clock_};
    host::Watchdog watchdog_{};
    host::SystemPower system_power_{};
    ports::Capabilities fitted_;
    uint32_t device_addr_;
    bool buzzer_pin_held_low_{false};
};

static_assert(fills_the_platform_contract<Platform>());

}  // namespace skyblip::platform::host

#endif
