// hardware/parts/sx1262/model.h: a behavioural SX1262 model at the SPI/GPIO seam, so
// the REAL parts::Sx1262 runs against it unchanged: it answers opcodes, raises
// DIO1 on Rx/Tx-done, and can be stuck busy or made to report a CRC error.
#ifndef SKYBLIP_HARDWARE_MODEL_SX1262_H
#define SKYBLIP_HARDWARE_MODEL_SX1262_H

#include <algorithm>
#include <cstring>
#include <vector>

#include "hardware/io/io.h"
#include "hardware/parts/sx1262/sx1262.h"

namespace skyblip::models {

class Sx1262 : public io::Spi, public io::Gpio, public io::Delay {
   public:
    // What this chip refuses to do, and why. The model is the datasheet's half
    // of the contract: the driver either honours it or lands here.
    enum class Fault : uint8_t {
        None,
        ShortReset,
        ConfigOutsideStandby,
        TxWithoutPower,
        FrameWithoutModulation,
        ImageCalibrationTooEarly,
        // Stricter than the silicon on purpose, and it is the DC-DC that is
        // required, not merely a SetRegulatorMode: a real SX1262 runs perfectly
        // well on its LDO and simply draws about twice the current, so there is
        // no symptom to find in a log, only a flat pack. A board that has no
        // inductor on DCC_SW would have to relax this, and would then owe the
        // reason here.
        RegulatorLeftOnLdo,
        // INFO: fc 05sep26 DS 13.1.2: the configuration save takes 500 us and no SPI
        SpiBeforeSleepSettled,
        // INFO: fc 05sep26 DS 13.1.1: SetSleep is a STDBY-only command like the rest
        SleepOutsideStandby,
        // INFO: fc 05sep26 DS 8: a command issued while BUSY is high is a command lost
        CommandWhileBusy,
    };

    // INFO: fc 26sep26 DS 8.1 and 13.1.2, as literals: parts::sx1262's own figures are on trial
    static constexpr uint32_t kResetLowFloorUs = 100;
    static constexpr uint32_t kSleepSettleFloorUs = 500;

    void set(int pin, bool level) override {
        if (pin != reset_pin) return;
        if (!level) {
            reset_low = true;
            reset_low_since_us_ = elapsed_us;
            return;
        }
        if (!reset_low) return;
        reset_low = false;
        reset_low_us = elapsed_us - reset_low_since_us_;
        if (reset_low_us < kResetLowFloorUs) note_fault(Fault::ShortReset);
        reset_pulses++;
        power_on_reset();
    }
    bool get(int pin) override {
        if (pin == busy_pin) return busy_stuck;
        if (pin == dio1_pin) return (irq_flags & dio1_mask) != 0;
        return false;
    }
    void mode_output(int) override {}
    void mode_input(int, bool) override {}
    void wait_at_least_us(uint32_t hold_us) override { elapsed_us += hold_us; }

    void select(bool on) override {
        if (on) {
            if (busy_stuck && !sleeping) note_fault(Fault::CommandWhileBusy);
            // DS 9.3: the falling edge on NSS is the wake-up. Whatever the host
            // meant to send, the first thing it does is bring the part back.
            if (sleeping) {
                slept_us = elapsed_us - slept_since_us_;
                if (slept_us < kSleepSettleFloorUs) note_fault(Fault::SpiBeforeSleepSettled);
                sleeping = false;
                standby = true;
                wakes++;
            }
            sequence_ = 0;
            opcode_ = 0xFF;
        }
    }
    // A part that is not fitted, or whose MISO pad never got soldered, still
    // lets the host clock bytes out and still lets BUSY be read low.
    void transfer(const uint8_t* tx, uint8_t* rx, size_t bytes) override {
        for (size_t i = 0; i < bytes; i++) {
            uint8_t in = tx ? tx[i] : 0;
            uint8_t out = process(in);
            if (rx) rx[i] = miso_dead ? miso_level : out;
            sequence_++;
        }
    }

    // A burst arrives as chips. The detector slides its configured pattern over
    // them, and what the chip reports is the bytes that follow the match: a
    // pattern the transmitter never sent is a frame this radio never saw.
    static uint16_t deliver_after_sync(const uint8_t* chips, uint16_t chip_bytes,
                                       const uint8_t* sync, uint8_t sync_bits, uint8_t* out,
                                       uint16_t capacity) {
        const int total = static_cast<int>(chip_bytes) * 8;
        for (int start = 0; start + sync_bits <= total; start++) {
            bool match = true;
            for (int i = 0; i < sync_bits && match; i++)
                match = bit_at(chips, start + i) == bit_at(sync, i);
            if (!match) continue;
            const int first = start + sync_bits;
            for (uint16_t i = 0; i < capacity; i++) {
                uint8_t byte = 0;
                for (int bit = 0; bit < 8; bit++) {
                    const int index = first + i * 8 + bit;
                    const bool set = index < total && bit_at(chips, index);
                    byte = static_cast<uint8_t>((byte << 1) | (set ? 1u : 0u));
                }
                out[i] = byte;
            }
            return capacity;
        }
        return 0;
    }

    // The chip rate the burst was sent at is part of the burst. A modem framing
    // 100 kbps does not recover a 200 kbps carrier, so a transmitter and a dwell
    // that disagree here are as deaf as a wrong sync word - and a good deal
    // harder to see, because everything else about the dwell looks right.
    bool receive_air(const uint8_t* chips, uint16_t chip_bytes, bool crc_error = false,
                     int8_t frame_rssi_dbm = -80, uint32_t burst_bitrate = 100000) {
        // A modem still on its reset defaults does not frame GFSK at all.
        if (!modulation_set) return false;
        if (bitrate != burst_bitrate) return false;
        if (sync_bits == 0 || payload_bytes == 0) return false;
        uint8_t payload[255];
        const uint16_t delivered =
            deliver_after_sync(chips, chip_bytes, sync, sync_bits, payload, payload_bytes);
        if (delivered == 0) return false;
        queue_rx(payload, static_cast<uint8_t>(delivered), crc_error, frame_rssi_dbm);
        return true;
    }

    void queue_rx(const uint8_t* data, uint8_t bytes, bool crc_error = false,
                  int8_t frame_rssi_dbm = -80) {
        rx_buffer_.assign(data, data + bytes);
        rx_bytes_ = bytes;
        rx_rssi_dbm = frame_rssi_dbm;
        irq_flags = crc_error ? parts::sx1262::kIrqCrcError : parts::sx1262::kIrqRxDone;
    }
    void signal_tx_done() {
        irq_flags = parts::sx1262::kIrqTxDone;
        fall_back();
    }

    void set_rssi_sequence(const int8_t* levels, uint8_t count) {
        rssi_sequence_length = count < kMaxRssiSequence ? count : kMaxRssiSequence;
        rssi_sequence_at = 0;
        for (uint8_t i = 0; i < rssi_sequence_length; i++) rssi_sequence[i] = levels[i];
    }

    // The chip's own SetTx timeout running out. A SetTx issued with timeout 0
    // has nothing to run out, which is the PA left keyed for ever.
    bool expire_tx() {
        if (tx_timeout_ticks == 0) return false;
        irq_flags = parts::sx1262::kIrqTimeout;
        fall_back();
        return true;
    }

    // What the world needs to know to be an honest channel: where this radio is
    // tuned, whether it is listening, and what it just put on the air.
    //
    // INFO: fc 15sep26 DS 6.2.2: the part inserts its sync register on transmit as on receive
    bool take_tx(uint8_t* out, uint8_t& bytes) {
        if (!tx_pending) return false;
        tx_pending = false;
        const size_t inserted = sync_bits / 8u;
        for (size_t i = 0; i < inserted; i++) out[i] = sync[i];
        const size_t sent = payload_bytes != 0 ? payload_bytes : tx_buffer_.size();
        for (size_t i = 0; i < sent; i++)
            out[inserted + i] = i < tx_buffer_.size() ? tx_buffer_[i] : 0;
        bytes = static_cast<uint8_t>(inserted + sent);
        return true;
    }

    bool saw_command(uint8_t opcode) const {
        return std::any_of(commands_seen.begin(), commands_seen.end(),
                           [opcode](uint8_t seen) { return seen == opcode; });
    }

    int busy_pin{0}, reset_pin{1}, dio1_pin{2};
    bool busy_stuck{false};
    bool miso_dead{false};
    uint8_t miso_level{0x00};
    bool sleeping{false};
    uint8_t sleep_config{0xFF};
    uint32_t wakes{0};
    uint32_t frequency_hz{0};
    bool receiving{false};
    bool tx_pending{false};
    // The carrier the radio would measure on the tuned channel right now, and
    // the level the last delivered frame arrived with.
    int8_t rssi_dbm{-110};
    int8_t rx_rssi_dbm{-80};
    // INFO: fc 15sep26 a repeating sequence, one entry per read, is a channel changing mid-window
    static constexpr uint8_t kMaxRssiSequence = 16;
    int8_t rssi_sequence[kMaxRssiSequence]{};
    uint8_t rssi_sequence_length{0};
    uint8_t rssi_sequence_at{0};
    uint8_t sync[8]{};
    uint8_t sync_bits{0};
    uint8_t payload_bytes{0};
    // INFO: fc 16sep26 DS 13.4.6 SetPacketParams, GFSK, in the datasheet's order
    uint8_t packet_parameters[9]{};
    uint16_t irq_flags{0};
    int reset_pulses{0};
    bool reset_low{false};
    uint64_t reset_low_us{0};
    bool standby{false};
    bool regulator_dcdc{false};
    uint8_t regulator_mode{parts::sx1262::kRegulatorLdo};
    // DS 9.6: the receive gain register and the retention list that decides
    // whether it survives a warm start. Reset default is the power-saving gain.
    uint8_t rx_gain{parts::sx1262::kRxGainPowerSaving};
    uint8_t retention_list[3]{};
    // INFO: fc 05sep26 DS table 13-45: the reset values these three come up on
    static constexpr uint8_t kOcpReset = 0x18;
    static constexpr uint8_t kOcpAfterPaConfigSx1262 = 0x38;
    static constexpr uint8_t kTxClampReset = 0xC8;
    static constexpr uint8_t kTxModulationReset = 0x01;
    uint8_t ocp{kOcpReset};
    uint8_t tx_clamp{kTxClampReset};
    uint8_t tx_modulation{kTxModulationReset};
    uint16_t device_errors{0};
    uint16_t fail_calibration{0};
    uint64_t slept_us{0};
    uint64_t elapsed_us{0};
    bool tcxo_on_dio3{false};
    bool tcxo_running{false};
    uint32_t tcxo_starts{0};
    uint8_t fallback_mode{parts::sx1262::kFallbackStandbyOnRcOscillator};
    bool calibrated{false};
    bool image_calibrated{false};
    uint8_t image_band[2]{};
    uint8_t modulation[8]{};
    bool modulation_set{false};
    uint32_t bitrate{0};
    uint32_t deviation_hz{0};
    uint8_t pulse_shape{0xFF};
    uint8_t rx_bandwidth{0xFF};
    uint8_t pa_config[4]{};
    bool pa_set{false};
    bool tx_power_set{false};
    int8_t tx_power_dbm{0};
    uint8_t ramp_time{0xFF};
    uint16_t irq_mask{0};
    uint16_t dio1_mask{0};
    uint32_t tx_timeout_ticks{0};
    Fault fault{Fault::None};
    uint32_t faults{0};
    std::vector<uint8_t> commands_seen;

    // Where an opcode sits in the order it was actually issued, -1 if never.
    int command_order(uint8_t opcode) const {
        for (size_t i = 0; i < commands_seen.size(); i++)
            if (commands_seen[i] == opcode) return static_cast<int>(i);
        return -1;
    }

   private:
    static bool bit_at(const uint8_t* bytes, int index) {
        return ((bytes[index >> 3] >> (7 - (index & 7))) & 1u) != 0;
    }

    int8_t next_rssi_dbm() {
        if (rssi_sequence_length == 0) return rssi_dbm;
        const int8_t level = rssi_sequence[rssi_sequence_at];
        rssi_sequence_at = static_cast<uint8_t>((rssi_sequence_at + 1) % rssi_sequence_length);
        return level;
    }

    // INFO: fc 26sep26 DS 13.3.6: DIO3 powers the TCXO in STDBY_XOSC, FS, TX and RX, not STDBY_RC
    void start_tcxo() {
        if (!tcxo_on_dio3 || tcxo_running) return;
        tcxo_running = true;
        tcxo_starts++;
    }

    void fall_back() {
        standby = fallback_mode != parts::sx1262::kFallbackFrequencySynthesis;
        if (fallback_mode == parts::sx1262::kFallbackStandbyOnRcOscillator) tcxo_running = false;
    }

    void note_fault(Fault raised) {
        if (fault == Fault::None) fault = raised;
        faults++;
    }

    // DS 9.6: a warm start retains the configuration, EXCEPT the registers that
    // are not on the retention list. The receive gain is the one that matters
    // here, and losing it costs 3 dB with nothing in any log to say so.
    bool rx_gain_retained() const {
        return retention_list[0] >= 1 &&
               retention_list[1] == static_cast<uint8_t>(parts::sx1262::kRxGainRegister >> 8) &&
               retention_list[2] == static_cast<uint8_t>(parts::sx1262::kRxGainRegister & 0xFF);
    }

    // NRESET returns every block to its reset default, including the IRQ mask
    // and the modem: everything the driver programmed is gone.
    void power_on_reset() {
        standby = true;
        sleeping = false;
        regulator_dcdc = false;
        regulator_mode = parts::sx1262::kRegulatorLdo;
        rx_gain = parts::sx1262::kRxGainPowerSaving;
        retention_list[0] = retention_list[1] = retention_list[2] = 0;
        ocp = kOcpReset;
        tx_clamp = kTxClampReset;
        tx_modulation = kTxModulationReset;
        tcxo_on_dio3 = tcxo_running = calibrated = image_calibrated = false;
        fallback_mode = parts::sx1262::kFallbackStandbyOnRcOscillator;
        modulation_set = pa_set = tx_power_set = false;
        irq_mask = dio1_mask = 0;
        irq_flags = 0;
        receiving = false;
        tx_timeout_ticks = 0;
    }

    // DS 13.1: these are accepted in standby only. Issued on a chip that is
    // still receiving, they are silently dropped on silicon.
    static bool standby_only(uint8_t opcode) {
        return opcode == parts::sx1262::kSetPacketType ||
               opcode == parts::sx1262::kSetRfFrequency ||
               opcode == parts::sx1262::kSetModulationParameters ||
               opcode == parts::sx1262::kSetPacketParameters ||
               opcode == parts::sx1262::kSetPaConfig || opcode == parts::sx1262::kSetTxParameters ||
               opcode == parts::sx1262::kWriteRegister || opcode == parts::sx1262::kCalibrate ||
               opcode == parts::sx1262::kCalibrateImage ||
               opcode == parts::sx1262::kSetRegulatorMode;
    }

    uint8_t process(uint8_t in) {
        if (sequence_ == 0) {
            opcode_ = in;
            commands_seen.push_back(in);
            if (standby_only(in) && !standby) note_fault(Fault::ConfigOutsideStandby);
            if (opcode_ == parts::sx1262::kClearIrqStatus) irq_flags = 0;
            if (opcode_ == parts::sx1262::kSetStandby) standby = true;
            if (opcode_ == parts::sx1262::kClearDeviceErrors) device_errors = 0;
            if (opcode_ == parts::sx1262::kSetSleep) {
                if (!standby) note_fault(Fault::SleepOutsideStandby);
                sleeping = true;
                slept_since_us_ = elapsed_us;
                standby = false;
                tcxo_running = false;
                receiving = false;
                if (!rx_gain_retained()) rx_gain = parts::sx1262::kRxGainPowerSaving;
            }
            // INFO: fc 05sep26 DS 13.3.6: a TCXO part always flags this, and must be cleared
            if (opcode_ == parts::sx1262::kSetDio3AsTcxoControl) {
                tcxo_on_dio3 = true;
                device_errors = static_cast<uint16_t>(device_errors |
                                                      parts::sx1262::kErrorCrystalOscillatorStart);
            }
            if (opcode_ == parts::sx1262::kCalibrate && !regulator_dcdc)
                note_fault(Fault::RegulatorLeftOnLdo);
            if (opcode_ == parts::sx1262::kCalibrate) {
                calibrated = true;
                device_errors = static_cast<uint16_t>(device_errors | fail_calibration);
            }
            if (opcode_ == parts::sx1262::kCalibrateImage) {
                if (!tcxo_on_dio3 || !calibrated) note_fault(Fault::ImageCalibrationTooEarly);
                image_calibrated = true;
                device_errors = static_cast<uint16_t>(device_errors | fail_calibration);
            }
            if (opcode_ == parts::sx1262::kSetRx) {
                start_tcxo();
                if (!modulation_set) note_fault(Fault::FrameWithoutModulation);
                if (!regulator_dcdc) note_fault(Fault::RegulatorLeftOnLdo);
                receiving = true;
                standby = false;
            }
            if (opcode_ == parts::sx1262::kSetTx) {
                start_tcxo();
                receiving = false;
                standby = false;
                tx_timeout_ticks = 0;
                if (!tx_power_set) {
                    note_fault(Fault::TxWithoutPower);
                } else {
                    tx_pending = true;
                }
                if (!regulator_dcdc) note_fault(Fault::RegulatorLeftOnLdo);
            }
            if (opcode_ == parts::sx1262::kWriteBuffer) tx_buffer_.clear();
            if (opcode_ == parts::sx1262::kWriteRegister || opcode_ == parts::sx1262::kReadRegister)
                register_address_ = 0;
            if (opcode_ == parts::sx1262::kSetRfFrequency) pll_word_ = 0;
            return 0;
        }
        if (opcode_ == parts::sx1262::kWriteBuffer && sequence_ >= 2) {
            tx_buffer_.push_back(in);
            return 0;
        }
        if (opcode_ == parts::sx1262::kSetSleep) {
            if (sequence_ == 1) sleep_config = in;
            return 0;
        }
        if (opcode_ == parts::sx1262::kSetStandby) {
            if (sequence_ == 1 && in == parts::sx1262::kStandbyOnCrystalOscillator) start_tcxo();
            if (sequence_ == 1 && in == parts::sx1262::kStandbyOnRcOscillator) tcxo_running = false;
            return 0;
        }
        if (opcode_ == parts::sx1262::kSetRxTxFallbackMode) {
            if (sequence_ == 1) fallback_mode = in;
            return 0;
        }
        if (opcode_ == parts::sx1262::kSetRegulatorMode) {
            if (sequence_ == 1) {
                regulator_mode = in;
                regulator_dcdc = in == parts::sx1262::kRegulatorDcDc;
            }
            return 0;
        }
        if (opcode_ == parts::sx1262::kReadRegister) {
            if (sequence_ <= 2) {
                register_address_ = static_cast<uint16_t>((register_address_ << 8) | in);
                if (sequence_ == 2) register_offset_ = 0;
                return 0;
            }
            if (sequence_ == 3) return 0;  // the NOP the chip answers nothing to
            const uint16_t address = static_cast<uint16_t>(register_address_ + register_offset_++);
            if (address >= parts::sx1262::kSyncWordRegister &&
                address < parts::sx1262::kSyncWordRegister + sizeof(sync))
                return sync[address - parts::sx1262::kSyncWordRegister];
            if (address == parts::sx1262::kRxGainRegister) return rx_gain;
            if (address == parts::sx1262::kOcpRegister) return ocp;
            if (address == parts::sx1262::kTxClampRegister) return tx_clamp;
            if (address == parts::sx1262::kTxModulationRegister) return tx_modulation;
            if (address >= parts::sx1262::kRetentionListRegister &&
                address < parts::sx1262::kRetentionListRegister + sizeof(retention_list))
                return retention_list[address - parts::sx1262::kRetentionListRegister];
            return 0;
        }
        if (opcode_ == parts::sx1262::kWriteRegister) {
            if (sequence_ <= 2) {
                register_address_ = static_cast<uint16_t>((register_address_ << 8) | in);
                if (sequence_ == 2) register_offset_ = 0;
                return 0;
            }
            const uint16_t address = static_cast<uint16_t>(register_address_ + register_offset_++);
            if (address >= parts::sx1262::kSyncWordRegister &&
                address < parts::sx1262::kSyncWordRegister + sizeof(sync))
                sync[address - parts::sx1262::kSyncWordRegister] = in;
            if (address == parts::sx1262::kRxGainRegister) rx_gain = in;
            if (address == parts::sx1262::kOcpRegister) ocp = in;
            if (address == parts::sx1262::kTxClampRegister) tx_clamp = in;
            if (address == parts::sx1262::kTxModulationRegister) tx_modulation = in;
            if (address >= parts::sx1262::kRetentionListRegister &&
                address < parts::sx1262::kRetentionListRegister + sizeof(retention_list))
                retention_list[address - parts::sx1262::kRetentionListRegister] = in;
            return 0;
        }
        if (opcode_ == parts::sx1262::kSetModulationParameters) {
            if (sequence_ >= 1 && sequence_ <= 8) modulation[sequence_ - 1] = in;
            if (sequence_ == 8) {
                const uint32_t bitrate_word = (static_cast<uint32_t>(modulation[0]) << 16) |
                                              (static_cast<uint32_t>(modulation[1]) << 8) |
                                              modulation[2];
                const uint32_t deviation_word = (static_cast<uint32_t>(modulation[5]) << 16) |
                                                (static_cast<uint32_t>(modulation[6]) << 8) |
                                                modulation[7];
                bitrate =
                    bitrate_word ? static_cast<uint32_t>(32ULL * 32000000ULL / bitrate_word) : 0;
                deviation_hz = static_cast<uint32_t>(
                    (static_cast<uint64_t>(deviation_word) * 32000000ULL) >> 25);
                pulse_shape = modulation[3];
                rx_bandwidth = modulation[4];
                modulation_set = bitrate_word != 0;
            }
            return 0;
        }
        // INFO: fc 05sep26 DS table 5-2: SetPaConfig rewrites OCP, so a driver owes it back
        if (opcode_ == parts::sx1262::kSetPaConfig) {
            if (sequence_ >= 1 && sequence_ <= 4) pa_config[sequence_ - 1] = in;
            if (sequence_ == 4) {
                pa_set = true;
                ocp = kOcpAfterPaConfigSx1262;
            }
            return 0;
        }
        if (opcode_ == parts::sx1262::kSetTxParameters) {
            if (sequence_ == 1) tx_power_dbm = static_cast<int8_t>(in);
            if (sequence_ == 2) {
                ramp_time = in;
                tx_power_set = pa_set;
            }
            return 0;
        }
        if (opcode_ == parts::sx1262::kSetDioIrqParameters) {
            if (sequence_ == 1) irq_mask = in;
            if (sequence_ == 2) irq_mask = static_cast<uint16_t>((irq_mask << 8) | in);
            if (sequence_ == 3) dio1_mask = in;
            if (sequence_ == 4) dio1_mask = static_cast<uint16_t>((dio1_mask << 8) | in);
            return 0;
        }
        if (opcode_ == parts::sx1262::kCalibrateImage) {
            if (sequence_ >= 1 && sequence_ <= 2) image_band[sequence_ - 1] = in;
            return 0;
        }
        if (opcode_ == parts::sx1262::kSetTx) {
            if (sequence_ >= 1 && sequence_ <= 3) tx_timeout_ticks = (tx_timeout_ticks << 8) | in;
            return 0;
        }
        if (opcode_ == parts::sx1262::kSetPacketParameters) {
            if (sequence_ >= 1 && sequence_ <= sizeof(packet_parameters))
                packet_parameters[sequence_ - 1] = in;
            if (sequence_ == 4) sync_bits = in;
            if (sequence_ == 7) payload_bytes = in;
            return 0;
        }
        if (opcode_ == parts::sx1262::kSetRfFrequency) {
            pll_word_ = (pll_word_ << 8) | in;
            // The inverse of the driver's PLL word: frf = freq << 25 / 32 MHz.
            if (sequence_ == 4)
                frequency_hz = static_cast<uint32_t>((pll_word_ * 32000000ULL) >> 25);
            return 0;
        }
        switch (opcode_) {
            // DS 13.3.1: the status register only ever shows unmasked bits.
            case parts::sx1262::kGetIrqStatus:
                if (sequence_ == 2) return static_cast<uint8_t>((irq_flags & irq_mask) >> 8);
                if (sequence_ == 3) return static_cast<uint8_t>(irq_flags & irq_mask);
                return 0;
            case parts::sx1262::kGetDeviceErrors:
                if (sequence_ == 2) return static_cast<uint8_t>(device_errors >> 8);
                if (sequence_ == 3) return static_cast<uint8_t>(device_errors);
                return 0;
            case parts::sx1262::kGetRxBufferStatus:
                if (sequence_ == 2) return rx_bytes_;
                if (sequence_ == 3) return 0;
                return 0;
            case parts::sx1262::kGetInstantRssi:
                if (sequence_ == 2) return static_cast<uint8_t>(-2 * next_rssi_dbm());
                return 0;
            case parts::sx1262::kGetPacketStatus:
                if (sequence_ == 4) return static_cast<uint8_t>(-2 * rx_rssi_dbm);
                return 0;
            case parts::sx1262::kReadBuffer: {
                if (sequence_ >= 3) {
                    size_t index = sequence_ - 3;
                    return index < rx_buffer_.size() ? rx_buffer_[index] : 0;
                }
                return 0;
            }
            default: return 0;
        }
    }

    uint64_t reset_low_since_us_{0};
    uint64_t slept_since_us_{0};
    size_t sequence_{0};
    uint8_t opcode_{0xFF};
    uint16_t register_address_{0};
    uint16_t register_offset_{0};
    uint64_t pll_word_{0};
    std::vector<uint8_t> rx_buffer_;
    std::vector<uint8_t> tx_buffer_;
    uint8_t rx_bytes_{0};
};

}

#endif
