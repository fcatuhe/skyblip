#ifndef SKYBLIP_HARDWARE_PARTS_DRV2605_H
#define SKYBLIP_HARDWARE_PARTS_DRV2605_H

#include "core/util/result.h"
#include "hardware/io/io.h"
#include "ports/haptic.h"

namespace skyblip::parts {

class Drv2605 : public ports::Haptic {
   public:
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
    // INFO: fc 14nov26 SLOS854D 9.3.1: 250 us from power-up to I2C, in case the enable line gates
    // the supply
    static constexpr uint32_t kPowerUpUs = 250;

    Drv2605(io::I2c& bus, io::Gpio& gpio, io::Delay& delay, int enable_pin, Actuator actuator)
        : bus_(bus), gpio_(gpio), delay_(delay), enable_pin_(enable_pin), actuator_(actuator) {}

    // INFO: fc 20nov26 the enable goes up before the bus is believed, in case it gates the motor's
    // supply: README.md
    void power_up();

    // INFO: fc 20nov26 presence needs the address to answer and the part to name itself, never the
    // pin alone
    Status begin();

    // INFO: fc 20nov26 real-time playback, so the motor runs as long as the caller asks, not a ROM
    // effect's length
    void start() override;
    void stop() override;

    // TODO: fc 12nov26 smoke image only, as a boot buzz reads as traffic, until #169 stores results
    void calibrate();

    void park();

    bool ready() const { return ready_; }
    // INFO: fc 20nov26 STATUS[7:5]: 3 DRV2605, 4 DRV2604, 5 DRV2605X, 6 DRV2604L, 7 DRV2605L
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
    static constexpr uint8_t kModeStandby = 0x40;
    // INFO: fc 20nov26 the T-Echo's ERM runs open loop, because its unknown motor gives no rated
    // voltage or back-EMF
    static constexpr uint8_t kFeedbackLraSelect = 0x80;
    static constexpr uint8_t kControl3ErmOpenLoop = 0x20;
    // INFO: fc 20nov26 real-time playback ignores the library, but one must be selected for a
    // defined state
    static constexpr uint8_t kErmLibraryA = 0x01;
    // INFO: fc 20nov26 DATA_FORMAT_RTP defaults to signed, so 0x7F is full drive in one direction
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
