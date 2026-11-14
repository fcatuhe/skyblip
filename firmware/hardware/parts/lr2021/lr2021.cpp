#include "hardware/parts/lr2021/lr2021.h"

#include <cstring>

namespace skyblip::parts {

namespace {

void put_be16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value >> 8);
    out[1] = static_cast<uint8_t>(value);
}

void put_be32(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value >> 24);
    out[1] = static_cast<uint8_t>(value >> 16);
    out[2] = static_cast<uint8_t>(value >> 8);
    out[3] = static_cast<uint8_t>(value);
}

uint32_t be32(const uint8_t* in) {
    return (static_cast<uint32_t>(in[0]) << 24) | (static_cast<uint32_t>(in[1]) << 16) |
           (static_cast<uint32_t>(in[2]) << 8) | in[3];
}

uint16_t be16(const uint8_t* in) { return static_cast<uint16_t>((in[0] << 8) | in[1]); }

// INFO: fc 11nov26 DS 16.3.7: RssiHigh is nine bits split over two bytes, minus half a dB a count
int8_t rssi_high_dbm(const uint8_t* status) {
    const int raw = (status[3] << 1) | (status[4] & 0x01);
    const int dbm = -(raw / 2);
    return static_cast<int8_t>(dbm < -128 ? -128 : dbm);
}

// TODO: fc 13nov26 adsb trial #293, remove once the trial picks the LR2021 settings
// INFO: fc 13nov26 the part decoded the bits, so they go back to the chips the decoder reads
bool chips_of_bits(const uint8_t* bits, uint8_t* chips) {
    if ((bits[0] & 0xF0) != 0x80) return false;
    std::memset(chips, 0, lr2021::kChipBytes);
    for (int bit = 4; bit < 8 * lr2021::kManchesterFrameBytes; bit++) {
        const bool one = ((bits[bit >> 3] >> (7 - (bit & 7))) & 1u) != 0;
        const int chip = 2 * (bit - 4) + (one ? 0 : 1);
        chips[chip >> 3] = static_cast<uint8_t>(chips[chip >> 3] | (0x80u >> (chip & 7)));
    }
    return true;
}

}  // namespace

Status Lr2021::probe() {
    const Status status = reset();
    if (status != Status::Ok) return status;
    return verify_version();
}

Status Lr2021::begin(int16_t detection_threshold_dbm) {
    tuning_.detection_threshold_dbm = detection_threshold_dbm;
    reset_owed_ = false;
    const Status status = bring_up();
    if (status != Status::Ok) mode_ = Lr2021Mode::Reset;
    return status;
}

Status Lr2021::start_receive() {
    if (mode_ != Lr2021Mode::Standby) return Status::Invalid;
    const uint8_t path[2] = {lr2021::kRxPathLowFrequency, tuning_.boost};
    uint8_t irq_config[5] = {irq_dio_};
    put_be32(irq_config + 1, lr2021::kIrqRxDone);
    uint8_t clear[4];
    put_be32(clear, lr2021::kIrqAll);
    uint8_t continuous[3] = {0xFF, 0xFF, 0xFF};
    Status status = command(lr2021::kSetRxPath, path, sizeof(path));
    if (status == Status::Ok) status = command(lr2021::kSetDioIrqConfig, irq_config, 5);
    if (status == Status::Ok) status = command(lr2021::kClearIrq, clear, sizeof(clear));
    if (status == Status::Ok) status = command(lr2021::kSetRx, continuous, sizeof(continuous));
    if (status != Status::Ok) return status;
    mode_ = Lr2021Mode::Receive;
    frames_waiting_ = 0;
    since_frame_ms_ = 0;
    return Status::Ok;
}

// TODO: fc 13nov26 adsb trial #293, remove once the trial picks the LR2021 settings
Status Lr2021::retune(const Lr2021Tuning& tuning) {
    tuning_ = tuning;
    const bool receiving = mode_ == Lr2021Mode::Receive;
    if (!receiving && mode_ != Lr2021Mode::Standby) return Status::Ok;
    frames_waiting_ = 0;
    Status status = enter_standby();
    if (status == Status::Ok) status = configure_ook();
    if (status == Status::Ok) status = command(lr2021::kClearRxFifo, nullptr, 0);
    if (status == Status::Ok && receiving) status = start_receive();
    return status;
}

// INFO: fc 11nov26 RxDone can fire twice before a read, so whole frames are drained by FIFO level
Lr2021Event Lr2021::poll(uint8_t* chips, size_t capacity) {
    Lr2021Event event{};
    if (mode_ != Lr2021Mode::Receive || capacity < lr2021::kChipBytes) return event;
    if (frames_waiting_ == 0 && take_snapshot() != Status::Ok) {
        event.type = Lr2021EventType::Fault;
        return event;
    }
    while (frames_waiting_ != 0) {
        const Status read = read_frame(chips, event);
        if (read != Status::Ok && read != Status::Unsupported) {
            event.type = Lr2021EventType::Fault;
            return event;
        }
        frames_waiting_--;
        since_frame_ms_ = 0;
        if (read == Status::Ok) {
            event.type = Lr2021EventType::Frame;
            return event;
        }
        foreign_frames_++;
    }
    return Lr2021Event{};
}

// INFO: fc 11nov26 the packet status is read on both sides of the level, and only agreement gives a
// level
Status Lr2021::take_snapshot() {
    uint8_t irq[4] = {};
    uint8_t before[6] = {};
    uint8_t level[2] = {};
    uint8_t fifo_flags[2] = {};
    uint8_t after[6] = {};
    Status status = read(lr2021::kGetAndClearIrqStatus, irq, sizeof(irq));
    if (status != Status::Ok || (be32(irq) & lr2021::kIrqRxDone) == 0) return status;
    status = read(lr2021::kGetOokPacketStatus, before, sizeof(before));
    if (status == Status::Ok) status = read(lr2021::kGetRxFifoLevel, level, sizeof(level));
    if (status == Status::Ok)
        status = read(lr2021::kGetAndClearFifoIrqFlags, fifo_flags, sizeof(fifo_flags));
    if (status == Status::Ok) status = read(lr2021::kGetOokPacketStatus, after, sizeof(after));
    if (status != Status::Ok) return status;
    if ((fifo_flags[0] & lr2021::kFifoOverflow) != 0) {
        const uint32_t lost = (be16(level) + frame_bytes() - 1u) / frame_bytes();
        fifo_drops_ += lost != 0 ? lost : 1;
        return restart_receive();
    }
    frames_waiting_ = static_cast<uint8_t>(be16(level) / frame_bytes());
    newest_rssi_valid_ = std::memcmp(before, after, sizeof(before)) == 0;
    newest_rssi_dbm_ = rssi_high_dbm(after);
    return Status::Ok;
}

// INFO: fc 11nov26 SoftRF re-arms after 15 s of silence; a fault resets the part instead
bool Lr2021::service(uint32_t elapsed_ms, uint32_t silence_ceiling_ms) {
    if (mode_ == Lr2021Mode::Sleep) return false;
    if (reset_owed_) {
        (void)reinitialise();
        return true;
    }
    if (mode_ != Lr2021Mode::Receive) return false;
    since_frame_ms_ += elapsed_ms;
    if (since_frame_ms_ < silence_ceiling_ms) return false;
    if (rearm() != Status::Ok) (void)reinitialise();
    return true;
}

// INFO: fc 11nov26 a BUSY that blocks the standby gets one reset, so the part sleeps or the caller
// hears why not
Status Lr2021::sleep() {
    if (mode_ == Lr2021Mode::Sleep) return Status::Ok;
    const uint8_t sleep_config[5] = {lr2021::kSleepColdNoClock, 0, 0, 0, 0};
    Status status = enter_standby();
    if (status != Status::Ok) status = reset();
    if (status == Status::Ok) status = command(lr2021::kSetSleep, sleep_config, 5);
    if (status != Status::Ok) return status;
    delay_.wait_at_least_us(lr2021::kSleepSettleUs);
    if (!gpio_.get(busy_pin_)) return Status::Down;
    mode_ = Lr2021Mode::Sleep;
    return Status::Ok;
}

// INFO: fc 11nov26 for a part the thread never ran: asleep if it will, held in reset if not
void Lr2021::park() {
    if (sleep() != Status::Ok) hold_in_reset();
}

void Lr2021::hold_in_reset() {
    gpio_.mode_output(reset_pin_);
    gpio_.set(reset_pin_, false);
    mode_ = Lr2021Mode::Reset;
}

// INFO: fc 11nov26 a command frees BUSY in microseconds, so only a fault or a calibration reaches
// the sleeps
Status Lr2021::wait_busy_low(uint32_t ceiling_us) {
    uint32_t waited_us = 0;
    while (gpio_.get(busy_pin_)) {
        if (waited_us >= ceiling_us) return Status::Timeout;
        const uint32_t step_us =
            waited_us < lr2021::kBusySpinUs ? lr2021::kBusySpinPollUs : lr2021::kBusySleepPollUs;
        delay_.wait_at_least_us(step_us);
        waited_us += step_us;
    }
    return Status::Ok;
}

Status Lr2021::reset() {
    gpio_.mode_output(reset_pin_);
    gpio_.mode_input(busy_pin_, false);
    gpio_.mode_input(irq_pin_, false);
    gpio_.set(reset_pin_, false);
    delay_.wait_at_least_us(lr2021::kResetLowUs);
    gpio_.set(reset_pin_, true);
    mode_ = Lr2021Mode::Reset;
    last_opcode_ = lr2021::kNop;
    frames_waiting_ = 0;
    const Status status = wait_busy_low(lr2021::kBootBusyCeilingUs);
    if (status == Status::Ok) mode_ = Lr2021Mode::Standby;
    return status;
}

// INFO: fc 11nov26 BUSY falling proves a pull-down and a rail, only a version read proves the part
Status Lr2021::verify_version() {
    uint8_t version[2] = {};
    const Status status = read(lr2021::kGetVersion, version, sizeof(version));
    if (status != Status::Ok) return status;
    if (version[0] != lr2021::kVersionMajor || version[1] != lr2021::kVersionMinor)
        return Status::Down;
    return Status::Ok;
}

Status Lr2021::bring_up() {
    Status status = reset();
    if (status == Status::Ok) status = verify_version();
    if (status != Status::Ok) return status;

    const uint8_t standby = lr2021::kStandbyRc;
    uint8_t tcxo[5] = {lr2021::kTcxoTune1v6};
    put_be32(tcxo + 1, lr2021::kTcxoStartTime);
    const uint8_t fallback = lr2021::kFallbackStandbyRc;
    uint8_t clear[4];
    put_be32(clear, lr2021::kIrqAll);
    const uint8_t dio[2] = {irq_dio_, lr2021::kDioFunctionIrqPullDown};
    const uint8_t blocks = lr2021::kCalibrateAllBlocks;
    const uint8_t packet_type = lr2021::kPacketTypeOok;
    const uint8_t path[2] = {lr2021::kRxPathLowFrequency, tuning_.boost};
    uint8_t calibration[6] = {};
    put_be16(calibration, lr2021::kCalibFe1090Word);
    uint8_t frequency[4];
    put_be32(frequency, lr2021::kFrequencyHz);

    status = command(lr2021::kSetStandby, &standby, 1);
    if (status == Status::Ok) status = read_errors();
    if (status == Status::Ok && device_errors_ != 0)
        status = command(lr2021::kClearErrors, nullptr, 0);
    if (status == Status::Ok) status = command(lr2021::kSetTcxoMode, tcxo, sizeof(tcxo));
    if (status == Status::Ok) status = command(lr2021::kSetRxTxFallbackMode, &fallback, 1);
    if (status == Status::Ok) status = command(lr2021::kClearIrq, clear, sizeof(clear));
    if (status == Status::Ok) status = command(lr2021::kSetDioFunction, dio, sizeof(dio));
    if (status == Status::Ok) status = command(lr2021::kCalibrate, &blocks, 1);
    if (status == Status::Ok) status = command(lr2021::kSetPacketType, &packet_type, 1);
    if (status == Status::Ok) status = configure_ook();
    if (status == Status::Ok) status = command(lr2021::kSetRxPath, path, sizeof(path));
    if (status == Status::Ok) status = command(lr2021::kCalibFe, calibration, sizeof(calibration));
    if (status == Status::Ok) status = command(lr2021::kSetRfFrequency, frequency, 4);
    if (status == Status::Ok) status = read_errors();
    if (status != Status::Ok) return status;
    if ((device_errors_ & lr2021::kFatalErrors) != 0) return Status::Down;
    mode_ = Lr2021Mode::Standby;
    return Status::Ok;
}

// INFO: fc 11nov26 DS 22.1: the threshold override goes after SetOokModulationParams, which
// recomputes it
Status Lr2021::configure_ook() {
    uint8_t modulation[7] = {};
    put_be32(modulation, lr2021::kBitrateBps);
    modulation[4] = lr2021::kPulseShapeNone;
    modulation[5] = lr2021::kRxBandwidth3076kHz;
    modulation[6] = lr2021::kDepthFull;
    // TODO: fc 13nov26 adsb trial #293, back to SoftRF's layout once the trial picks the settings
    const bool manchester = tuning_.layout == Lr2021Layout::Manchester;
    uint8_t packet[6] = {};
    put_be16(packet, lr2021::kPreambleChips);
    packet[2] = lr2021::kFixedLength;
    put_be16(packet + 3, frame_bytes());
    packet[5] = manchester ? lr2021::kCrcOffManchesterInverted : lr2021::kCrcOffManchesterOff;
    const uint8_t whitening_off[4] = {};
    uint8_t syncword[5] = {0, 0, 0, lr2021::kSyncword, lr2021::kSyncwordMsbFirstBits};
    if (manchester) {
        syncword[3] = 0;
        syncword[4] = lr2021::kSyncwordNoneMsbFirst;
    }
    uint8_t detector[5] = {};
    put_be16(detector, lr2021::kAdsbPreamblePattern);
    detector[2] = lr2021::kAdsbPreambleChips - 1;
    const uint8_t gain = tuning_.gain_step;
    const uint32_t threshold =
        (static_cast<uint32_t>(tuning_.detection_threshold_dbm + lr2021::kOokThresholdOffsetDb)
         << 20) &
        lr2021::kOokThresholdMask;
    const uint32_t front_end =
        tuning_.front_end_raw ? lr2021::kOokQuirkData : lr2021::kOokQuirkChipDefault;

    Status status = command(lr2021::kSetOokModulationParams, modulation, sizeof(modulation));
    if (status == Status::Ok) status = command(lr2021::kSetOokPacketParams, packet, sizeof(packet));
    if (status == Status::Ok)
        status = command(lr2021::kSetOokWhiteningParams, whitening_off, sizeof(whitening_off));
    if (status == Status::Ok) status = command(lr2021::kSetOokSyncword, syncword, sizeof(syncword));
    if (status == Status::Ok) status = command(lr2021::kSetOokDetector, detector, sizeof(detector));
    if (status == Status::Ok) status = command(lr2021::kSetAgcGainManual, &gain, 1);
    if (status == Status::Ok)
        status = write_mask(lr2021::kOokThresholdRegister, lr2021::kOokThresholdMask, threshold);
    if (status == Status::Ok)
        status = write_mask(lr2021::kOokQuirkRegister, lr2021::kOokQuirkMask, front_end);
    return status;
}

Status Lr2021::rearm() {
    rearms_++;
    since_frame_ms_ = 0;
    return restart_receive();
}

// INFO: fc 11nov26 DS 6.3.5: SetRx fails on a part already in Rx, so receive restarts through
// standby
Status Lr2021::restart_receive() {
    frames_waiting_ = 0;
    Status status = enter_standby();
    if (status == Status::Ok) status = command(lr2021::kClearRxFifo, nullptr, 0);
    if (status == Status::Ok) status = start_receive();
    return status;
}

Status Lr2021::reinitialise() {
    resets_++;
    Status status = begin(tuning_.detection_threshold_dbm);
    if (status == Status::Ok) status = start_receive();
    reset_owed_ = status != Status::Ok;
    return status;
}

Status Lr2021::enter_standby() {
    const uint8_t standby = lr2021::kStandbyRc;
    const Status status = command(lr2021::kSetStandby, &standby, 1);
    if (status == Status::Ok) mode_ = Lr2021Mode::Standby;
    return status;
}

// INFO: fc 11nov26 DS 5.2: a command sent while BUSY is high is undefined, so it is never sent
Status Lr2021::command(uint16_t opcode, const uint8_t* arguments, size_t bytes) {
    if (bytes + 2 > kFrameBytes) return Status::Invalid;
    if (wait_busy_low(lr2021::kCommandBusyCeilingUs) != Status::Ok) {
        reset_owed_ = true;
        return Status::Timeout;
    }
    uint8_t tx[kFrameBytes] = {};
    uint8_t rx[kFrameBytes] = {};
    put_be16(tx, opcode);
    if (bytes != 0) std::memcpy(tx + 2, arguments, bytes);
    spi_.select(true);
    spi_.transfer(tx, rx, bytes + 2);
    spi_.select(false);
    note_status(rx);
    last_opcode_ = opcode;
    return Status::Ok;
}

// INFO: fc 11nov26 DS 5.4.1.2: a read answers in a second frame of zeros, behind two status bytes
Status Lr2021::read(uint16_t opcode, uint8_t* out, size_t bytes) {
    if (bytes + 2 > kFrameBytes) return Status::Invalid;
    const Status status = command(opcode, nullptr, 0);
    if (status != Status::Ok) return status;
    uint8_t tx[kFrameBytes] = {};
    uint8_t rx[kFrameBytes] = {};
    if (wait_busy_low(lr2021::kCommandBusyCeilingUs) != Status::Ok) {
        reset_owed_ = true;
        return Status::Timeout;
    }
    spi_.select(true);
    spi_.transfer(tx, rx, bytes + 2);
    spi_.select(false);
    note_status(rx);
    last_opcode_ = lr2021::kNop;
    std::memcpy(out, rx + 2, bytes);
    return Status::Ok;
}

Status Lr2021::write_mask(uint32_t address, uint32_t mask, uint32_t data) {
    uint8_t arguments[11];
    arguments[0] = static_cast<uint8_t>(address >> 16);
    arguments[1] = static_cast<uint8_t>(address >> 8);
    arguments[2] = static_cast<uint8_t>(address);
    put_be32(arguments + 3, mask);
    put_be32(arguments + 7, data);
    return command(lr2021::kWriteRegMemMask32, arguments, sizeof(arguments));
}

// INFO: fc 11nov26 DS 6.7.1: each frame opens on the status of the command before it
void Lr2021::note_status(const uint8_t* status) {
    if (last_opcode_ == lr2021::kNop) return;
    const uint8_t result = (status[0] >> lr2021::kCommandStatusShift) & lr2021::kCommandStatusMask;
    if (result == lr2021::kCommandFail || result == lr2021::kCommandParameterError)
        command_errors_++;
}

Status Lr2021::read_errors() {
    uint8_t errors[2] = {};
    const Status status = read(lr2021::kGetErrors, errors, sizeof(errors));
    if (status == Status::Ok) device_errors_ = be16(errors);
    return status;
}

// INFO: fc 11nov26 DS 5.4.1.3: the FIFO is the one read answered in its own frame
Status Lr2021::read_frame(uint8_t* chips, Lr2021Event& event) {
    if (wait_busy_low(lr2021::kCommandBusyCeilingUs) != Status::Ok) {
        reset_owed_ = true;
        return Status::Timeout;
    }
    uint8_t tx[2 + lr2021::kChipBytes] = {};
    uint8_t rx[2 + lr2021::kChipBytes] = {};
    put_be16(tx, lr2021::kReadRadioRxFifo);
    spi_.select(true);
    spi_.transfer(tx, rx, 2u + frame_bytes());
    spi_.select(false);
    note_status(rx);
    last_opcode_ = lr2021::kReadRadioRxFifo;
    event.rssi_valid = frames_waiting_ == 1 && newest_rssi_valid_;
    event.rssi_dbm = event.rssi_valid ? newest_rssi_dbm_ : 0;
    // TODO: fc 13nov26 adsb trial #293, remove once the trial picks the LR2021 settings
    if (tuning_.layout == Lr2021Layout::Manchester)
        return chips_of_bits(rx + 2, chips) ? Status::Ok : Status::Unsupported;
    std::memcpy(chips, rx + 2, lr2021::kChipBytes);
    return Status::Ok;
}

// TODO: fc 13nov26 adsb trial #293, remove once the trial picks the LR2021 settings
uint8_t Lr2021::frame_bytes() const {
    return tuning_.layout == Lr2021Layout::Manchester ? lr2021::kManchesterFrameBytes
                                                      : lr2021::kChipBytes;
}

}  // namespace skyblip::parts
