#ifndef SKYBLIP_HARDWARE_MODEL_LR2021_H
#define SKYBLIP_HARDWARE_MODEL_LR2021_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <map>
#include <vector>

#include "hardware/io/io.h"
#include "hardware/parts/lr2021/lr2021.h"

namespace skyblip::models {

class Lr2021 : public io::Spi, public io::Gpio, public io::Delay {
   public:
    enum class Fault : uint8_t { None, ShortReset, CommandWhileBusy, CommandWhileAsleep };
    enum class Mode : uint8_t { Sleep, StandbyRc, StandbyXosc, Receive };

    // INFO: fc 11nov26 DS 4.2 and 5.3 as literals, because parts::lr2021's own figures are on trial
    static constexpr uint32_t kResetLowFloorUs = 100;
    static constexpr size_t kFifoBytes = 256;
    static constexpr uint8_t kCommandOk = 0x02;
    static constexpr uint8_t kCommandData = 0x03;
    static constexpr uint8_t kResetSourcePin = 0x02;
    static constexpr uint8_t kFifoFull = 0x08;
    static constexpr uint8_t kFifoOverflow = 0x10;
    static constexpr uint32_t kSleepableHoldUs = 1000;

    void set(int pin, bool level) override {
        if (pin != reset_pin) return;
        if (!level) {
            in_reset = true;
            reset_low_since_us_ = elapsed_us;
            return;
        }
        if (!in_reset) return;
        in_reset = false;
        if (elapsed_us - reset_low_since_us_ < kResetLowFloorUs) note_fault(Fault::ShortReset);
        reset_pulses++;
        busy_stuck = busy_stuck && !busy_stuck_until_reset;
        power_on_reset();
    }
    bool get(int pin) override {
        if (pin == busy_pin) return busy();
        if (pin == irq_pin) return irq_line();
        return false;
    }
    void mode_output(int) override {}
    void mode_input(int, bool) override {}
    // INFO: fc 11nov26 a Zephyr Delay spins through a hold under a tick, so short holds are counted
    // apart
    void wait_at_least_us(uint32_t hold_us) override {
        elapsed_us += hold_us;
        if (hold_us < kSleepableHoldUs) spun_us += hold_us;
    }

    void select(bool on) override {
        if (on) {
            if (mode == Mode::Sleep) note_fault(Fault::CommandWhileAsleep);
            if (busy()) note_fault(Fault::CommandWhileBusy);
            mosi_.clear();
            response_frame_ = response_pending_;
            return;
        }
        if (response_frame_) {
            response_pending_ = false;
            command_status_ = kCommandData;
            return;
        }
        execute();
        land_arrivals();
    }

    void transfer(const uint8_t* tx, uint8_t* rx, size_t bytes) override {
        for (size_t i = 0; i < bytes; i++) {
            const uint8_t out = miso(mosi_.size());
            mosi_.push_back(tx ? tx[i] : 0);
            if (rx) rx[i] = miso_dead ? 0 : out;
        }
    }

    // INFO: fc 11nov26 DS 16.3.8: the pattern arrives first bit first, then the sync word, then the
    // payload
    bool receive_air(const uint8_t* chips, size_t chip_bytes, int8_t rssi_dbm = -70) {
        if (!listening_for_adsb()) return false;
        std::vector<bool> wanted;
        wanted.reserve(static_cast<size_t>(pattern_length) + static_cast<size_t>(sync_bits));
        for (int bit = 0; bit < pattern_length; bit++) wanted.push_back((pattern >> bit) & 1u);
        for (int bit = sync_bits - 1; bit >= 0; bit--) wanted.push_back((sync >> bit) & 1u);
        const size_t total = chip_bytes * 8;
        for (size_t start = 0; start + wanted.size() <= total; start++) {
            bool match = true;
            for (size_t i = 0; i < wanted.size() && match; i++)
                match = bit_at(chips, start + i) == wanted[i];
            if (!match) continue;
            const size_t first = start + wanted.size();
            for (uint16_t i = 0; i < payload_bytes; i++) {
                uint8_t byte = 0;
                for (size_t bit = 0; bit < 8; bit++) {
                    const size_t index = first + static_cast<size_t>(i) * 8u + bit;
                    const bool set_bit = index < total && bit_at(chips, index);
                    byte = static_cast<uint8_t>((byte << 1) | (set_bit ? 1u : 0u));
                }
                if (fifo.size() < kFifoBytes) {
                    fifo.push_back(byte);
                } else {
                    rx_fifo_flags |= kFifoOverflow;
                }
            }
            if (fifo.size() == kFifoBytes) rx_fifo_flags |= kFifoFull;
            last_length = payload_bytes;
            rssi_high_raw = static_cast<uint16_t>(-2 * rssi_dbm);
            irq |= parts::lr2021::kIrqRxDone;
            return true;
        }
        return false;
    }

    // INFO: fc 11nov26 a frame that lands after the given command, so a test can interleave the air
    void arrive_after(uint16_t opcode, std::function<void()> arrival) {
        arrivals_.push_back({opcode, std::move(arrival)});
    }

    bool listening_for_adsb() const {
        return !in_reset && mode == Mode::Receive && packet_type == parts::lr2021::kPacketTypeOok &&
               bitrate_bps == 2000000 && frequency_hz == 1090000000 && pattern == 0x0285 &&
               pattern_length == 16 && sync_bits == 8 && sync_msb_first && payload_bytes != 0 &&
               packet_format == 0;
    }

    int command_order(uint16_t opcode) const {
        for (size_t i = 0; i < commands_seen.size(); i++)
            if (commands_seen[i] == opcode) return static_cast<int>(i);
        return -1;
    }
    int last_order(uint16_t opcode) const {
        for (size_t i = commands_seen.size(); i > 0; i--)
            if (commands_seen[i - 1] == opcode) return static_cast<int>(i - 1);
        return -1;
    }
    int count(uint16_t opcode) const {
        return static_cast<int>(std::count(commands_seen.begin(), commands_seen.end(), opcode));
    }
    uint32_t reg(uint32_t address) const {
        const auto found = registers.find(address);
        return found == registers.end() ? 0 : found->second;
    }

    int busy_pin{0}, reset_pin{1}, irq_pin{2};
    uint8_t version_major{0x01}, version_minor{0x18};
    bool busy_stuck{false};
    bool busy_stuck_until_reset{false};
    bool miso_dead{false};
    uint32_t boot_busy_us{0};
    uint32_t calibrate_busy_us{0};
    uint16_t boot_errors{0};
    uint16_t calibration_errors{0};
    std::vector<uint16_t> fail_opcodes;

    Mode mode{Mode::StandbyRc};
    bool in_reset{false};
    int reset_pulses{0};
    uint64_t elapsed_us{0};
    uint64_t spun_us{0};
    uint16_t errors{0};
    uint32_t irq{0};
    std::map<uint8_t, uint8_t> dio_function;
    std::map<uint8_t, uint32_t> dio_irq_mask;
    uint8_t irq_dio{8};
    bool tcxo_set{false};
    uint8_t tcxo_tune{0xFF};
    uint32_t tcxo_start_time{0};
    uint8_t fallback_mode{0};
    bool calibrated{false};
    uint8_t calibrated_blocks{0};
    uint16_t calib_fe_word{0};
    size_t calib_fe_bytes{0};
    uint8_t packet_type{0xFF};
    uint32_t bitrate_bps{0};
    uint8_t pulse_shape{0xFF}, rx_bandwidth{0xFF}, depth{0xFF};
    uint16_t preamble_chips{0};
    uint8_t packet_format{0xFF};
    uint16_t payload_bytes{0};
    uint8_t crc_manchester{0xFF};
    uint16_t whitening_polynom{0xFFFF};
    uint32_t sync{0};
    int sync_bits{0};
    bool sync_msb_first{false};
    uint16_t pattern{0};
    int pattern_length{0};
    uint8_t pattern_repeats{0xFF};
    uint8_t sfd{0xFF};
    uint8_t agc_gain{0xFF};
    uint8_t rx_path{0xFF}, rx_boost{0xFF};
    uint32_t frequency_hz{0};
    uint32_t rx_timeout{0};
    uint8_t sleep_config{0xFF};
    size_t sleep_bytes{0};
    std::map<uint32_t, uint32_t> registers;
    std::map<uint32_t, uint32_t> masks;
    std::deque<uint8_t> fifo;
    uint8_t rx_fifo_flags{0};
    uint16_t last_length{0};
    uint16_t rssi_high_raw{0};
    std::vector<uint16_t> commands_seen;
    std::vector<uint8_t> refused;
    Fault fault{Fault::None};
    uint32_t faults{0};

   private:
    static bool bit_at(const uint8_t* bytes, size_t index) {
        return ((bytes[index >> 3] >> (7 - (index & 7))) & 1u) != 0;
    }
    static uint32_t be32(const uint8_t* in) {
        return (static_cast<uint32_t>(in[0]) << 24) | (static_cast<uint32_t>(in[1]) << 16) |
               (static_cast<uint32_t>(in[2]) << 8) | in[3];
    }
    static uint16_t be16(const uint8_t* in) { return static_cast<uint16_t>((in[0] << 8) | in[1]); }
    static uint8_t high(uint32_t value) { return static_cast<uint8_t>(value >> 8); }
    static uint8_t low(uint32_t value) { return static_cast<uint8_t>(value); }

    bool busy() const {
        return busy_stuck || in_reset || mode == Mode::Sleep || elapsed_us < busy_until_us_;
    }
    bool irq_line() const {
        const auto function = dio_function.find(irq_dio);
        const auto mask = dio_irq_mask.find(irq_dio);
        if (function == dio_function.end() || (function->second >> 4) != 1) return false;
        return mask != dio_irq_mask.end() && (irq & mask->second) != 0;
    }

    void note_fault(Fault raised) {
        if (fault == Fault::None) fault = raised;
        faults++;
    }

    // INFO: fc 11nov26 DS 6.7.1 table 6-38: command status, interrupt, reset source, chip mode
    uint8_t miso(size_t at) {
        if (at == 0) return static_cast<uint8_t>((command_status_ << 1) | (irq != 0 ? 1 : 0));
        if (at == 1) return static_cast<uint8_t>((reset_source_ << 4) | chip_mode());
        if (response_frame_) return at - 2 < response_.size() ? response_[at - 2] : 0;
        if (mosi_.size() >= 2 && be16(mosi_.data()) == parts::lr2021::kReadRadioRxFifo) {
            if (fifo.empty()) return 0;
            const uint8_t byte = fifo.front();
            fifo.pop_front();
            return byte;
        }
        if (at < 6) return static_cast<uint8_t>(irq >> (8 * (5 - at)));
        return 0;
    }

    uint8_t chip_mode() const {
        switch (mode) {
            case Mode::Sleep: return 0x0;
            case Mode::StandbyRc: return 0x1;
            case Mode::StandbyXosc: return 0x2;
            case Mode::Receive: return 0x4;
        }
        return 0x0;
    }

    void power_on_reset() {
        mode = Mode::StandbyRc;
        busy_until_us_ = elapsed_us + boot_busy_us;
        errors = boot_errors;
        irq = 0;
        dio_function.clear();
        dio_irq_mask.clear();
        tcxo_set = calibrated = false;
        packet_type = 0xFF;
        registers.clear();
        fifo.clear();
        rx_fifo_flags = 0;
        response_pending_ = false;
        command_status_ = kCommandOk;
        reset_source_ = kResetSourcePin;
    }

    void respond(std::initializer_list<uint8_t> bytes) {
        response_.assign(bytes.begin(), bytes.end());
        response_pending_ = true;
    }

    void refuse(uint8_t status) {
        command_status_ = status;
        refused.push_back(status);
    }

    void execute() {
        if (mosi_.size() < 2) return;
        const uint16_t opcode = be16(mosi_.data());
        const uint8_t* args = mosi_.data() + 2;
        const size_t argument_bytes = mosi_.size() - 2;
        commands_seen.push_back(opcode);
        command_status_ = kCommandOk;
        if (std::find(fail_opcodes.begin(), fail_opcodes.end(), opcode) != fail_opcodes.end()) {
            refuse(parts::lr2021::kCommandFail);
            return;
        }
        namespace chip = parts::lr2021;
        switch (opcode) {
            case chip::kNop:
            case chip::kReadRadioRxFifo: break;
            case chip::kGetVersion: respond({version_major, version_minor}); break;
            case chip::kGetErrors: respond({high(errors), low(errors)}); break;
            case chip::kClearErrors: errors = 0; break;
            case chip::kSetStandby:
                mode = args[0] == chip::kStandbyRc ? Mode::StandbyRc : Mode::StandbyXosc;
                break;
            // INFO: fc 11nov26 DS 6.11.3 and 5.1.1: TCXO and DIO functions are set in STBY_RC only
            case chip::kSetTcxoMode:
                if (mode != Mode::StandbyRc) {
                    refuse(chip::kCommandFail);
                    return;
                }
                tcxo_set = true;
                tcxo_tune = args[0];
                tcxo_start_time = be32(args + 1);
                break;
            case chip::kSetDioFunction:
                if (mode != Mode::StandbyRc) {
                    refuse(chip::kCommandFail);
                    return;
                }
                dio_function[args[0]] = args[1];
                break;
            case chip::kSetDioIrqConfig: dio_irq_mask[args[0]] = be32(args + 1); break;
            case chip::kSetRxTxFallbackMode: fallback_mode = args[0]; break;
            case chip::kClearIrq: irq &= ~be32(args); break;
            case chip::kGetAndClearIrqStatus:
                respond({static_cast<uint8_t>(irq >> 24), static_cast<uint8_t>(irq >> 16),
                         high(irq), low(irq)});
                irq = 0;
                break;
            case chip::kCalibrate:
                if (mode == Mode::Receive) {
                    refuse(chip::kCommandFail);
                    return;
                }
                calibrated = true;
                calibrated_blocks = args[0];
                mode = Mode::StandbyRc;
                busy_until_us_ = elapsed_us + calibrate_busy_us;
                break;
            case chip::kCalibFe:
                if (mode == Mode::Receive) {
                    refuse(chip::kCommandFail);
                    return;
                }
                calib_fe_word = be16(args);
                calib_fe_bytes = argument_bytes;
                errors = static_cast<uint16_t>(errors | calibration_errors);
                break;
            case chip::kSetPacketType: packet_type = args[0]; break;
            // INFO: fc 11nov26 DS 16.3.2: the OOK commands fail on any other packet type
            case chip::kSetOokModulationParams:
                if (packet_type != chip::kPacketTypeOok) {
                    refuse(chip::kCommandFail);
                    return;
                }
                bitrate_bps = be32(args);
                pulse_shape = args[4];
                rx_bandwidth = args[5];
                depth = args[6];
                break;
            case chip::kSetOokPacketParams:
                if (packet_type != chip::kPacketTypeOok) {
                    refuse(chip::kCommandFail);
                    return;
                }
                preamble_chips = be16(args);
                packet_format = args[2];
                payload_bytes = be16(args + 3);
                crc_manchester = args[5];
                break;
            case chip::kSetOokWhiteningParams:
                whitening_polynom = static_cast<uint16_t>(((args[0] & 0x0F) << 8) | args[1]);
                break;
            case chip::kSetOokSyncword:
                sync = be32(args);
                sync_bits = args[4] & 0x7F;
                sync_msb_first = (args[4] & 0x80) != 0;
                break;
            case chip::kSetOokDetector:
                pattern = be16(args);
                pattern_length = (args[2] & 0x0F) + 1;
                pattern_repeats = args[3];
                sfd = args[4];
                break;
            case chip::kSetAgcGainManual: agc_gain = args[0]; break;
            case chip::kWriteRegMemMask32: {
                const uint32_t address = (static_cast<uint32_t>(args[0]) << 16) |
                                         (static_cast<uint32_t>(args[1]) << 8) | args[2];
                const uint32_t mask = be32(args + 3);
                masks[address] = mask;
                registers[address] = (reg(address) & ~mask) | (be32(args + 7) & mask);
                break;
            }
            case chip::kSetRxPath:
                rx_path = args[0];
                rx_boost = args[1];
                break;
            case chip::kSetRfFrequency: frequency_hz = be32(args); break;
            // INFO: fc 11nov26 DS 6.3.5: SetRx fails on a part already in Rx
            case chip::kSetRx:
                if (mode == Mode::Receive || packet_type == 0xFF) {
                    refuse(chip::kCommandFail);
                    return;
                }
                mode = Mode::Receive;
                rx_timeout = (static_cast<uint32_t>(args[0]) << 16) |
                             (static_cast<uint32_t>(args[1]) << 8) | args[2];
                break;
            case chip::kGetRxFifoLevel:
                respond({high(static_cast<uint32_t>(fifo.size())),
                         low(static_cast<uint32_t>(fifo.size()))});
                break;
            case chip::kGetRxPktLength: respond({high(last_length), low(last_length)}); break;
            case chip::kGetOokPacketStatus:
                respond({high(last_length), low(last_length), 0, low(rssi_high_raw >> 1),
                         low(rssi_high_raw & 0x01), 0});
                break;
            case chip::kClearRxFifo: fifo.clear(); break;
            case chip::kGetAndClearFifoIrqFlags:
                respond({rx_fifo_flags, 0});
                rx_fifo_flags = 0;
                break;
            // INFO: fc 11nov26 DS 6.3.1 makes sleep_time optional, so the length is recorded, not
            // refused
            case chip::kSetSleep:
                sleep_config = args[0];
                sleep_bytes = argument_bytes;
                mode = Mode::Sleep;
                break;
            default: refuse(chip::kCommandParameterError); break;
        }
    }

    void land_arrivals() {
        if (commands_seen.empty()) return;
        const uint16_t last = commands_seen.back();
        for (size_t i = 0; i < arrivals_.size(); i++) {
            if (arrivals_[i].first != last) continue;
            const std::function<void()> arrival = arrivals_[i].second;
            arrivals_.erase(arrivals_.begin() + static_cast<long>(i));
            arrival();
            return;
        }
    }

    std::vector<std::pair<uint16_t, std::function<void()>>> arrivals_;
    uint64_t reset_low_since_us_{0};
    uint64_t busy_until_us_{0};
    std::vector<uint8_t> mosi_;
    std::vector<uint8_t> response_;
    bool response_pending_{false};
    bool response_frame_{false};
    uint8_t command_status_{kCommandOk};
    uint8_t reset_source_{0};
};

}  // namespace skyblip::models

#endif
