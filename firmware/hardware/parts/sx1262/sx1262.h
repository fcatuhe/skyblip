#ifndef SKYBLIP_HARDWARE_PARTS_SX1262_H
#define SKYBLIP_HARDWARE_PARTS_SX1262_H

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "core/util/result.h"
#include "hardware/io/io.h"

namespace skyblip::parts {

enum class RadioMode : uint8_t { Sleep, Standby, Tx, Rx };

enum class RadioEventType : uint8_t { None, RxDone, CrcError, TxDone, Timeout, Fault };

struct RadioEvent {
    RadioEventType type{RadioEventType::None};
    uint8_t bytes{0};
    int8_t rssi_dbm{0};
    bool rssi_valid{false};
};

// INFO: fc 20nov26 the M and O bands are two modulations, so every modem figure travels in here
struct RadioConfig {
    uint32_t frequency_hz{868200000};
    uint32_t bitrate{100000};
    uint32_t deviation_hz{50000};
    uint32_t bandwidth_hz{200000};
    // INFO: fc 20nov26 zero is the unshaped carrier the M band asks for and the part resets to
    uint16_t gaussian_bt_e2{0};
    // INFO: fc 20nov26 the detector runs on chips, so the sync and the payload count chips
    const uint8_t* sync{nullptr};
    uint8_t sync_bits{0};
    uint8_t payload_bytes{0};
    // INFO: fc 20nov26 a field trim for an off reference, positive upwards, clamped by trimmed_hz()
    int16_t frequency_correction_e1_ppm{0};
};

class Sx1262 {
   public:
    Sx1262(io::Spi& spi, io::Gpio& gpio, io::Delay& delay, int busy_pin, int reset_pin,
           int dio1_pin)
        : spi_(spi),
          gpio_(gpio),
          delay_(delay),
          busy_pin_(busy_pin),
          reset_pin_(reset_pin),
          dio1_pin_(dio1_pin) {}

    Status probe();

    Status begin();
    Status configure_radio(const RadioConfig& config);

    Status transmit(const uint8_t* data, uint8_t bytes);
    Status stage_tx(const uint8_t* data, uint8_t bytes);
    Status key_tx();
    uint32_t air_us(uint8_t bytes) const;
    Status start_receive();

    void sleep();
    Status wake();

    // INFO: fc 15sep26 DS 13.5.2: an instant, and valid only while the receiver is running
    int8_t instant_rssi_dbm();

    Status wait_ready() { return wait_busy_low(); }

    RadioEvent poll(uint8_t* out, uint8_t capacity);
    // INFO: fc 16sep26 DIO1 is a level here, not an edge | 03oct26 an edge only wakes the executor
    bool irq_asserted() { return gpio_.get(dio1_pin_); }

    bool service(uint32_t elapsed_ms, uint32_t rx_silence_ceiling_ms = 30000);

    RadioMode mode() const { return mode_; }
    uint32_t reinitialisations() const { return reinitialisations_; }
    uint32_t tx_recovery_count() const { return tx_recovery_count_; }
    uint16_t device_errors() const { return device_errors_; }

   private:
    Status wait_busy_low(uint32_t max_spins = 100000);
    void select_when_ready();
    void command(uint8_t opcode, const uint8_t* parameters, size_t bytes);
    void read_command(uint8_t opcode, uint8_t* out, size_t bytes);
    void write_frame(const uint8_t* head, size_t head_bytes, const uint8_t* data, size_t bytes);
    void read_frame(const uint8_t* head, size_t head_bytes, uint8_t* out, size_t bytes);
    void write_register(uint16_t address, const uint8_t* data, size_t bytes);
    void read_register(uint16_t address, uint8_t* out, size_t bytes);
    Status reset_to_standby();
    Status verify_link();
    Status enter_standby();
    Status enter_standby_on_tcxo();
    Status enter_standby_on(uint8_t clock);
    bool holds(const RadioConfig& config) const;
    bool hops_to(const RadioConfig& config) const;
    Status retune(const RadioConfig& config, RadioMode was);
    void write_frequency(const RadioConfig& config);
    void configure_modulation(const RadioConfig& config);
    void configure_rx_gain();
    void configure_tx_clamp();
    void configure_tx_modulation();
    void configure_power();
    void configure_irq();
    int8_t packet_rssi_dbm();
    uint8_t read_payload(uint8_t* out, uint8_t capacity);
    Status check_device_errors();
    void clear_device_errors();
    void configure_frame(const RadioConfig& config);
    void write_packet_parameters(uint8_t payload_bytes);
    uint32_t tx_timeout_ticks(uint8_t bytes) const;
    void recover_tx();
    Status reinitialise();

    static constexpr size_t kWriteFrameBytes = 3 + 255;
    static constexpr size_t kReadFrameBytes = 16;

    io::Spi& spi_;
    io::Gpio& gpio_;
    io::Delay& delay_;
    uint8_t frame_[kWriteFrameBytes]{};
    int busy_pin_, reset_pin_, dio1_pin_;
    RadioMode mode_{RadioMode::Sleep};
    RadioConfig config_{};
    bool brought_up_{false};
    bool configured_{false};
    bool tuned_{false};
    uint8_t packet_bytes_{0};
    uint8_t staged_bytes_{0};
    uint32_t since_rx_ms_{0};
    uint32_t reinitialisations_{0};
    bool reinitialisation_owed_{false};
    uint32_t tx_recovery_count_{0};
    uint16_t device_errors_{0};
};

namespace sx1262 {
constexpr uint8_t kSetStandby = 0x80;
constexpr uint8_t kStandbyOnRcOscillator = 0x00;
constexpr uint8_t kStandbyOnCrystalOscillator = 0x01;
// INFO: fc 26sep26 DS 13.1.15: where the part goes after TX, STDBY_RC unless told otherwise
constexpr uint8_t kSetRxTxFallbackMode = 0x93;
constexpr uint8_t kFallbackFrequencySynthesis = 0x40;
constexpr uint8_t kFallbackStandbyOnCrystalOscillator = 0x30;
constexpr uint8_t kFallbackStandbyOnRcOscillator = 0x20;
constexpr uint8_t kSetRegulatorMode = 0x96;
// INFO: fc 20nov26 DS 13.1.4: DC-DC roughly halves the current, and SoftRF selects it on this board
constexpr uint8_t kRegulatorLdo = 0x00;
constexpr uint8_t kRegulatorDcDc = 0x01;
constexpr uint8_t kSetDio3AsTcxoControl = 0x97;
constexpr uint8_t kSetDio2AsRfSwitch = 0x9D;
constexpr uint8_t kCalibrate = 0x89;
constexpr uint8_t kTcxoVoltage1v8 = 0x02;
constexpr uint8_t kCalibrateImage = 0x98;
constexpr uint8_t kSetTx = 0x83;
constexpr uint8_t kSetRx = 0x82;
constexpr uint8_t kSetRfFrequency = 0x86;
constexpr uint8_t kSetPacketType = 0x8A;
constexpr uint8_t kSetModulationParameters = 0x8B;
constexpr uint8_t kSetPacketParameters = 0x8C;
constexpr uint8_t kSetPaConfig = 0x95;
constexpr uint8_t kSetTxParameters = 0x8E;
constexpr uint8_t kSetDioIrqParameters = 0x08;
constexpr uint8_t kWriteRegister = 0x0D;
constexpr uint8_t kReadRegister = 0x1D;
constexpr uint8_t kSetSleep = 0x84;
// INFO: fc 20nov26 DS 13.1.2: bit 2 keeps the configuration, and RTC wake-up stays off
constexpr uint8_t kSleepWarmStartNoRtc = 0x04;
// INFO: fc 08nov26 DS 13.1.1: cold start draws 160 nA against 600 nA, and every boot reinitialises
constexpr uint8_t kSleepColdStartNoRtc = 0x00;
// INFO: fc 20nov26 DS 13.4.9: eight bytes of sync word start here
constexpr uint16_t kSyncWordRegister = 0x06C0;

// INFO: fc 20nov26 DS 9.6: boost buys about 3 dB for 2 mA, and a warm start drops it unless listed
constexpr uint16_t kRxGainRegister = 0x08AC;
constexpr uint8_t kRxGainPowerSaving = 0x94;
constexpr uint8_t kRxGainBoosted = 0x96;
// INFO: fc 20nov26 DS 9.6: the retention list is a count, then one 16-bit register address
constexpr uint16_t kRetentionListRegister = 0x029F;
constexpr uint8_t kRetainRxGain[3] = {0x01, static_cast<uint8_t>(kRxGainRegister >> 8),
                                      static_cast<uint8_t>(kRxGainRegister & 0xFF)};
// INFO: fc 20nov26 complementary bytes, so a MISO stuck at a rail or tied to MOSI reads back wrong
constexpr uint8_t kLinkProbePattern[2] = {0xA5, 0x5A};
constexpr uint8_t kWriteBuffer = 0x0E;
constexpr uint8_t kReadBuffer = 0x1E;
constexpr uint8_t kGetIrqStatus = 0x12;
constexpr uint8_t kClearIrqStatus = 0x02;
constexpr uint8_t kGetStatus = 0xC0;
constexpr uint8_t kGetDeviceErrors = 0x17;
constexpr uint8_t kClearDeviceErrors = 0x07;
// INFO: fc 05sep26 DS 13.5.5 OpError: the only report a failed calibration ever makes
constexpr uint16_t kErrorRc64kCalibration = 0x0001;
constexpr uint16_t kErrorRc13mCalibration = 0x0002;
constexpr uint16_t kErrorPllCalibration = 0x0004;
constexpr uint16_t kErrorAdcCalibration = 0x0008;
constexpr uint16_t kErrorImageCalibration = 0x0010;
constexpr uint16_t kErrorCrystalOscillatorStart = 0x0020;
constexpr uint16_t kErrorPllLock = 0x0040;
constexpr uint16_t kErrorPaRamp = 0x0100;
constexpr uint16_t kDeviceErrorMask = 0x01FF;
constexpr uint8_t kGetInstantRssi = 0x15;
constexpr uint8_t kGetRxBufferStatus = 0x13;
constexpr uint8_t kGetPacketStatus = 0x14;
constexpr uint16_t kIrqTxDone = 0x0001;
constexpr uint16_t kIrqRxDone = 0x0002;
constexpr uint16_t kIrqCrcError = 0x0040;
constexpr uint16_t kIrqTimeout = 0x0200;
// INFO: wr 02aug26 DS 13.3.1: GetIrqStatus never reports a bit this mask leaves out
constexpr uint16_t kIrqMask = kIrqTxDone | kIrqRxDone | kIrqCrcError | kIrqTimeout;

constexpr uint32_t kCrystalHz = 32000000;
// INFO: fc 20nov26 DS 13.4.6: pulse shape and RX bandwidth are table indices, not values
constexpr uint8_t kPulseShapeNone = 0x00;
constexpr uint8_t kGaussianBt0p3 = 0x08;
constexpr uint8_t kGaussianBt0p5 = 0x09;
constexpr uint8_t kGaussianBt0p7 = 0x0A;
constexpr uint8_t kGaussianBt1p0 = 0x0B;
constexpr uint8_t kRxBandwidth234kHz = 0x0A;

// INFO: fc 05sep26 DS 15.1.2: bit 2 must be 1 for any (G)FSK, and reset leaves it 0
constexpr uint16_t kTxModulationRegister = 0x0889;
constexpr uint8_t kTxModulationGfskBit = 0x04;

// INFO: fc 20nov26 DS 13.4.6's double-sideband bandwidths, over the range the two bands use
struct RxBandwidthEntry {
    uint32_t bandwidth_hz;
    uint8_t index;
};
constexpr RxBandwidthEntry kRxBandwidths[] = {
    {117300, 0x0B}, {156200, 0x1A}, {187200, 0x12}, {234300, 0x0A},
    {312000, 0x19}, {373600, 0x11}, {467000, 0x09},
};
constexpr int kRxBandwidthCount =
    static_cast<int>(sizeof(kRxBandwidths) / sizeof(kRxBandwidths[0]));

constexpr uint8_t kPaConfigHighPower[4] = {0x04, 0x07, 0x00, 0x01};
// INFO: fc 26sep26 DS table 13-21: those four bytes are the +22 dBm row
constexpr int8_t kPaConfigHighPowerRatedDbm = 22;
constexpr uint8_t kRampTime200Us = 0x04;
// TODO: fc 05sep26 DS 13-21 halves the 90 mA of +14 dBm, once this PA match is measured

// INFO: fc 05sep26 DS table 5-2: SetPaConfig rewrites OCP to 140 mA on an SX1262, every call
constexpr uint16_t kOcpRegister = 0x08E7;
// INFO: fc 05sep26 DS table 3-5: +14 dBm through this PA configuration draws 90 mA at 868 MHz
constexpr uint16_t kOcpLimitMa = 120;
// INFO: fc 05sep26 DS 5.1: the OCP register counts 2.5 mA a step
constexpr uint8_t kOcpLimitSteps = static_cast<uint8_t>(kOcpLimitMa * 2 / 5);

// INFO: fc 05sep26 DS 15.2.2: the reset clamp costs 5-6 dB into a mismatched antenna
constexpr uint16_t kTxClampRegister = 0x08D8;
constexpr uint8_t kTxClampWidenBits = 0x1E;
// INFO: wr 02aug26 ERC 70-03 annex 1 band h1.4: 868.0-868.6 MHz allows 25 mW e.r.p., 14 dBm
constexpr int8_t kSrd868ErpLimitDbm = 14;

// INFO: fc 26sep26 e.r.p. is referenced to a half-wave dipole, 2.15 dBi above isotropic
constexpr int16_t kDbiToDbdCentiDb = 215;
// INFO: fc 26sep26 the vendor's datasheet peak for ANT-868-CW-QW-SMA, never measured here
constexpr int16_t kAntennaPeakGainDbiCentiDb = 160;
// INFO: fc 26sep26 an unmeasured allowance, and SoftRF takes none: the field run sets it
constexpr int16_t kFeedLossCentiDb = 50;
// INFO: fc 26sep26 nominal, TXACC +-2 dB, and SoftRF's too (almic.cpp:330): the field run sets it
constexpr int8_t kConductedDbm = 14;
constexpr int16_t kResultingErpCentiDb = static_cast<int16_t>(
    kConductedDbm * 100 - kFeedLossCentiDb + kAntennaPeakGainDbiCentiDb - kDbiToDbdCentiDb);
static_assert(kResultingErpCentiDb <= kSrd868ErpLimitDbm * 100,
              "the programmed conducted power exceeds 25 mW e.r.p. with the declared antenna");
// INFO: fc 14nov26 the antenna's gain over a dipole, which EN 300 220-1 Table 45 is corrected by
constexpr int16_t kAntennaGainCentiDbd =
    static_cast<int16_t>(kAntennaPeakGainDbiCentiDb - kDbiToDbdCentiDb);

// INFO: fc 20nov26 DS 13.1.12: the CalibrateImage pair for 863-870 MHz
constexpr uint8_t kImageBand863to870[2] = {0xD7, 0xDB};

// INFO: fc 20nov26 +-8.7 kHz at 868.2 MHz stays in band h1.4, the bound the Go's settings share
constexpr int16_t kFrequencyTrimCeilingE1Ppm = 100;
constexpr int32_t kFrequencyTrimPerUnit = 10000000;

constexpr uint32_t trimmed_hz(uint32_t frequency_hz, int16_t correction_e1_ppm) {
    int32_t correction = correction_e1_ppm;
    correction =
        std::clamp<int32_t>(correction, -kFrequencyTrimCeilingE1Ppm, kFrequencyTrimCeilingE1Ppm);
    const int64_t offset =
        (static_cast<int64_t>(frequency_hz) * correction) / kFrequencyTrimPerUnit;
    return static_cast<uint32_t>(static_cast<int64_t>(frequency_hz) + offset);
}

// INFO: fc 20nov26 DS 13.4.1: a zero SetTx timeout leaves the PA keyed when TxDone never comes
constexpr uint32_t kTimeoutStepNs = 15625;
constexpr uint32_t kTimeoutTicksCeiling = 0xFFFFFF;
constexpr uint32_t kTxGuardUs = 25000;

// INFO: wr 02aug26 DS 8.1: NRESET must be held low >= 100 us
constexpr uint32_t kResetLowUs = 100;
// INFO: fc 05sep26 DS 13.1.2: no SPI for 500 us after SetSleep, while the configuration saves
constexpr uint32_t kSleepSettleUs = 500;
// INFO: fc 20nov26 ADS-L §C.2 sends 16 preamble chips, and the detector needs 8 of them
constexpr uint16_t kPreambleChips = 16;
constexpr uint8_t kPreambleDetect8Chips = 0x04;
constexpr uint8_t kAddressFilteringOff = 0x00;
constexpr uint8_t kFixedLength = 0x00;
// INFO: fc 16sep26 DS 13.4.6 table 13-70: CRC off is 0x01 here, 0x00 is a one-byte CRC
constexpr uint8_t kCrcOff = 0x01;
constexpr uint8_t kCrcOneByte = 0x00;
constexpr uint8_t kWhiteningOff = 0x00;
constexpr uint8_t kReadChunkBytes = 64;
}

}

#endif
