#include "hardware/parts/drv2605/drv2605.h"

namespace skyblip::parts {

void Drv2605::power_up() {
    if (enable_pin_ < 0) return;
    gpio_.mode_output(enable_pin_);
    gpio_.set(enable_pin_, true);
}

Status Drv2605::begin() {
    ready_ = false;
    power_up();

    // The address has to answer before a register read means anything: a
    // zero-length write is the bus's own presence question, and on a bus with
    // nothing on it every read would otherwise return the pull-ups' 0xFF.
    if (!bus_.write(kAddress, nullptr, 0)) return Status::Down;

    uint8_t status = 0;
    if (!read_register(kRegisterStatus, status)) return Status::Down;
    device_id_ = static_cast<uint8_t>(status >> 5);
    switch (device_id_) {
        case 3:  // DRV2605
        case 4:  // DRV2604
        case 5:  // DRV2605X
        case 6:  // DRV2604L
        case 7:  // DRV2605L
            break;
        default: return Status::Unsupported;
    }

    // Out of software standby first: every register below is only writable with
    // the part awake.
    if (!write_register(kRegisterMode, kModeInternalTrigger)) return Status::Down;
    write_register(kRegisterRealTimePlaybackInput, 0x00);
    write_register(kRegisterLibrary, actuator_ == Actuator::Lra ? kLraLibrary : kErmLibraryA);
    write_register(kRegisterWaveSequence1, 0x01);
    write_register(kRegisterWaveSequence2, 0x00);  // the terminator: a sequence with no end runs on
    write_register(kRegisterOverdrive, 0x00);
    write_register(kRegisterSustainPositive, 0x00);
    write_register(kRegisterSustainNegative, 0x00);
    write_register(kRegisterBreak, 0x00);
    write_register(kRegisterAudioMaximumInput, kAudioMaximumInputDefault);
    if (actuator_ == Actuator::Lra) {
        configure_lra();
    } else {
        configure_erm();
    }

    // Configured and idle. Standby rather than awake: this device counts
    // microamps in a flight bag, and an idle driver stage is milliamps.
    write_register(kRegisterMode, kModeStandby);
    ready_ = true;
    return Status::Ok;
}

void Drv2605::start() {
    if (!ready_ || driving_) return;
    driving_ = true;
    write_register(kRegisterMode, kModeRealTimePlayback);
    write_register(kRegisterRealTimePlaybackInput, kRealTimePlaybackFullScale);
}

void Drv2605::stop() {
    if (!ready_) return;
    driving_ = false;
    write_register(kRegisterRealTimePlaybackInput, 0x00);
    // GO is cleared too, so a part left mid-sequence by anything else - a reset
    // in the middle of a pulse, a ROM effect from a bench session - is silent
    // when the alarm releases it.
    write_register(kRegisterGo, 0x00);
    write_register(kRegisterMode, kModeStandby);
}

// INFO: fc 08nov26 EN held low on a part that answered, which is its own shutdown, and released
// otherwise, as boot raised it on every board
void Drv2605::park() {
    stop();
    if (enable_pin_ < 0) return;
    if (ready_) {
        gpio_.mode_output(enable_pin_);
        gpio_.set(enable_pin_, false);
    } else {
        gpio_.mode_input(enable_pin_, false);
    }
}

// INFO: fc 12nov26 a failed calibration restores the results the part held, which Meshtastic drives
void Drv2605::calibrate() {
    calibration_ = Calibration{};
    if (!ready_ || driving_ || actuator_ != Actuator::Lra) return;
    uint8_t held_compensation = 0;
    uint8_t held_back_emf = 0;
    uint8_t held_feedback = 0;
    if (read_register(kRegisterCalibrationCompensation, held_compensation) &&
        read_register(kRegisterCalibrationBackEmf, held_back_emf) &&
        read_register(kRegisterFeedback, held_feedback) &&
        write_register(kRegisterMode, kModeAutoCalibration) &&
        write_register(kRegisterGo, kGoFire)) {
        calibration_.ran = true;
        const bool finished = calibration_finished();
        if (!finished) write_register(kRegisterGo, 0x00);
        uint8_t status = 0;
        uint8_t feedback = 0;
        const bool reported =
            read_register(kRegisterStatus, status) &&
            read_register(kRegisterCalibrationCompensation, calibration_.compensation) &&
            read_register(kRegisterCalibrationBackEmf, calibration_.back_emf) &&
            read_register(kRegisterFeedback, feedback);
        calibration_.back_emf_gain = static_cast<uint8_t>(feedback & kFeedbackBackEmfGain);
        calibration_.passed = finished && reported && (status & kStatusDiagnosticResult) == 0;
        if (!calibration_.passed) {
            write_register(kRegisterCalibrationCompensation, held_compensation);
            write_register(kRegisterCalibrationBackEmf, held_back_emf);
            write_register(kRegisterFeedback, held_feedback);
        }
    }
    write_register(kRegisterMode, kModeStandby);
}

void Drv2605::configure_erm() {
    update_register(kRegisterFeedback, kFeedbackLraSelect, 0x00);
    update_register(kRegisterControl3, 0x00, kControl3ErmOpenLoop);
}

// INFO: fc 12nov26 SLOS854D 9.3.1 step 4: a part with programmed OTP keeps its 0x16 to 0x1A
void Drv2605::configure_lra() {
    uint8_t control4 = 0;
    otp_programmed_ =
        read_register(kRegisterControl4, control4) && (control4 & kControl4OtpStatus) != 0;
    if (otp_programmed_) {
        update_register(kRegisterFeedback, 0x00, kFeedbackLraSelect);
    } else {
        update_register(kRegisterFeedback, static_cast<uint8_t>(~kFeedbackBackEmfGain),
                        kLraFeedback & ~kFeedbackBackEmfGain);
        write_register(kRegisterRatedVoltage, kLraRatedVoltage);
        write_register(kRegisterOverdriveClamp, kLraOverdriveClamp);
    }
    write_register(kRegisterControl1, kLraControl1);
    write_register(kRegisterControl2, kLraControl2);
    update_register(kRegisterControl3, kControl3LraOpenLoop | kControl3DataFormatRtpUnsigned, 0x00);
    write_register(kRegisterControl4, kLraControl4);
    write_register(kRegisterControl5, kLraControl5);
}

bool Drv2605::calibration_finished() {
    uint32_t waited_ms = 0;
    for (;;) {
        uint8_t go = kGoFire;
        if (read_register(kRegisterGo, go) && (go & kGoFire) == 0) return true;
        if (waited_ms >= kCalibrationCeilingMs) return false;
        delay_.wait_at_least_us(kCalibrationPollMs * 1000);
        waited_ms += kCalibrationPollMs;
    }
}

bool Drv2605::write_register(uint8_t register_address, uint8_t value) {
    const uint8_t frame[2] = {register_address, value};
    return bus_.write(kAddress, frame, sizeof(frame));
}

bool Drv2605::read_register(uint8_t register_address, uint8_t& out) {
    if (!bus_.write(kAddress, &register_address, 1)) return false;
    return bus_.read(kAddress, &out, 1);
}

bool Drv2605::update_register(uint8_t register_address, uint8_t clear_mask, uint8_t set_mask) {
    uint8_t value = 0;
    if (!read_register(register_address, value)) return false;
    const uint8_t updated = static_cast<uint8_t>((value & ~clear_mask) | set_mask);
    return write_register(register_address, updated);
}

}  // namespace skyblip::parts
