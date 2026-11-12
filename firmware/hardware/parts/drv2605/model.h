#ifndef SKYBLIP_HARDWARE_MODEL_DRV2605_H
#define SKYBLIP_HARDWARE_MODEL_DRV2605_H

#include <cstdint>

#include "hardware/io/io.h"

namespace skyblip::models {

// The virtual DRV2605, at the two seams the real one has: an I2C register file
// and an enable pin. The important behaviour it models is the one that broke us:
// the enable pin does not move the motor. drive() is zero until the part has
// been taken out of standby, put in a playing mode and given something to play,
// which is exactly the sequence a GPIO and a timer cannot perform.
class Drv2605 : public io::I2c, public io::Gpio, public io::Delay {
   public:
    static constexpr uint8_t kAddress = 0x5A;
    static constexpr uint8_t kRegisterCount = 0x24;
    static constexpr uint8_t kConvergedCompensation = 0x0E;
    static constexpr uint8_t kConvergedBackEmf = 0x9A;
    static constexpr uint8_t kConvergedBackEmfGain = 0x01;

    // DRV2605L: STATUS[7:5] = 7. Settable, so a test can present a part that
    // answers the address but is not a haptic driver at all.
    Drv2605() {
        for (size_t i = 0; i < sizeof(kPowerUp); i++) registers[i] = kPowerUp[i];
    }

    // io::I2c. A register write is address then value, a register read is a
    // one-byte write of the address followed by a read, which is what the driver
    // does and what the datasheet's protocol section describes.
    bool write(uint8_t address, const uint8_t* data, size_t bytes) override {
        if (address != kAddress || !answers) return false;
        writes++;
        if (bytes == 0) return true;  // the bus's presence probe
        pointer = data[0];
        for (size_t i = 1; i < bytes; i++) {
            const uint8_t register_address = static_cast<uint8_t>(pointer + i - 1);
            if (register_address == kRegisterControl4) {
                if ((data[i] & kControl4OtpProgram) != 0) otp_burned = true;
                registers[kRegisterControl4] =
                    static_cast<uint8_t>((data[i] & ~kControl4OtpStatus) |
                                         (registers[kRegisterControl4] & kControl4OtpStatus));
            } else if (register_address < kRegisterCount) {
                registers[register_address] = data[i];
            }
            if (register_address == kRegisterGo && data[i] != 0) {
                go_pulses++;
                if (mode() == kModeAutoCalibration && !standby() && enabled) calibrate();
            }
        }
        return true;
    }

    bool read(uint8_t address, uint8_t* out, size_t bytes) override {
        if (address != kAddress || !answers) return false;
        reads++;
        for (size_t i = 0; i < bytes; i++) {
            const uint8_t register_address = static_cast<uint8_t>(pointer + i);
            out[i] = register_address < kRegisterCount ? registers[register_address] : 0xFF;
            if (register_address == kRegisterStatus)
                registers[kRegisterStatus] &= ~kDiagnosticResult;
        }
        return true;
    }

    void wait_at_least_us(uint32_t hold_us) override { waited_us += hold_us; }

    // io::Gpio: the enable line, and nothing else on this part.
    void set(int pin, bool level) override {
        if (pin != enable_pin) return;
        if (level && !enabled) enable_raises++;
        enabled = level;
    }
    bool get(int pin) override { return pin == enable_pin ? enabled : false; }
    void mode_output(int pin) override {
        if (pin == enable_pin) enable_driven = true;
    }
    void mode_input(int pin, bool) override {
        if (pin != enable_pin) return;
        enabled = false;
        enable_driven = false;
    }

    uint8_t mode() const { return registers[kRegisterMode] & 0x07; }
    bool standby() const { return (registers[kRegisterMode] & kModeStandby) != 0; }
    uint8_t library() const { return registers[kRegisterLibrary] & 0x07; }
    bool erm_selected() const { return (registers[kRegisterFeedback] & 0x80) == 0; }
    bool open_loop() const { return (registers[kRegisterControl3] & 0x20) != 0; }
    bool rtp_signed() const { return (registers[kRegisterControl3] & 0x08) == 0; }
    bool lra_closed_loop() const {
        return !erm_selected() && (registers[kRegisterControl3] & 0x01) == 0;
    }

    // What the actuator is actually being driven with, 0 for nothing. Real-time
    // playback holds its value; a waveform sequence only moves while GO is set.
    uint8_t drive() const {
        if (standby()) return 0;
        if (mode() == kModeRealTimePlayback) return registers[kRegisterRealTimePlaybackInput];
        if (registers[kRegisterGo] != 0) return registers[kRegisterWaveSequence1] != 0 ? 0xFF : 0;
        return 0;
    }
    bool moving() const { return drive() != 0; }

    uint8_t registers[kRegisterCount]{};
    uint8_t pointer{0};
    int enable_pin{8};
    bool enabled{false};
    bool enable_driven{false};
    bool answers{true};
    int enable_raises{0};
    int go_pulses{0};
    int writes{0};
    int reads{0};
    uint32_t waited_us{0};
    bool otp_burned{false};

    bool calibration_converges{true};
    bool calibration_hangs{false};
    int calibrations{0};
    uint8_t calibrated_feedback{0};
    uint8_t calibrated_rated_voltage{0};
    uint8_t calibrated_overdrive_clamp{0};

   private:
    static constexpr uint8_t kRegisterStatus = 0x00;
    static constexpr uint8_t kRegisterMode = 0x01;
    static constexpr uint8_t kRegisterRealTimePlaybackInput = 0x02;
    static constexpr uint8_t kRegisterLibrary = 0x03;
    static constexpr uint8_t kRegisterWaveSequence1 = 0x04;
    static constexpr uint8_t kRegisterGo = 0x0C;
    static constexpr uint8_t kRegisterRatedVoltage = 0x16;
    static constexpr uint8_t kRegisterOverdriveClamp = 0x17;
    static constexpr uint8_t kRegisterCalibrationCompensation = 0x18;
    static constexpr uint8_t kRegisterCalibrationBackEmf = 0x19;
    static constexpr uint8_t kRegisterFeedback = 0x1A;
    static constexpr uint8_t kRegisterControl3 = 0x1D;
    static constexpr uint8_t kRegisterControl4 = 0x1E;
    static constexpr uint8_t kModeStandby = 0x40;
    static constexpr uint8_t kModeRealTimePlayback = 0x05;
    static constexpr uint8_t kModeAutoCalibration = 0x07;
    static constexpr uint8_t kDiagnosticResult = 0x08;
    static constexpr uint8_t kControl4OtpProgram = 0x01;
    static constexpr uint8_t kControl4OtpStatus = 0x04;
    static constexpr uint8_t kBackEmfGain = 0x03;
    static constexpr uint8_t kPowerUp[] = {0xE0, 0x40, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
                                           0x19, 0xFF, 0x19, 0xFF, 0x3E, 0x8C, 0x0C, 0x6C, 0x36,
                                           0x93, 0xF5, 0xA0, 0x20, 0x80, 0x33, 0x00, 0x00};

    void calibrate() {
        calibrations++;
        calibrated_feedback = registers[kRegisterFeedback];
        calibrated_rated_voltage = registers[kRegisterRatedVoltage];
        calibrated_overdrive_clamp = registers[kRegisterOverdriveClamp];
        if (calibration_hangs) return;
        registers[kRegisterGo] = 0;
        const uint8_t feedback = registers[kRegisterFeedback] & ~kBackEmfGain;
        if (calibration_converges) {
            registers[kRegisterCalibrationCompensation] = kConvergedCompensation;
            registers[kRegisterCalibrationBackEmf] = kConvergedBackEmf;
            registers[kRegisterFeedback] = feedback | kConvergedBackEmfGain;
        } else {
            registers[kRegisterCalibrationCompensation] = 0xFF;
            registers[kRegisterCalibrationBackEmf] = 0x00;
            registers[kRegisterFeedback] = feedback;
            registers[kRegisterStatus] |= kDiagnosticResult;
        }
    }
};

}  // namespace skyblip::models

#endif
