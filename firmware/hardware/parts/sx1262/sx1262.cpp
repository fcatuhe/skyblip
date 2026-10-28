#include "hardware/parts/sx1262/sx1262.h"

#include <algorithm>
#include <cstring>

namespace skyblip::parts {

Status Sx1262::wait_busy_low(uint32_t max_spins) {
    for (uint32_t i = 0; i < max_spins; i++) {
        if (!gpio_.get(busy_pin_)) return Status::Ok;
    }
    return Status::Timeout;
}

// INFO: fc 05sep26 DS 8: a command sent while BUSY is high is a command lost
void Sx1262::select_when_ready() {
    (void)wait_busy_low();
    spi_.select(true);
}

void Sx1262::command(uint8_t opcode, const uint8_t* parameters, size_t bytes) {
    write_frame(&opcode, 1, parameters, bytes);
}

void Sx1262::read_command(uint8_t opcode, uint8_t* out, size_t bytes) {
    const uint8_t head[2] = {opcode, 0};
    read_frame(head, sizeof(head), out, bytes);
}

// INFO: fc 28sep26 one transfer per command: the bench hop's twelve took 794 us, 66 us apiece
void Sx1262::write_frame(const uint8_t* head, size_t head_bytes, const uint8_t* data,
                         size_t bytes) {
    select_when_ready();
    if (head_bytes + bytes <= sizeof(frame_)) {
        std::memcpy(frame_, head, head_bytes);
        if (bytes) std::memcpy(frame_ + head_bytes, data, bytes);
        spi_.transfer(frame_, nullptr, head_bytes + bytes);
    } else {
        spi_.transfer(head, nullptr, head_bytes);
        spi_.transfer(data, nullptr, bytes);
    }
    spi_.select(false);
}

void Sx1262::read_frame(const uint8_t* head, size_t head_bytes, uint8_t* out, size_t bytes) {
    select_when_ready();
    uint8_t tx[kReadFrameBytes]{};
    uint8_t rx[kReadFrameBytes]{};
    if (head_bytes + bytes <= kReadFrameBytes) {
        std::memcpy(tx, head, head_bytes);
        spi_.transfer(tx, rx, head_bytes + bytes);
        std::memcpy(out, rx + head_bytes, bytes);
    } else {
        spi_.transfer(head, nullptr, head_bytes);
        for (size_t i = 0; i < bytes; i++) spi_.transfer(tx, &out[i], 1);
    }
    spi_.select(false);
}

Status Sx1262::enter_standby() { return enter_standby_on(sx1262::kStandbyOnRcOscillator); }

// INFO: fc 27sep26 DS 13.3.6: STDBY_RC unpowers the TCXO, and the next RX waits its whole start
Status Sx1262::enter_standby_on_tcxo() {
    return enter_standby_on(sx1262::kStandbyOnCrystalOscillator);
}

Status Sx1262::enter_standby_on(uint8_t clock) {
    command(sx1262::kSetStandby, &clock, 1);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    mode_ = RadioMode::Standby;
    return Status::Ok;
}

Status Sx1262::reset_to_standby() {
    gpio_.mode_output(reset_pin_);
    gpio_.mode_input(busy_pin_, false);
    gpio_.mode_input(dio1_pin_, false);
    gpio_.set(reset_pin_, false);
    delay_.wait_at_least_us(sx1262::kResetLowUs);
    gpio_.set(reset_pin_, true);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    return enter_standby();
}

// BUSY going low proves a pull-down and a power rail, which a radio that is not
// fitted, not soldered on its MISO pad or held in reset also manages. Only a
// value that came back out of the part proves the part.
Status Sx1262::verify_link() {
    write_register(sx1262::kSyncWordRegister, sx1262::kLinkProbePattern,
                   sizeof(sx1262::kLinkProbePattern));
    uint8_t read_back[sizeof(sx1262::kLinkProbePattern)] = {0};
    read_register(sx1262::kSyncWordRegister, read_back, sizeof(read_back));
    for (size_t i = 0; i < sizeof(read_back); i++)
        if (read_back[i] != sx1262::kLinkProbePattern[i]) return Status::Down;
    return Status::Ok;
}

Status Sx1262::probe() {
    const Status status = reset_to_standby();
    if (status != Status::Ok) return status;
    return verify_link();
}

Status Sx1262::begin() {
    brought_up_ = false;
    configured_ = false;
    tuned_ = false;
    packet_bytes_ = 0;
    const Status reset = reset_to_standby();
    if (reset != Status::Ok) return reset;
    const Status link = verify_link();
    if (link != Status::Ok) return link;

    // First, and before the calibration that runs against it: the reset default
    // is LDO-only. SoftRF opens its own bring-up with the same command
    // (radio-sx126x.c:703).
    uint8_t regulator = sx1262::kRegulatorDcDc;
    command(sx1262::kSetRegulatorMode, &regulator, 1);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;

    // T-Echo (Plus) hardware wiring: DIO2 is the RF/antenna switch and DIO3
    // powers the TCXO at 1.8 V. Without the TCXO control the radio has no clock
    // and will neither TX nor RX. Enable both, then calibrate.
    uint8_t rf_switch_control = 0x01;  // DIO2 controls RF switch
    command(sx1262::kSetDio2AsRfSwitch, &rf_switch_control, 1);
    // SetDIO3AsTcxoCtrl: voltage byte + 3-byte startup delay (~5 ms @ 15.625 us).
    uint8_t tcxo_control[4] = {sx1262::kTcxoVoltage1v8, 0x00, 0x01, 0x40};
    command(sx1262::kSetDio3AsTcxoControl, tcxo_control, 4);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    // INFO: fc 05sep26 DS 13.3.6: a TCXO part raises XOSC_START_ERR here for free
    clear_device_errors();
    uint8_t calibration = 0x7F;  // calibrate all blocks after switching to the TCXO
    command(sx1262::kCalibrate, &calibration, 1);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    // INFO: wr 02aug26 DS 13.1.12: image rejection is calibrated per band and
    // only once the TCXO clock the calibration runs on is the chip's clock.
    command(sx1262::kCalibrateImage, sx1262::kImageBand863to870,
            sizeof(sx1262::kImageBand863to870));
    if (wait_busy_low() != Status::Ok) return Status::Timeout;

    // DS 9.6: without this the boosted receive gain is lost on every warm start,
    // silently and with no symptom but range. Written once, in standby, before
    // the first sleep can happen.
    write_register(sx1262::kRetentionListRegister, sx1262::kRetainRxGain,
                   sizeof(sx1262::kRetainRxGain));
    configure_rx_gain();
    configure_tx_clamp();

    mode_ = RadioMode::Standby;
    const Status errors = check_device_errors();
    brought_up_ = errors == Status::Ok;
    return errors;
}

void Sx1262::clear_device_errors() {
    uint8_t clear[2] = {0, 0};
    command(sx1262::kClearDeviceErrors, clear, sizeof(clear));
}

Status Sx1262::check_device_errors() {
    uint8_t errors[2] = {0, 0};
    read_command(sx1262::kGetDeviceErrors, errors, 2);
    device_errors_ =
        static_cast<uint16_t>(((errors[0] << 8) | errors[1]) & sx1262::kDeviceErrorMask);
    if (device_errors_ == 0) return Status::Ok;
    clear_device_errors();
    return Status::Down;
}

void Sx1262::write_register(uint16_t address, const uint8_t* data, size_t bytes) {
    const uint8_t head[3] = {sx1262::kWriteRegister, static_cast<uint8_t>(address >> 8),
                             static_cast<uint8_t>(address)};
    write_frame(head, sizeof(head), data, bytes);
}

// DS 13.2.2 ReadRegister: opcode, address, one NOP the chip answers nothing to,
// then the bytes.
void Sx1262::read_register(uint16_t address, uint8_t* out, size_t bytes) {
    const uint8_t head[4] = {sx1262::kReadRegister, static_cast<uint8_t>(address >> 8),
                             static_cast<uint8_t>(address), 0};
    read_frame(head, sizeof(head), out, bytes);
}

namespace {

// The narrowest entry of DS 13.4.6's table that still passes the channel, or
// the widest the part has if nothing does.
uint8_t rx_bandwidth_index(uint32_t bandwidth_hz) {
    for (int i = 0; i < sx1262::kRxBandwidthCount; i++)
        if (sx1262::kRxBandwidths[i].bandwidth_hz >= bandwidth_hz)
            return sx1262::kRxBandwidths[i].index;
    return sx1262::kRxBandwidths[sx1262::kRxBandwidthCount - 1].index;
}

// DS 13.4.6 pulse shape: an index, not a number, so the BT the caller asked for
// is matched to the one the part actually has.
uint8_t pulse_shape_index(uint16_t gaussian_bt_e2) {
    if (gaussian_bt_e2 == 0) return sx1262::kPulseShapeNone;
    if (gaussian_bt_e2 <= 30) return sx1262::kGaussianBt0p3;
    if (gaussian_bt_e2 <= 50) return sx1262::kGaussianBt0p5;
    if (gaussian_bt_e2 <= 70) return sx1262::kGaussianBt0p7;
    return sx1262::kGaussianBt1p0;
}

bool same_sync(const RadioConfig& left, const RadioConfig& right) {
    if (left.sync_bits != right.sync_bits) return false;
    const size_t bytes = (left.sync_bits + 7u) / 8u;
    if (bytes == 0) return true;
    return left.sync != nullptr && right.sync != nullptr &&
           std::equal(left.sync, left.sync + bytes, right.sync);
}

bool same_modem(const RadioConfig& left, const RadioConfig& right) {
    return left.bitrate == right.bitrate && left.deviation_hz == right.deviation_hz &&
           left.bandwidth_hz == right.bandwidth_hz && left.gaussian_bt_e2 == right.gaussian_bt_e2 &&
           left.payload_bytes == right.payload_bytes && same_sync(left, right);
}

bool same_dwell(const RadioConfig& left, const RadioConfig& right) {
    return left.frequency_hz == right.frequency_hz &&
           left.frequency_correction_e1_ppm == right.frequency_correction_e1_ppm &&
           same_modem(left, right);
}

}  // namespace

// INFO: fc 26sep26 DS 9.6: a warm start keeps only listed registers, so a slept part is rewritten
bool Sx1262::holds(const RadioConfig& config) const {
    return tuned_ && mode_ != RadioMode::Tx && same_dwell(config, config_);
}

bool Sx1262::hops_to(const RadioConfig& config) const {
    return tuned_ && mode_ != RadioMode::Tx && same_modem(config, config_);
}

// DS 13.4.6 SetModulationParams, GFSK, in the datasheet's order: bit rate,
// pulse shape, RX bandwidth, frequency deviation.
void Sx1262::configure_modulation(const RadioConfig& config) {
    const uint32_t bitrate = config.bitrate != 0 ? config.bitrate : 100000u;
    const uint32_t bitrate_word = static_cast<uint32_t>(32ULL * sx1262::kCrystalHz / bitrate);
    const uint32_t deviation_word = static_cast<uint32_t>(
        (static_cast<uint64_t>(config.deviation_hz) << 25) / sx1262::kCrystalHz);
    uint8_t parameters[8];
    parameters[0] = static_cast<uint8_t>(bitrate_word >> 16);
    parameters[1] = static_cast<uint8_t>(bitrate_word >> 8);
    parameters[2] = static_cast<uint8_t>(bitrate_word);
    parameters[3] = pulse_shape_index(config.gaussian_bt_e2);
    parameters[4] = rx_bandwidth_index(config.bandwidth_hz);
    parameters[5] = static_cast<uint8_t>(deviation_word >> 16);
    parameters[6] = static_cast<uint8_t>(deviation_word >> 8);
    parameters[7] = static_cast<uint8_t>(deviation_word);
    command(sx1262::kSetModulationParameters, parameters, sizeof(parameters));
}

// About 2 mA for about 3 dB, taken deliberately (sx1262.h, kRxGainBoosted).
void Sx1262::configure_rx_gain() {
    const uint8_t gain = sx1262::kRxGainBoosted;
    write_register(sx1262::kRxGainRegister, &gain, 1);
}

void Sx1262::configure_tx_clamp() {
    uint8_t clamp = 0;
    read_register(sx1262::kTxClampRegister, &clamp, 1);
    clamp = static_cast<uint8_t>(clamp | sx1262::kTxClampWidenBits);
    write_register(sx1262::kTxClampRegister, &clamp, 1);
}

void Sx1262::configure_tx_modulation() {
    uint8_t modulation = 0;
    read_register(sx1262::kTxModulationRegister, &modulation, 1);
    modulation = static_cast<uint8_t>(modulation | sx1262::kTxModulationGfskBit);
    write_register(sx1262::kTxModulationRegister, &modulation, 1);
}

void Sx1262::configure_power() {
    command(sx1262::kSetPaConfig, sx1262::kPaConfigHighPower, sizeof(sx1262::kPaConfigHighPower));
    uint8_t parameters[2] = {static_cast<uint8_t>(sx1262::kConductedDbm), sx1262::kRampTime200Us};
    command(sx1262::kSetTxParameters, parameters, sizeof(parameters));
    const uint8_t ocp = sx1262::kOcpLimitSteps;
    write_register(sx1262::kOcpRegister, &ocp, 1);
}

// DS 13.3.1 SetDioIrqParams: the global mask, then one mask per DIO. DIO1 is the
// only interrupt line wired on this board.
void Sx1262::configure_irq() {
    const uint8_t mask_high = static_cast<uint8_t>(sx1262::kIrqMask >> 8);
    const uint8_t mask_low = static_cast<uint8_t>(sx1262::kIrqMask);
    uint8_t parameters[8] = {mask_high, mask_low, mask_high, mask_low, 0, 0, 0, 0};
    command(sx1262::kSetDioIrqParameters, parameters, sizeof(parameters));
}

void Sx1262::configure_frame(const RadioConfig& config) {
    if (config.sync_bits == 0) return;
    write_register(sx1262::kSyncWordRegister, config.sync, (config.sync_bits + 7u) / 8u);
    write_packet_parameters(config.payload_bytes);
}

// DS 13.4.6 SetPacketParams, GFSK, in the datasheet's order.
void Sx1262::write_packet_parameters(uint8_t payload_bytes) {
    uint8_t parameters[9] = {0};
    parameters[0] = static_cast<uint8_t>(sx1262::kPreambleChips >> 8);
    parameters[1] = static_cast<uint8_t>(sx1262::kPreambleChips);
    parameters[2] = sx1262::kPreambleDetect8Chips;
    parameters[3] = config_.sync_bits;
    parameters[4] = sx1262::kAddressFilteringOff;
    parameters[5] = sx1262::kFixedLength;
    parameters[6] = payload_bytes;
    parameters[7] = sx1262::kCrcOff;
    parameters[8] = sx1262::kWhiteningOff;
    command(sx1262::kSetPacketParameters, parameters, sizeof(parameters));
    packet_bytes_ = payload_bytes;
}

// INFO: wr 02aug26 DS 13.1: SetPacketType and SetRfFrequency are standby-only
// commands, and the previous dwell leaves the chip in continuous RX. Bracket the
// whole sequence and hand the caller back the mode it had.
Status Sx1262::configure_radio(const RadioConfig& config) {
    if (!brought_up_) return Status::Down;
    if (holds(config)) return Status::Ok;
    const RadioMode was = mode_;
    if (hops_to(config)) return retune(config, was);
    if (was != RadioMode::Standby && enter_standby_on_tcxo() != Status::Ok) return Status::Timeout;
    config_ = config;
    uint8_t packet_type = 0x00;
    command(sx1262::kSetPacketType, &packet_type, 1);
    write_frequency(config);
    const uint8_t fallback = sx1262::kFallbackStandbyOnCrystalOscillator;
    command(sx1262::kSetRxTxFallbackMode, &fallback, 1);
    configure_power();
    configure_modulation(config);
    configure_rx_gain();
    configure_tx_modulation();
    configure_frame(config);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    configured_ = true;
    tuned_ = true;
    if (was == RadioMode::Rx) return start_receive();
    return Status::Ok;
}

// The same modem on another channel, which is the M-band hop between the two dwells.
Status Sx1262::retune(const RadioConfig& config, RadioMode was) {
    if (was != RadioMode::Standby && enter_standby_on_tcxo() != Status::Ok) return Status::Timeout;
    config_ = config;
    write_frequency(config);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    if (was == RadioMode::Rx) return start_receive();
    return Status::Ok;
}

// The reference's own error is taken out here, at the one place the PLL word
// is computed, so receive and transmit are trimmed by construction and
// nothing downstream carries a second copy of the correction.
void Sx1262::write_frequency(const RadioConfig& config) {
    const uint32_t tuned_hz =
        sx1262::trimmed_hz(config.frequency_hz, config.frequency_correction_e1_ppm);
    uint64_t pll_word = (static_cast<uint64_t>(tuned_hz) << 25) / 32000000ULL;
    uint8_t pll_bytes[4] = {static_cast<uint8_t>(pll_word >> 24),
                            static_cast<uint8_t>(pll_word >> 16),
                            static_cast<uint8_t>(pll_word >> 8), static_cast<uint8_t>(pll_word)};
    command(sx1262::kSetRfFrequency, pll_bytes, 4);
}

// Preamble, sync window and payload at the configured bit rate, plus the margin
// SoftRF uses to decide a burst is never going to end (almic.cpp:510).
uint32_t Sx1262::tx_timeout_ticks(uint8_t bytes) const {
    const uint64_t ticks = (static_cast<uint64_t>(air_us(bytes)) + sx1262::kTxGuardUs) * 1000ULL /
                           sx1262::kTimeoutStepNs;
    return ticks > sx1262::kTimeoutTicksCeiling ? sx1262::kTimeoutTicksCeiling
                                                : static_cast<uint32_t>(ticks);
}

uint32_t Sx1262::air_us(uint8_t bytes) const {
    const uint32_t bitrate = config_.bitrate != 0 ? config_.bitrate : 100000u;
    const uint32_t bits =
        sx1262::kPreambleChips + config_.sync_bits + static_cast<uint32_t>(bytes) * 8u;
    return static_cast<uint32_t>(static_cast<uint64_t>(bits) * 1000000ULL / bitrate);
}

Status Sx1262::transmit(const uint8_t* data, uint8_t bytes) {
    const Status staged = stage_tx(data, bytes);
    if (staged != Status::Ok) return staged;
    return key_tx();
}

// INFO: fc 28sep26 DS 13.1: SetPacketParams is standby-only, so the burst's length leaves RX
Status Sx1262::stage_tx(const uint8_t* data, uint8_t bytes) {
    if (!configured_) return Status::Invalid;
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    if (mode_ != RadioMode::Standby && enter_standby_on_tcxo() != Status::Ok)
        return Status::Timeout;
    if (packet_bytes_ != bytes) write_packet_parameters(bytes);
    const uint8_t head[2] = {sx1262::kWriteBuffer, 0};
    write_frame(head, sizeof(head), data, bytes);
    configure_irq();
    staged_bytes_ = bytes;
    return Status::Ok;
}

Status Sx1262::key_tx() {
    if (staged_bytes_ == 0) return Status::Invalid;
    const uint32_t ticks = tx_timeout_ticks(staged_bytes_);
    uint8_t timeout[3] = {static_cast<uint8_t>(ticks >> 16), static_cast<uint8_t>(ticks >> 8),
                          static_cast<uint8_t>(ticks)};
    command(sx1262::kSetTx, timeout, 3);
    mode_ = RadioMode::Tx;
    staged_bytes_ = 0;
    return Status::Ok;
}

Status Sx1262::start_receive() {
    if (!configured_) return Status::Invalid;
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    if (config_.sync_bits != 0 && packet_bytes_ != config_.payload_bytes) {
        if (mode_ != RadioMode::Standby && enter_standby_on_tcxo() != Status::Ok)
            return Status::Timeout;
        write_packet_parameters(config_.payload_bytes);
    }
    configure_irq();
    uint8_t continuous[3] = {0xFF, 0xFF, 0xFF};
    command(sx1262::kSetRx, continuous, 3);
    mode_ = RadioMode::Rx;
    return Status::Ok;
}

// DS 9.3: warm start keeps the configuration in retention across sleep, so what
// comes back is the radio that went to sleep and not a chip out of reset.
// INFO: fc 05sep26 DS 13.1.1: SetSleep is accepted in STDBY only, and a dwell ends in RX
void Sx1262::sleep() {
    if (mode_ != RadioMode::Standby && enter_standby() != Status::Ok) return;
    uint8_t sleep_config = sx1262::kSleepWarmStartNoRtc;
    command(sx1262::kSetSleep, &sleep_config, 1);
    mode_ = RadioMode::Sleep;
    tuned_ = false;
    delay_.wait_at_least_us(sx1262::kSleepSettleUs);
}

// DS 9.3: a falling edge on NSS is what wakes the part; it comes back in
// STDBY_RC with everything the warm start retained.
Status Sx1262::wake() {
    if (mode_ != RadioMode::Sleep) return Status::Ok;
    spi_.select(true);
    spi_.select(false);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    return enter_standby();
}

// DS 13.5.2: the chip reports -2x the signal level in dBm, one byte.
int8_t Sx1262::instant_rssi_dbm() {
    uint8_t reported = 0;
    read_command(sx1262::kGetInstantRssi, &reported, 1);
    return static_cast<int8_t>(-(static_cast<int>(reported) / 2));
}

// INFO: fc 16sep26 DS 13.5.3 GetPacketStatus in GFSK: RxStatus, RssiSync, RssiAvg
int8_t Sx1262::packet_rssi_dbm() {
    uint8_t packet_status[3] = {0, 0, 0};
    read_command(sx1262::kGetPacketStatus, packet_status, 3);
    return static_cast<int8_t>(-(static_cast<int>(packet_status[2]) / 2));
}

uint8_t Sx1262::read_payload(uint8_t* out, uint8_t capacity) {
    uint8_t buffer_status[2];
    read_command(sx1262::kGetRxBufferStatus, buffer_status, 2);
    uint8_t bytes = buffer_status[0];
    bytes = std::min(bytes, capacity);
    select_when_ready();
    uint8_t header[3] = {sx1262::kReadBuffer, buffer_status[1], 0};
    spi_.transfer(header, nullptr, sizeof(header));
    for (int read = 0; read < bytes; read += sx1262::kReadChunkBytes) {
        const int remaining = bytes - read;
        spi_.transfer(
            nullptr, out + read,
            static_cast<size_t>(remaining < sx1262::kReadChunkBytes ? remaining
                                                                    : sx1262::kReadChunkBytes));
    }
    spi_.select(false);
    return bytes;
}

RadioEvent Sx1262::poll(uint8_t* out, uint8_t capacity) {
    RadioEvent event{};
    if (wait_busy_low(10000) != Status::Ok) {
        event.type = RadioEventType::Fault;
        return event;
    }
    uint8_t irq_status[2];
    read_command(sx1262::kGetIrqStatus, irq_status, 2);
    uint16_t flags = static_cast<uint16_t>((irq_status[0] << 8) | irq_status[1]);
    if (flags == 0) return event;

    uint8_t cleared[2] = {irq_status[0], irq_status[1]};
    command(sx1262::kClearIrqStatus, cleared, 2);

    if (flags & sx1262::kIrqTxDone) {
        event.type = RadioEventType::TxDone;
        return event;
    }
    if (flags & sx1262::kIrqTimeout) {
        event.type = RadioEventType::Timeout;
        if (mode_ == RadioMode::Tx) recover_tx();
        return event;
    }
    if (flags & sx1262::kIrqCrcError) {
        event.type = RadioEventType::CrcError;
        event.rssi_dbm = packet_rssi_dbm();
        event.rssi_valid = true;
        return event;
    }
    if (flags & sx1262::kIrqRxDone) {
        event.bytes = read_payload(out, capacity);
        event.rssi_dbm = packet_rssi_dbm();
        event.rssi_valid = true;
        event.type = RadioEventType::RxDone;
        since_rx_ms_ = 0;
    }
    return event;
}

// The PA is keyed until something unkeys it. The chip's own SetTx timeout is
// what notices; this is what puts the radio back where the dwell expects it.
void Sx1262::recover_tx() {
    tx_recovery_count_++;
    if (enter_standby_on_tcxo() != Status::Ok) return;
    (void)start_receive();
}

Status Sx1262::reinitialise() {
    reinitialisations_++;
    Status status = begin();
    if (status != Status::Ok) return status;
    status = configure_radio(config_);
    if (status != Status::Ok) return status;
    return start_receive();
}

// INFO: fc 23sep26 a failed reinitialisation leaves no Rx to wait in, so the rope runs on, retrying
bool Sx1262::service(uint32_t elapsed_ms, uint32_t rx_silence_ceiling_ms) {
    if (mode_ != RadioMode::Rx && !reinitialisation_owed_) return false;
    since_rx_ms_ += elapsed_ms;
    if (since_rx_ms_ < rx_silence_ceiling_ms) return false;
    since_rx_ms_ = 0;
    reinitialisation_owed_ = reinitialise() != Status::Ok;
    return true;
}

}
