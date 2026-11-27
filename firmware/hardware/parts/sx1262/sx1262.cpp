#include "hardware/parts/sx1262/sx1262.h"

#include <algorithm>
#include <cstring>

namespace skyblip::parts {

// INFO: fc 26nov26 a part stuck outside RX counts no silence, so a BUSY timeout owes it a reset
Status Sx1262::wait_busy_low(uint32_t max_spins) {
    for (uint32_t i = 0; i < max_spins; i++) {
        if (!gpio_.get(busy_pin_)) return Status::Ok;
    }
    if (brought_up_) reinitialisation_owed_ = true;
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

// INFO: fc 20nov26 an absent radio still lets BUSY fall, so only a register read back proves one
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

    // INFO: fc 20nov26 the part resets on its LDO, so select DC-DC before calibrating against it
    uint8_t regulator = sx1262::kRegulatorDcDc;
    command(sx1262::kSetRegulatorMode, &regulator, 1);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;

    // INFO: fc 20nov26 DIO2 switches the antenna, and DIO3 powers the TCXO the part needs to run
    uint8_t rf_switch_control = 0x01;
    command(sx1262::kSetDio2AsRfSwitch, &rf_switch_control, 1);
    // INFO: fc 20nov26 DS 13.3.6: a 3-byte start delay of 0x140 steps of 15.625 us is 5 ms
    uint8_t tcxo_control[4] = {sx1262::kTcxoVoltage1v8, 0x00, 0x01, 0x40};
    command(sx1262::kSetDio3AsTcxoControl, tcxo_control, 4);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    // INFO: fc 05sep26 DS 13.3.6: a TCXO part raises XOSC_START_ERR here for free
    clear_device_errors();
    // INFO: fc 20nov26 0x7F calibrates every block, now on the TCXO's clock
    uint8_t calibration = 0x7F;
    command(sx1262::kCalibrate, &calibration, 1);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    // INFO: wr 02aug26 DS 13.1.12: calibrate the image per band, once the TCXO clocks the part
    command(sx1262::kCalibrateImage, sx1262::kImageBand863to870,
            sizeof(sx1262::kImageBand863to870));
    if (wait_busy_low() != Status::Ok) return Status::Timeout;

    // INFO: fc 20nov26 DS 9.6: listed before the first sleep, or every warm start loses the boost
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

// INFO: fc 20nov26 DS 13.2.2: one NOP byte sits between the address and the data
void Sx1262::read_register(uint16_t address, uint8_t* out, size_t bytes) {
    const uint8_t head[4] = {sx1262::kReadRegister, static_cast<uint8_t>(address >> 8),
                             static_cast<uint8_t>(address), 0};
    read_frame(head, sizeof(head), out, bytes);
}

namespace {

uint8_t rx_bandwidth_index(uint32_t bandwidth_hz) {
    for (int i = 0; i < sx1262::kRxBandwidthCount; i++)
        if (sx1262::kRxBandwidths[i].bandwidth_hz >= bandwidth_hz)
            return sx1262::kRxBandwidths[i].index;
    return sx1262::kRxBandwidths[sx1262::kRxBandwidthCount - 1].index;
}

uint8_t pulse_shape_index(uint16_t gaussian_bt_e2) {
    if (gaussian_bt_e2 == 0) return sx1262::kPulseShapeNone;
    if (gaussian_bt_e2 <= 30) return sx1262::kGaussianBt0p3;
    if (gaussian_bt_e2 <= 50) return sx1262::kGaussianBt0p5;
    if (gaussian_bt_e2 <= 70) return sx1262::kGaussianBt0p7;
    return sx1262::kGaussianBt1p0;
}

bool same_sync(const uint8_t* left, uint8_t left_bits, const uint8_t* right, uint8_t right_bits) {
    if (left_bits != right_bits) return false;
    if (left_bits == 0) return true;
    const size_t bytes = (left_bits + 7u) / 8u;
    return std::equal(left, left + bytes, right);
}

bool same_modem(const RadioConfig& left, const RadioConfig& right) {
    return left.bitrate == right.bitrate && left.deviation_hz == right.deviation_hz &&
           left.bandwidth_hz == right.bandwidth_hz && left.gaussian_bt_e2 == right.gaussian_bt_e2 &&
           left.payload_bytes == right.payload_bytes &&
           same_sync(left.sync, left.sync_bits, right.sync, right.sync_bits) &&
           left.tx_preamble_chips == right.tx_preamble_chips &&
           same_sync(left.tx_sync, left.tx_sync_bits, right.tx_sync, right.tx_sync_bits);
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

// INFO: fc 20nov26 DS 13.3.1: the global mask, then DIO1's, the only line wired, then the other two
void Sx1262::configure_irq() {
    const uint8_t mask_high = static_cast<uint8_t>(sx1262::kIrqMask >> 8);
    const uint8_t mask_low = static_cast<uint8_t>(sx1262::kIrqMask);
    uint8_t parameters[8] = {mask_high, mask_low, mask_high, mask_low, 0, 0, 0, 0};
    command(sx1262::kSetDioIrqParameters, parameters, sizeof(parameters));
}

void Sx1262::configure_frame(const RadioConfig& config) {
    tx_framing_ = false;
    if (config.sync_bits == 0) return;
    write_register(sx1262::kSyncWordRegister, config.sync, (config.sync_bits + 7u) / 8u);
    write_packet_parameters(config.payload_bytes, sx1262::kPreambleChips, config.sync_bits);
}

void Sx1262::write_packet_parameters(uint8_t payload_bytes, uint16_t preamble_chips,
                                     uint8_t sync_bits) {
    uint8_t parameters[9] = {0};
    parameters[0] = static_cast<uint8_t>(preamble_chips >> 8);
    parameters[1] = static_cast<uint8_t>(preamble_chips);
    parameters[2] = sx1262::kPreambleDetect8Chips;
    parameters[3] = sync_bits;
    parameters[4] = sx1262::kAddressFilteringOff;
    parameters[5] = sx1262::kFixedLength;
    parameters[6] = payload_bytes;
    parameters[7] = sx1262::kCrcOff;
    parameters[8] = sx1262::kWhiteningOff;
    command(sx1262::kSetPacketParameters, parameters, sizeof(parameters));
    packet_bytes_ = payload_bytes;
}

// INFO: wr 02aug26 DS 13.1: SetPacketType and SetRfFrequency need standby, so the mode is restored
Status Sx1262::configure_radio(const RadioConfig& config) {
    if (!brought_up_) return Status::Down;
    if (config.sync_bits > sx1262::kSyncWordBitsCeiling ||
        (config.sync_bits != 0 && config.sync == nullptr) ||
        config.tx_sync_bits > sx1262::kSyncWordBitsCeiling ||
        (config.tx_sync_bits != 0 &&
         (config.tx_sync == nullptr || config.tx_preamble_chips == 0 || config.sync_bits == 0)))
        return Status::Invalid;
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

Status Sx1262::retune(const RadioConfig& config, RadioMode was) {
    if (was != RadioMode::Standby && enter_standby_on_tcxo() != Status::Ok) return Status::Timeout;
    config_ = config;
    write_frequency(config);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    if (was == RadioMode::Rx) return start_receive();
    return Status::Ok;
}

// INFO: fc 20nov26 the one place the PLL word is computed, so receive and transmit share the trim
void Sx1262::write_frequency(const RadioConfig& config) {
    const uint32_t tuned_hz =
        sx1262::trimmed_hz(config.frequency_hz, config.frequency_correction_e1_ppm);
    uint64_t pll_word = (static_cast<uint64_t>(tuned_hz) << 25) / 32000000ULL;
    uint8_t pll_bytes[4] = {static_cast<uint8_t>(pll_word >> 24),
                            static_cast<uint8_t>(pll_word >> 16),
                            static_cast<uint8_t>(pll_word >> 8), static_cast<uint8_t>(pll_word)};
    command(sx1262::kSetRfFrequency, pll_bytes, 4);
}

// INFO: fc 20nov26 kTxGuardUs is SoftRF's margin for a burst that never ends (almic.cpp:510)
uint32_t Sx1262::tx_timeout_ticks(uint8_t bytes) const {
    const uint64_t ticks = (static_cast<uint64_t>(air_us(bytes)) + sx1262::kTxGuardUs) * 1000ULL /
                           sx1262::kTimeoutStepNs;
    return ticks > sx1262::kTimeoutTicksCeiling ? sx1262::kTimeoutTicksCeiling
                                                : static_cast<uint32_t>(ticks);
}

uint32_t Sx1262::air_us(uint8_t bytes) const {
    const uint32_t bitrate = config_.bitrate != 0 ? config_.bitrate : 100000u;
    const bool custom_tx = config_.tx_sync_bits != 0;
    const uint16_t preamble = custom_tx ? config_.tx_preamble_chips : sx1262::kPreambleChips;
    const uint8_t sync_bits = custom_tx ? config_.tx_sync_bits : config_.sync_bits;
    const uint32_t bits = preamble + sync_bits + static_cast<uint32_t>(bytes) * 8u;
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
    const bool custom_tx = config_.tx_sync_bits != 0;
    const bool change_framing = custom_tx && !tx_framing_;
    if (change_framing)
        write_register(sx1262::kSyncWordRegister, config_.tx_sync,
                       (config_.tx_sync_bits + 7u) / 8u);
    if (packet_bytes_ != bytes || change_framing)
        write_packet_parameters(bytes,
                                custom_tx ? config_.tx_preamble_chips : sx1262::kPreambleChips,
                                custom_tx ? config_.tx_sync_bits : config_.sync_bits);
    tx_framing_ = custom_tx;
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
    if (config_.sync_bits != 0 && (tx_framing_ || packet_bytes_ != config_.payload_bytes)) {
        if (mode_ != RadioMode::Standby && enter_standby_on_tcxo() != Status::Ok)
            return Status::Timeout;
        if (tx_framing_)
            configure_frame(config_);
        else
            write_packet_parameters(config_.payload_bytes, sx1262::kPreambleChips,
                                    config_.sync_bits);
    }
    configure_irq();
    uint8_t continuous[3] = {0xFF, 0xFF, 0xFF};
    command(sx1262::kSetRx, continuous, 3);
    mode_ = RadioMode::Rx;
    return Status::Ok;
}

// INFO: fc 05sep26 DS 13.1.1: SetSleep is accepted in STDBY only, and a dwell ends in RX
void Sx1262::sleep() {
    if (mode_ != RadioMode::Standby && enter_standby() != Status::Ok) return;
    uint8_t sleep_config = sx1262::kSleepWarmStartNoRtc;
    command(sx1262::kSetSleep, &sleep_config, 1);
    mode_ = RadioMode::Sleep;
    tuned_ = false;
    delay_.wait_at_least_us(sx1262::kSleepSettleUs);
}

// INFO: fc 20nov26 DS 9.3: an NSS falling edge wakes the part into STDBY_RC
Status Sx1262::wake() {
    if (mode_ != RadioMode::Sleep) return Status::Ok;
    spi_.select(true);
    spi_.select(false);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    return enter_standby();
}

// INFO: fc 20nov26 DS 13.5.2: the part reports minus twice the level in dBm
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
