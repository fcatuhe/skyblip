#include "hardware/parts/lr2021/lr2021.h"

#include <cstring>

#include "core/util/clamp.h"

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

// INFO: fc 26nov26 DS 7.3.3: nine bits, the low one in bit 7 of the second byte, minus half a dB
int8_t rssi_inst_dbm(const uint8_t* rssi) {
    const int raw = (rssi[0] << 1) | (rssi[1] >> 7);
    const int dbm = -(raw / 2);
    return static_cast<int8_t>(dbm < -128 ? -128 : dbm);
}

int8_t threshold_dbm(uint32_t reg) {
    const uint32_t field = (reg & lr2021::kOokThresholdMask) >> lr2021::kOokThresholdShift;
    const int32_t signed_field = (field & lr2021::kOokThresholdSignBit) != 0
                                     ? static_cast<int32_t>(field) - lr2021::kOokThresholdSpan
                                     : static_cast<int32_t>(field);
    return clamp_int8(signed_field - lr2021::kOokThresholdOffsetDb);
}

ports::ReceiverMode mode_of(uint8_t status) {
    const uint8_t code = status & lr2021::kChipModeMask;
    return code <= static_cast<uint8_t>(ports::ReceiverMode::Transmit)
               ? static_cast<ports::ReceiverMode>(code)
               : ports::ReceiverMode::Unknown;
}

ports::ReceiverReset reset_of(uint8_t status) {
    const uint8_t code = (status >> lr2021::kResetSourceShift) & lr2021::kResetSourceMask;
    return code <= static_cast<uint8_t>(ports::ReceiverReset::Pin)
               ? static_cast<ports::ReceiverReset>(code)
               : ports::ReceiverReset::Unknown;
}

}  // namespace

Status Lr2021::probe() {
    const Status status = reset();
    if (status != Status::Ok) return status;
    return verify_version();
}

Status Lr2021::begin() {
    reset_owed_ = false;
    const Status status = bring_up();
    if (status != Status::Ok) mode_ = Lr2021Mode::Reset;
    return status;
}

Status Lr2021::start_receive() {
    if (mode_ != Lr2021Mode::Standby) return Status::Invalid;
    const uint8_t path[2] = {lr2021::kRxPathLowFrequency, rx_boost_};
    uint8_t irq_config[5] = {irq_dio_};
    put_be32(irq_config + 1, lr2021::kIrqRxFifo);
    uint8_t fifo_irq[10] = {lr2021::kFifoHigh | lr2021::kFifoOverflow, 0};
    put_be16(fifo_irq + 2, lr2021::kFifoHighFrames * lr2021::kFifoFrameBytes);
    uint8_t clear[4];
    put_be32(clear, lr2021::kIrqAll);
    uint8_t continuous[3] = {0xFF, 0xFF, 0xFF};
    Status status = command(lr2021::kSetRxPath, path, sizeof(path));
    if (status == Status::Ok) status = command(lr2021::kConfigFifoIrq, fifo_irq, sizeof(fifo_irq));
    if (status == Status::Ok) status = command(lr2021::kSetDioIrqConfig, irq_config, 5);
    if (status == Status::Ok) status = command(lr2021::kClearIrq, clear, sizeof(clear));
    if (status == Status::Ok) status = read_packets(packets_at_clear_);
    if (status == Status::Ok) status = command(lr2021::kSetRx, continuous, sizeof(continuous));
    if (status != Status::Ok) return status;
    mode_ = Lr2021Mode::Receive;
    frames_waiting_ = 0;
    frames_since_clear_ = 0;
    since_frame_ms_ = 0;
    return Status::Ok;
}

// INFO: fc 27nov26 one snapshot, then the whole frames it counted in one FIFO read, oldest first
Lr2021Event Lr2021::poll(uint8_t* frames, size_t capacity) {
    Lr2021Event event{};
    if (mode_ != Lr2021Mode::Receive || capacity < lr2021::kFifoFrameBytes) return event;
    if (frames_waiting_ == 0 && take_snapshot() != Status::Ok) {
        event.type = Lr2021EventType::Fault;
        return event;
    }
    const size_t room = capacity / lr2021::kFifoFrameBytes;
    const uint8_t count = static_cast<uint8_t>(frames_waiting_ < room ? frames_waiting_ : room);
    if (count == 0) return event;
    if (read_frames(frames, count, event) != Status::Ok) {
        event.type = Lr2021EventType::Fault;
        return event;
    }
    frames_waiting_ = static_cast<uint8_t>(frames_waiting_ - count);
    frames_since_clear_ = static_cast<uint16_t>(frames_since_clear_ + count);
    since_frame_ms_ = 0;
    event.type = Lr2021EventType::Frame;
    event.frames = count;
    return event;
}

// INFO: fc 27nov26 two equal counts around the packet status make it the newest counted frame's
Status Lr2021::take_snapshot() {
    uint16_t packets_before = 0;
    Status status = read_packets(packets_before);
    if (status != Status::Ok) return status;
    const uint16_t whole =
        static_cast<uint16_t>(packets_before - packets_at_clear_ - frames_since_clear_);
    if (whole == 0) return Status::Ok;
    uint8_t packet[6] = {};
    uint8_t level[2] = {};
    uint8_t fifo_flags[2] = {};
    uint16_t packets_after = 0;
    uint8_t clear[4];
    put_be32(clear, lr2021::kIrqAll);
    status = read(lr2021::kGetOokPacketStatus, packet, sizeof(packet));
    if (status == Status::Ok) status = read(lr2021::kGetRxFifoLevel, level, sizeof(level));
    if (status == Status::Ok)
        status = read(lr2021::kGetAndClearFifoIrqFlags, fifo_flags, sizeof(fifo_flags));
    if (status == Status::Ok) status = read_packets(packets_after);
    if (status == Status::Ok) status = command(lr2021::kClearIrq, clear, sizeof(clear));
    if (status != Status::Ok) return status;
    if ((fifo_flags[0] & lr2021::kFifoOverflow) != 0) {
        const uint32_t lost =
            (be16(level) + lr2021::kFifoFrameBytes - 1u) / lr2021::kFifoFrameBytes;
        overflow_drops_ += lost != 0 ? lost : 1;
        return restart_receive();
    }
    newest_rssi_valid_ = packets_before == packets_after;
    newest_rssi_dbm_ = rssi_high_dbm(packet);
    // INFO: fc 26nov26 a frame that closed between the two counts may or may not be in the level
    if (packets_before != packets_after) {
        split_counts_++;
        const uint16_t landed = be16(level) / lr2021::kFifoFrameBytes;
        frames_waiting_ = static_cast<uint8_t>(whole < landed ? whole : landed);
        return Status::Ok;
    }
    return check_framing(whole, be16(level));
}

// INFO: fc 26nov26 every byte belongs to a frame the part counted or to the one still arriving
Status Lr2021::check_framing(uint16_t whole, uint16_t level) {
    const int32_t excess =
        static_cast<int32_t>(level) - static_cast<int32_t>(whole) * lr2021::kFifoFrameBytes;
    if (excess > 0 && excess < lr2021::kFifoFrameBytes) {
        uint16_t packets = 0;
        uint8_t again[2] = {};
        delay_.wait_at_least_us(lr2021::kInFlightGrowthUs);
        Status status = read_packets(packets);
        if (status == Status::Ok) status = read(lr2021::kGetRxFifoLevel, again, sizeof(again));
        if (status != Status::Ok) return status;
        const uint16_t counted =
            static_cast<uint16_t>(packets - packets_at_clear_ - frames_since_clear_);
        if (counted != whole || be16(again) != level) {
            in_flight_++;
            frames_waiting_ = static_cast<uint8_t>(whole);
            return Status::Ok;
        }
        stranded_resyncs_++;
    }
    if (excess < 0) short_resyncs_++;
    if (excess != 0) return resync_framing(whole);
    frames_waiting_ = static_cast<uint8_t>(whole);
    return Status::Ok;
}

Status Lr2021::resync_framing(uint16_t whole) {
    framing_resyncs_++;
    resync_drops_ += whole;
    return restart_receive();
}

// INFO: fc 11nov26 SoftRF re-arms after 15 s of silence, and a fault resets the part instead
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

// INFO: fc 26nov26 bring-up cleared the reset source, so one seen here is a reset nobody asked for
Status Lr2021::sample(ports::ReceiverProbe& probe) {
    probe = ports::ReceiverProbe{};
    if (mode_ == Lr2021Mode::Sleep || mode_ == Lr2021Mode::Reset) return Status::Invalid;
    uint8_t rssi[2] = {};
    uint8_t stats[6] = {};
    uint8_t errors[2] = {};
    uint32_t threshold = 0;
    uint32_t front_end = 0;
    Status status = read(lr2021::kGetRssiInst, rssi, sizeof(rssi));
    const uint8_t chip_status[2] = {last_status_[0], last_status_[1]};
    if (status == Status::Ok) status = read(lr2021::kGetOokRxStats, stats, sizeof(stats));
    if (status == Status::Ok) status = read(lr2021::kGetErrors, errors, sizeof(errors));
    if (status == Status::Ok) status = read_register(lr2021::kOokThresholdRegister, threshold);
    if (status == Status::Ok) status = read_register(lr2021::kOokQuirkRegister, front_end);
    if (status != Status::Ok) return status;
    probe.reset = reset_of(chip_status[1]);
    if (probe.reset != ports::ReceiverReset::None) {
        part_resets_++;
        // INFO: fc 26nov26 the reset took the OOK modem, so only a bring-up hears again
        reset_owed_ = true;
        status = clear_reset_source();
        if (status != Status::Ok) return status;
    }
    probe.packets = be16(stats);
    probe.errors = be16(errors);
    probe.noise_dbm = rssi_inst_dbm(rssi);
    probe.threshold_dbm = threshold_dbm(threshold);
    probe.mode = mode_of(chip_status[1]);
    probe.gain_step = lr2021::kGainStep;
    probe.boosted = rx_boost_ != lr2021::kRxBoostOff;
    probe.interrupt_pending = (chip_status[0] & lr2021::kInterruptPending) != 0;
    probe.front_end_kept = (front_end & lr2021::kOokQuirkMask) == lr2021::kOokQuirkData;
    probe.valid = true;
    return Status::Ok;
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
        busy_wait_us_ += step_us;
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
    const uint8_t path[2] = {lr2021::kRxPathLowFrequency, rx_boost_};
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
    if (status == Status::Ok) status = command(lr2021::kSetRxPath, path, sizeof(path));
    if (status == Status::Ok) status = command(lr2021::kCalibFe, calibration, sizeof(calibration));
    if (status == Status::Ok) status = command(lr2021::kSetRfFrequency, frequency, 4);
    if (status == Status::Ok) status = configure_ook();
    if (status == Status::Ok) status = read_errors();
    if (status != Status::Ok) return status;
    if ((device_errors_ & lr2021::kFatalErrors) != 0) return Status::Down;
    status = clear_reset_source();
    if (status != Status::Ok) return status;
    mode_ = Lr2021Mode::Standby;
    return Status::Ok;
}

// INFO: fc 27nov26 DS 22.1: SetOokModulationParams sets the part's own threshold, which #550 keeps
Status Lr2021::configure_ook() {
    uint8_t modulation[7] = {};
    put_be32(modulation, lr2021::kBitrateBps);
    modulation[4] = lr2021::kPulseShapeNone;
    modulation[5] = lr2021::kRxBandwidth3076kHz;
    modulation[6] = lr2021::kDepthFull;
    uint8_t packet[6] = {};
    put_be16(packet, lr2021::kPreambleChips);
    packet[2] = lr2021::kFixedLength;
    put_be16(packet + 3, lr2021::kFifoFrameBytes);
    packet[5] = lr2021::kCrcOffManchesterInverted;
    const uint8_t whitening_off[4] = {};
    const uint8_t syncword[5] = {0, 0, 0, 0, lr2021::kSyncwordNoneMsbFirst};
    uint8_t detector[5] = {};
    put_be16(detector, lr2021::kAdsbPreamblePattern);
    detector[2] = lr2021::kAdsbPreambleChips - 1;
    const uint8_t gain = lr2021::kGainStep;

    Status status = command(lr2021::kSetOokModulationParams, modulation, sizeof(modulation));
    if (status == Status::Ok) status = command(lr2021::kSetOokPacketParams, packet, sizeof(packet));
    if (status == Status::Ok)
        status = command(lr2021::kSetOokWhiteningParams, whitening_off, sizeof(whitening_off));
    if (status == Status::Ok) status = command(lr2021::kSetOokSyncword, syncword, sizeof(syncword));
    if (status == Status::Ok) status = command(lr2021::kSetOokDetector, detector, sizeof(detector));
    if (status == Status::Ok) status = command(lr2021::kSetAgcGainManual, &gain, 1);
    if (status == Status::Ok)
        status =
            write_mask(lr2021::kOokQuirkRegister, lr2021::kOokQuirkMask, lr2021::kOokQuirkData);
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
    Status status = begin();
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
    transactions_++;
    spi_.transfer(tx, rx, bytes + 2);
    spi_.select(false);
    note_status(rx);
    last_opcode_ = opcode;
    return Status::Ok;
}

Status Lr2021::read(uint16_t opcode, uint8_t* out, size_t bytes) {
    return read(opcode, nullptr, 0, out, bytes);
}

// INFO: fc 11nov26 DS 5.4.1.2: a read answers in a second frame of zeros, behind two status bytes
Status Lr2021::read(uint16_t opcode, const uint8_t* arguments, size_t argument_bytes, uint8_t* out,
                    size_t bytes) {
    if (bytes + 2 > kFrameBytes) return Status::Invalid;
    const Status status = command(opcode, arguments, argument_bytes);
    if (status != Status::Ok) return status;
    uint8_t tx[kFrameBytes] = {};
    uint8_t rx[kFrameBytes] = {};
    if (wait_busy_low(lr2021::kCommandBusyCeilingUs) != Status::Ok) {
        reset_owed_ = true;
        return Status::Timeout;
    }
    spi_.select(true);
    transactions_++;
    spi_.transfer(tx, rx, bytes + 2);
    spi_.select(false);
    note_status(rx);
    last_opcode_ = lr2021::kNop;
    std::memcpy(out, rx + 2, bytes);
    return Status::Ok;
}

// INFO: fc 26nov26 DS 6.2.3: the address, then a count of one word, which comes back big-endian
Status Lr2021::read_register(uint32_t address, uint32_t& value) {
    const uint8_t arguments[4] = {static_cast<uint8_t>(address >> 16),
                                  static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address),
                                  1};
    uint8_t word[4] = {};
    const Status status = read(lr2021::kReadRegMem32, arguments, sizeof(arguments), word, 4);
    if (status == Status::Ok) value = be32(word);
    return status;
}

Status Lr2021::read_packets(uint16_t& packets) {
    uint8_t stats[6] = {};
    const Status status = read(lr2021::kGetOokRxStats, stats, sizeof(stats));
    if (status == Status::Ok) packets = be16(stats);
    return status;
}

// INFO: fc 26nov26 DS 6.7.1: GetStatus clears the reset source every later status reports
Status Lr2021::clear_reset_source() { return command(lr2021::kGetStatus, nullptr, 0); }

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
    last_status_[0] = status[0];
    last_status_[1] = status[1];
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

// INFO: fc 27nov26 DS 5.4.1.3 and 5.3.1: the FIFO answers in its own frame, any count of bytes long
Status Lr2021::read_frames(uint8_t* frames, uint8_t count, Lr2021Event& event) {
    if (wait_busy_low(lr2021::kCommandBusyCeilingUs) != Status::Ok) {
        reset_owed_ = true;
        return Status::Timeout;
    }
    uint8_t opcode[2] = {};
    uint8_t status[2] = {};
    put_be16(opcode, lr2021::kReadRadioRxFifo);
    spi_.select(true);
    transactions_++;
    spi_.transfer(opcode, status, sizeof(opcode));
    spi_.transfer(nullptr, frames, static_cast<size_t>(count) * lr2021::kFifoFrameBytes);
    spi_.select(false);
    note_status(status);
    last_opcode_ = lr2021::kReadRadioRxFifo;
    event.rssi_valid = frames_waiting_ == count && newest_rssi_valid_;
    event.rssi_dbm = event.rssi_valid ? newest_rssi_dbm_ : 0;
    return Status::Ok;
}

}  // namespace skyblip::parts
