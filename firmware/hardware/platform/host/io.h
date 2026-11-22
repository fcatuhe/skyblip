#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_IO_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_IO_H

#include "hardware/io/io.h"
#include "hardware/parts/bhi260/model.h"
#include "hardware/parts/drv2605/model.h"
#include "hardware/parts/l76k/model.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/sx1262/model.h"

namespace skyblip::platform::host {

struct Chips {
    models::Sx1262 radio;
    models::Ssd1681 epd;
    models::L76k gnss;
    models::Drv2605 haptic;
    models::Bhi260 imu;
};

// INFO: fc 26sep26 not the world clock: a hold that advanced it would make set_milliseconds() wrap
class Delay : public io::Delay {
   public:
    explicit Delay(Chips& chips) : chips_(chips) {}

    void wait_at_least_us(uint32_t hold_us) override {
        chips_.radio.wait_at_least_us(hold_us);
        chips_.epd.wait_at_least_us(hold_us);
    }

   private:
    Chips& chips_;
};

// INFO: fc 20nov26 fitted but unused parts such as the RTC answer their address too, so a scan sees
// what the board carries
class I2cBus : public io::I2c {
   public:
    static constexpr int kMaxDevices = 8;

    void attach(uint8_t address, io::I2c& device) {
        if (device_count_ >= kMaxDevices) return;
        devices_[device_count_].address = address;
        devices_[device_count_].device = &device;
        device_count_++;
        answer(address, true);
    }

    void answer(uint8_t address, bool on) {
        if (address >= kAddressSpace) return;
        if (on)
            answers_[address >> 3] |= static_cast<uint8_t>(1u << (address & 7));
        else
            answers_[address >> 3] &= static_cast<uint8_t>(~(1u << (address & 7)));
    }

    bool answers(uint8_t address) const {
        return address < kAddressSpace &&
               (answers_[address >> 3] & static_cast<uint8_t>(1u << (address & 7))) != 0;
    }

    bool write(uint8_t address, const uint8_t* data, size_t bytes) override {
        if (!answers(address)) return false;
        io::I2c* device = route(address);
        if (device) return device->write(address, data, bytes);
        return true;
    }

    bool read(uint8_t address, uint8_t* out, size_t bytes) override {
        if (!answers(address)) return false;
        io::I2c* device = route(address);
        if (device) return device->read(address, out, bytes);
        for (size_t i = 0; i < bytes; i++) out[i] = 0xFF;
        return true;
    }

   private:
    static constexpr int kAddressSpace = 128;

    struct Slot {
        uint8_t address{0};
        io::I2c* device{nullptr};
    };

    io::I2c* route(uint8_t address) {
        for (int i = 0; i < device_count_; i++)
            if (devices_[i].address == address) return devices_[i].device;
        return nullptr;
    }

    Slot devices_[kMaxDevices]{};
    int device_count_{0};
    uint8_t answers_[kAddressSpace / 8]{};
};

class Gpio : public io::Gpio {
   public:
    explicit Gpio(Chips& chips) : chips_(chips) {}

    void wire(const io::PinMap& map) {
        for (int i = 0; i < map.count; i++) {
            const io::PinRole& role = map.roles[i];
            switch (role.function) {
                case io::PinFunction::RadioBusy: chips_.radio.busy_pin = role.pin; break;
                case io::PinFunction::RadioReset: chips_.radio.reset_pin = role.pin; break;
                case io::PinFunction::RadioIrq: chips_.radio.dio1_pin = role.pin; break;
                case io::PinFunction::EpdDc: chips_.epd.dc_pin = role.pin; break;
                case io::PinFunction::EpdReset: chips_.epd.reset_pin = role.pin; break;
                case io::PinFunction::EpdBusy: chips_.epd.busy_pin = role.pin; break;
                case io::PinFunction::EpdBacklight: chips_.epd.backlight_pin = role.pin; break;
                case io::PinFunction::HapticEnable: chips_.haptic.enable_pin = role.pin; break;
                case io::PinFunction::GnssReset: chips_.gnss.reset_pin = role.pin; break;
            }
            owner_[i] = role;
            count_ = i + 1;
        }
    }

    void set(int pin, bool level) override {
        io::Gpio* owner = route(pin);
        if (owner) owner->set(pin, level);
    }
    bool get(int pin) override {
        io::Gpio* owner = route(pin);
        return owner ? owner->get(pin) : false;
    }
    void mode_output(int pin) override {
        io::Gpio* owner = route(pin);
        if (owner) owner->mode_output(pin);
    }
    void mode_input(int pin, bool pullup) override {
        io::Gpio* owner = route(pin);
        if (owner) owner->mode_input(pin, pullup);
    }

    bool button_down{false};
    bool pad_down{false};

   private:
    static constexpr int kMaxPins = 16;

    io::Gpio* route(int pin) {
        for (int i = 0; i < count_; i++) {
            if (owner_[i].pin != pin) continue;
            switch (owner_[i].function) {
                case io::PinFunction::RadioBusy:
                case io::PinFunction::RadioReset:
                case io::PinFunction::RadioIrq: return &chips_.radio;
                case io::PinFunction::EpdDc:
                case io::PinFunction::EpdReset:
                case io::PinFunction::EpdBusy:
                case io::PinFunction::EpdBacklight: return &chips_.epd;
                case io::PinFunction::HapticEnable: return &chips_.haptic;
                case io::PinFunction::GnssReset: return &chips_.gnss;
            }
        }
        return nullptr;
    }

    Chips& chips_;
    io::PinRole owner_[kMaxPins]{};
    int count_{0};
};

}  // namespace skyblip::platform::host

#endif
