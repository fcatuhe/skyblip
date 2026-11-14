#ifndef SKYBLIP_HARDWARE_PARTS_LR2021_H
#define SKYBLIP_HARDWARE_PARTS_LR2021_H

#include <cstddef>
#include <cstdint>

#include "core/util/result.h"
#include "hardware/io/io.h"

namespace skyblip::parts {

enum class Lr2021Mode : uint8_t { Reset, Standby, Receive, Sleep };

enum class Lr2021EventType : uint8_t { None, Frame, Fault };

struct Lr2021Event {
    Lr2021EventType type{Lr2021EventType::None};
    int8_t rssi_dbm{0};
    bool rssi_valid{false};
};

// TODO: fc 13nov26 adsb trial #293, back to the constants below once the trial picks the settings
enum class Lr2021Layout : uint8_t { Chips = 0, Manchester = 1 };
struct Lr2021Tuning {
    int16_t detection_threshold_dbm{-90};
    uint8_t gain_step{0};
    uint8_t boost{0};
    bool front_end_raw{true};
    Lr2021Layout layout{Lr2021Layout::Chips};
};

class Lr2021 {
   public:
    Lr2021(io::Spi& spi, io::Gpio& gpio, io::Delay& delay, int busy_pin, int reset_pin, int irq_pin,
           uint8_t irq_dio)
        : spi_(spi),
          gpio_(gpio),
          delay_(delay),
          busy_pin_(busy_pin),
          reset_pin_(reset_pin),
          irq_pin_(irq_pin),
          irq_dio_(irq_dio) {}

    Status probe();
    Status begin(int16_t detection_threshold_dbm);
    Status start_receive();
    // TODO: fc 13nov26 adsb trial #293, remove once the trial picks the LR2021 settings
    Status retune(const Lr2021Tuning& tuning);
    Lr2021Event poll(uint8_t* chips, size_t capacity);
    bool service(uint32_t elapsed_ms, uint32_t silence_ceiling_ms);
    Status sleep();
    void park();
    void hold_in_reset();

    bool irq_asserted() { return gpio_.get(irq_pin_); }
    Lr2021Mode mode() const { return mode_; }
    uint32_t rearms() const { return rearms_; }
    uint32_t resets() const { return resets_; }
    uint32_t command_errors() const { return command_errors_; }
    uint16_t device_errors() const { return device_errors_; }
    uint32_t fifo_drops() const { return fifo_drops_; }
    uint32_t foreign_frames() const { return foreign_frames_; }

   private:
    Status wait_busy_low(uint32_t ceiling_us);
    Status reset();
    Status verify_version();
    Status bring_up();
    Status configure_ook();
    Status take_snapshot();
    Status rearm();
    Status restart_receive();
    Status reinitialise();
    Status enter_standby();
    Status command(uint16_t opcode, const uint8_t* arguments, size_t bytes);
    Status read(uint16_t opcode, uint8_t* out, size_t bytes);
    Status write_mask(uint32_t address, uint32_t mask, uint32_t data);
    void note_status(const uint8_t* status);
    Status read_errors();
    Status read_frame(uint8_t* chips, Lr2021Event& event);
    uint8_t frame_bytes() const;

    static constexpr size_t kFrameBytes = 16;

    io::Spi& spi_;
    io::Gpio& gpio_;
    io::Delay& delay_;
    int busy_pin_, reset_pin_, irq_pin_;
    uint8_t irq_dio_;
    Lr2021Mode mode_{Lr2021Mode::Reset};
    Lr2021Tuning tuning_{};
    uint16_t last_opcode_{0};
    uint8_t frames_waiting_{0};
    int8_t newest_rssi_dbm_{0};
    bool newest_rssi_valid_{false};
    bool reset_owed_{false};
    uint32_t since_frame_ms_{0};
    uint32_t rearms_{0};
    uint32_t resets_{0};
    uint32_t command_errors_{0};
    uint16_t device_errors_{0};
    uint32_t fifo_drops_{0};
    uint32_t foreign_frames_{0};
};

// INFO: fc 11nov26 DS is Semtech's LR2021/LR2022/LR2012 Datasheet, Final Rev. 2.1, 13 April 2026,
// cited by section
namespace lr2021 {
constexpr uint16_t kNop = 0x0000;
constexpr uint16_t kReadRadioRxFifo = 0x0001;
constexpr uint16_t kGetVersion = 0x0101;
constexpr uint16_t kWriteRegMemMask32 = 0x0105;
constexpr uint16_t kGetErrors = 0x0110;
constexpr uint16_t kClearErrors = 0x0111;
constexpr uint16_t kSetDioFunction = 0x0112;
constexpr uint16_t kSetDioIrqConfig = 0x0115;
constexpr uint16_t kClearIrq = 0x0116;
constexpr uint16_t kGetAndClearIrqStatus = 0x0117;
constexpr uint16_t kGetRxFifoLevel = 0x011C;
constexpr uint16_t kGetAndClearFifoIrqFlags = 0x012E;
constexpr uint16_t kClearRxFifo = 0x011E;
constexpr uint16_t kSetTcxoMode = 0x0120;
constexpr uint16_t kCalibrate = 0x0122;
constexpr uint16_t kCalibFe = 0x0123;
constexpr uint16_t kSetSleep = 0x0127;
constexpr uint16_t kSetStandby = 0x0128;
constexpr uint16_t kSetRfFrequency = 0x0200;
constexpr uint16_t kSetRxPath = 0x0201;
constexpr uint16_t kSetRxTxFallbackMode = 0x0206;
constexpr uint16_t kSetPacketType = 0x0207;
constexpr uint16_t kSetRx = 0x020C;
constexpr uint16_t kGetRxPktLength = 0x0212;
constexpr uint16_t kSetAgcGainManual = 0x021A;
constexpr uint16_t kSetOokModulationParams = 0x0281;
constexpr uint16_t kSetOokPacketParams = 0x0282;
constexpr uint16_t kSetOokSyncword = 0x0284;
constexpr uint16_t kGetOokPacketStatus = 0x0287;
constexpr uint16_t kSetOokDetector = 0x0288;
constexpr uint16_t kSetOokWhiteningParams = 0x0289;

// INFO: fc 11nov26 DS 6.7.2: an LR2022 or LR2012 answers 2.0, so the check also refuses a sibling
constexpr uint8_t kVersionMajor = 0x01;
constexpr uint8_t kVersionMinor = 0x18;

constexpr uint8_t kCommandStatusShift = 1;
constexpr uint8_t kCommandStatusMask = 0x07;
constexpr uint8_t kCommandFail = 0x00;
constexpr uint8_t kCommandParameterError = 0x01;

constexpr uint8_t kStandbyRc = 0x00;
constexpr uint8_t kFallbackStandbyRc = 0x01;
constexpr uint8_t kPacketTypeOok = 0x0A;
constexpr uint8_t kSleepColdNoClock = 0x00;
constexpr uint8_t kDioFunctionIrqPullDown = 0x11;
constexpr uint8_t kCalibrateAllBlocks = 0x6F;
constexpr uint32_t kRxContinuous = 0xFFFFFF;
constexpr uint32_t kIrqAll = 0xFFFFFFFF;
constexpr uint32_t kIrqRxDone = 1UL << 18;
// INFO: fc 11nov26 DS 6.10.1 and 6.10.4: the overflow flag latches whether or not its IRQ is
// enabled
constexpr uint8_t kFifoOverflow = 0x10;

constexpr uint16_t kErrorHfXoscStart = 0x0001;
constexpr uint16_t kErrorPllLock = 0x0004;
constexpr uint16_t kFatalErrors = kErrorHfXoscStart | kErrorPllLock;

// TODO: fc 11nov26 1.6 V from SoftRF radiolib.cpp:2133 for the X1, unverified until #169 benches a
// unit
constexpr uint8_t kTcxoTune1v6 = 0x00;
// TODO: fc 11nov26 SoftRF's start time, which DS 6.11.3 counts in 32 MHz periods, unverified until
// #169
constexpr uint32_t kTcxoStartTime = 32765;

constexpr uint32_t kFrequencyHz = 1090000000;
// TODO: fc 11nov26 DS table 3-8 specifies the LF path to 960 MHz and SoftRF receives 1090 on it,
// unverified until #169
constexpr uint8_t kRxPathLowFrequency = 0x00;
constexpr uint8_t kRxBoostOff = 0x00;
// TODO: fc 11nov26 SoftRF's CalibFE word, 1090 MHz over 4 MHz rounded, unverified until #169
// benches a unit
constexpr uint16_t kCalibFe1090Word = 273;

// TODO: fc 11nov26 SoftRF radiolib.cpp:2250-2281 asks 2 Mbps in 3076 kHz unshaped, unverified until
// #169
constexpr uint32_t kBitrateBps = 2000000;
constexpr uint8_t kPulseShapeNone = 0x00;
constexpr uint8_t kRxBandwidth3076kHz = 0x00;
constexpr uint8_t kDepthFull = 0x00;
constexpr uint16_t kPreambleChips = 16;
constexpr uint8_t kFixedLength = 0x00;
constexpr uint8_t kCrcOffManchesterOff = 0x00;
constexpr uint8_t kSyncword = 0x95;
constexpr uint8_t kSyncwordMsbFirstBits = 0x80 | 8;
// INFO: fc 11nov26 DS 16.3.8 names these for ADS-B: pattern 0x0285, no repeat, falling edge, no SFD
constexpr uint16_t kAdsbPreamblePattern = 0x0285;
constexpr uint8_t kAdsbPreambleChips = 16;
constexpr uint8_t kAgcAutomatic = 0x00;
// TODO: fc 13nov26 adsb trial #293, keep only the values the trial picks
// INFO: fc 13nov26 RadioLib setGain() and setRxPath(): gain steps 1 to 13 and boosts 0 to 7
constexpr uint8_t kGainStepCeiling = 13;
constexpr uint8_t kRxBoostCeiling = 7;
// INFO: fc 13nov26 RadioLib LR2021_commands.h: Manchester on, inverted, the layout cb-tfc receives
constexpr uint8_t kCrcOffManchesterInverted = 0x09;
constexpr uint8_t kSyncwordNoneMsbFirst = 0x80;
constexpr uint8_t kManchesterFrameBytes = 14;

// INFO: fc 11nov26 Semtech's lr20xx_workarounds.c, not the DS: register 0xF30E14 holds the
// threshold in dB plus 74
constexpr uint32_t kOokThresholdRegister = 0xF30E14;
constexpr uint32_t kOokThresholdMask = 0x7FUL << 20;
constexpr int16_t kOokThresholdOffsetDb = 74;
// TODO: fc 11nov26 the field SoftRF's -80 dBm writes, -90 dBm by Semtech's formula, unverified
// until #169
constexpr int16_t kDetectionThresholdDbm = -90;
// TODO: fc 11nov26 SoftRF radiolib.cpp:2359 writes it with no public source, unverified until #169
// benches a unit
constexpr uint32_t kOokQuirkRegister = 0xF30150;
constexpr uint32_t kOokQuirkMask = 0x000000FF;
constexpr uint32_t kOokQuirkData = 0x18;
// TODO: fc 13nov26 adsb trial #293, keep only the value the trial picks
// INFO: fc 13nov26 cb-tfc radio.cpp: the part's default, which runs the AGC and blanks a saturated
// ADC
constexpr uint32_t kOokQuirkChipDefault = 0x75;

constexpr uint8_t kChipBytes = 27;

// TODO: fc 13nov26 adsb trial #293, remove once the trial picks the LR2021 settings
static_assert(Lr2021Tuning{}.detection_threshold_dbm == kDetectionThresholdDbm &&
                  Lr2021Tuning{}.gain_step == kAgcAutomatic &&
                  Lr2021Tuning{}.boost == kRxBoostOff && Lr2021Tuning{}.front_end_raw,
              "a default tuning is SoftRF's, which the bring-up sent before the trial");

// INFO: fc 11nov26 DS 4.2 asks NRESET low for 100 us at least
constexpr uint32_t kResetLowUs = 100;
// TODO: fc 11nov26 RadioLib's 3 s boot and 1 s command ceilings, the DS gives none, unverified
// until #169
constexpr uint32_t kBootBusyCeilingUs = 3000000;
constexpr uint32_t kCommandBusyCeilingUs = 1000000;
// INFO: fc 11nov26 a 32768 Hz tick is 31 us, so 1 ms steps sleep and yield where 10 us steps spin
constexpr uint32_t kBusySpinUs = 1000;
constexpr uint32_t kBusySpinPollUs = 10;
constexpr uint32_t kBusySleepPollUs = 1000;
// INFO: fc 11nov26 RadioLib 8.0.0 LR2021::sleep() waits 1 ms after SetSleep, and BUSY then stays
// high
constexpr uint32_t kSleepSettleUs = 1000;
}  // namespace lr2021

}  // namespace skyblip::parts

#endif
