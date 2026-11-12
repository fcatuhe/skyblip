// TI DRV2605 haptic driver, I2C 0x5A, with its enable line on a GPIO.
//
// This part exists because P0.08 on the T-Echo Plus is NOT a motor drive. LilyGO
// and Meshtastic call it PIN_DRV_EN / PIN_MOTOR_EN, and SoftRF identifies the
// Plus by finding a DRV2605 at 0x5A next to the BHI260AP
// (platform/nRF52.cpp:1158-1163, address at platform/nRF52.h:136). Driving that
// pin high brings the driver out of hardware standby and produces no pulse at
// all: the part needs a mode and a drive value over I2C before the motor moves
// (nRF52.cpp:2112-2133 configures it, 3383-3385 plays an effect, 2901 stops it).
//
// Register map and the ERM open-loop bring-up follow SensorLib's driver, which
// is the vendor library SoftRF uses: HapticDriver_DRV2605_Reg.hpp for the
// addresses and HapticDriver_DRV2605.cpp:410-456 for the sequence.
#ifndef SKYBLIP_HARDWARE_PARTS_DRV2605_H
#define SKYBLIP_HARDWARE_PARTS_DRV2605_H

#include "core/util/result.h"
#include "hardware/io/io.h"
#include "ports/haptic.h"

namespace skyblip::parts {

class Drv2605 : public ports::Haptic {
   public:
    // SoftRF platform/nRF52.h:136 (DRV2605_ADDRESS), and the datasheet's only
    // address: the part has no address-select pin.
    static constexpr uint8_t kAddress = 0x5A;

    enum class Actuator : uint8_t { Erm, Lra };

    struct Calibration {
        bool ran{false};
        bool passed{false};
        uint8_t compensation{0};
        uint8_t back_emf{0};
        uint8_t back_emf_gain{0};
    };

    // INFO: fc 12nov26 SLOS854D 8.6.24 ends AUTO_CAL_TIME 3 within 1200 ms, so a later GO has hung
    static constexpr uint32_t kCalibrationCeilingMs = 1500;

    Drv2605(io::I2c& bus, io::Gpio& gpio, io::Delay& delay, int enable_pin, Actuator actuator)
        : bus_(bus), gpio_(gpio), delay_(delay), enable_pin_(enable_pin), actuator_(actuator) {}

    // The enable line, raised before anything on the bus is believed. SoftRF
    // configures the part over I2C first and raises the pin afterwards, which is
    // evidence that the chip answers with this pin undriven - it found 0x5A at
    // nRF52.cpp:1158 long before nRF52.cpp:2130 drove the pin. We raise it
    // first anyway: it costs one register write, and it is the order that also
    // works if the line turns out to gate the motor's supply rather than the
    // driver's standby.
    void power_up();

    // Identify and configure, or report absent. Never asserts presence from the
    // pin: the address has to answer and the part has to name itself.
    Status begin();

    // ports::Haptic. Real-time playback, not a ROM effect: the drive value is a
    // register that holds until it is cleared, so the motor runs exactly as long
    // as the caller keeps it on, and no entry in the effect ROM has to be trusted to
    // last a particular number of milliseconds. The ROM route (setWaveform +
    // GO, which is what SoftRF plays) gives the effect's own length, which is
    // not the length core/annunciation asked for.
    void start() override;
    void stop() override;

    // TODO: fc 12nov26 smoke image only, as a boot buzz reads as traffic, until #169 stores results
    void calibrate();

    // Standby and the enable line held low, for the way down. The enable of a
    // part that never answered is released instead.
    void park();

    bool ready() const { return ready_; }
    // STATUS[7:5]. 3 DRV2605, 4 DRV2604, 5 DRV2605X, 6 DRV2604L, 7 DRV2605L.
    uint8_t device_id() const { return device_id_; }
    bool otp_programmed() const { return otp_programmed_; }
    const Calibration& calibration() const { return calibration_; }

   private:
    static constexpr uint8_t kRegisterStatus = 0x00;
    static constexpr uint8_t kRegisterMode = 0x01;
    static constexpr uint8_t kRegisterRealTimePlaybackInput = 0x02;
    static constexpr uint8_t kRegisterLibrary = 0x03;
    static constexpr uint8_t kRegisterWaveSequence1 = 0x04;
    static constexpr uint8_t kRegisterWaveSequence2 = 0x05;
    static constexpr uint8_t kRegisterGo = 0x0C;
    static constexpr uint8_t kRegisterOverdrive = 0x0D;
    static constexpr uint8_t kRegisterSustainPositive = 0x0E;
    static constexpr uint8_t kRegisterSustainNegative = 0x0F;
    static constexpr uint8_t kRegisterBreak = 0x10;
    static constexpr uint8_t kRegisterAudioMaximumInput = 0x13;
    static constexpr uint8_t kRegisterRatedVoltage = 0x16;
    static constexpr uint8_t kRegisterOverdriveClamp = 0x17;
    static constexpr uint8_t kRegisterCalibrationCompensation = 0x18;
    static constexpr uint8_t kRegisterCalibrationBackEmf = 0x19;
    static constexpr uint8_t kRegisterFeedback = 0x1A;
    static constexpr uint8_t kRegisterControl1 = 0x1B;
    static constexpr uint8_t kRegisterControl2 = 0x1C;
    static constexpr uint8_t kRegisterControl3 = 0x1D;
    static constexpr uint8_t kRegisterControl4 = 0x1E;
    static constexpr uint8_t kRegisterControl5 = 0x1F;

    static constexpr uint8_t kModeInternalTrigger = 0x00;
    static constexpr uint8_t kModeRealTimePlayback = 0x05;
    static constexpr uint8_t kModeAutoCalibration = 0x07;
    static constexpr uint8_t kStatusDiagnosticResult = 0x08;
    static constexpr uint8_t kGoFire = 0x01;
    static constexpr uint8_t kFeedbackBackEmfGain = 0x03;
    static constexpr uint8_t kControl3LraOpenLoop = 0x01;
    static constexpr uint8_t kControl3DataFormatRtpUnsigned = 0x08;
    static constexpr uint8_t kControl4OtpStatus = 0x04;
    static constexpr uint8_t kLraLibrary = 0x06;
    static constexpr uint32_t kCalibrationPollMs = 10;

    // TODO: fc 12nov26 SLOS854D 8.6.20 defaults with N_ERM_LRA, as Meshtastic's X1, until #169
    static constexpr uint8_t kLraFeedback = 0xB6;
    // TODO: fc 12nov26 SLOS854D 8.6.16 default, about 1.5 Vrms, until #169 names the motor
    static constexpr uint8_t kLraRatedVoltage = 0x3E;
    // TODO: fc 12nov26 SLOS854D 8.6.17 default, 3.0 V peak, until #169 names the motor
    static constexpr uint8_t kLraOverdriveClamp = 0x8C;
    // TODO: fc 12nov26 SLOS854D 8.6.21 default, a 208 Hz guess, until #169 reads the resonance
    static constexpr uint8_t kLraControl1 = 0x93;
    // TODO: fc 12nov26 SLOS854D 8.6.22 default, as 8.5.6 step 3 asks, until #169 benches it
    static constexpr uint8_t kLraControl2 = 0xF5;
    // TODO: fc 12nov26 SLOS854D 8.5.6 step 3f's AUTO_CAL_TIME, until #169 benches the motor
    static constexpr uint8_t kLraControl4 = 0x30;
    // TODO: fc 12nov26 SLOS854D 8.6.25 default, auto open loop off, until #169 benches the motor
    static constexpr uint8_t kLraControl5 = 0x80;
    // MODE bit 6. Software standby: the bus stays alive, the driver stage does
    // not, which is where the part belongs between pulses.
    static constexpr uint8_t kModeStandby = 0x40;
    // FEEDBACK bit 7 clear selects ERM rather than an LRA; CONTROL3 bit 5 is
    // ERM_OPEN_LOOP. Open loop because the fitted motor is not in any datasheet
    // we have: closed loop needs a rated voltage and a back-EMF the part can
    // trust, and an unknown motor gives neither.
    static constexpr uint8_t kFeedbackLraSelect = 0x80;
    static constexpr uint8_t kControl3ErmOpenLoop = 0x20;
    // ERM library A. Not used by real-time playback, but a library has to be
    // selected for the part to be in a defined state, and 1 is what both
    // SoftRF (nRF52.cpp:2121) and SensorLib pick for an ERM.
    static constexpr uint8_t kErmLibraryA = 0x01;
    // Signed full scale in RTP mode: DATA_FORMAT_RTP defaults to signed, so
    // 0x7F is 100% drive in one direction.
    static constexpr uint8_t kRealTimePlaybackFullScale = 0x7F;
    static constexpr uint8_t kAudioMaximumInputDefault = 0x64;

    bool write_register(uint8_t register_address, uint8_t value);
    bool read_register(uint8_t register_address, uint8_t& out);
    bool update_register(uint8_t register_address, uint8_t clear_mask, uint8_t set_mask);
    void configure_erm();
    void configure_lra();
    bool calibration_finished();

    io::I2c& bus_;
    io::Gpio& gpio_;
    io::Delay& delay_;
    int enable_pin_;
    Actuator actuator_;
    bool ready_{false};
    bool driving_{false};
    uint8_t device_id_{0};
    bool otp_programmed_{false};
    Calibration calibration_{};
};

}  // namespace skyblip::parts

#endif
