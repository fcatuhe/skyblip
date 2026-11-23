#ifndef SKYBLIP_HARDWARE_PARTS_L76K_H
#define SKYBLIP_HARDWARE_PARTS_L76K_H

#include "core/gnss/nmea.h"
#include "core/gnss/validity.h"
#include "hardware/io/io.h"
#include "ports/gnss.h"

namespace skyblip::parts {

// INFO: fc 20nov26 autobaud works only when the platform hands over an io::UartRate that can retune
// the port

class L76k : public ports::Gnss {
   public:
    L76k(ports::SolutionRate solution_rate, io::Uart& uart, io::UartRate& rate = io::kFixedUartRate,
         io::Gpio* reset = nullptr, int reset_pin = -1)
        : solution_rate_(solution_rate),
          uart_(uart),
          rate_(rate),
          reset_(reset),
          reset_pin_(reset_pin) {}

    static constexpr bool solves(ports::SolutionRate) { return true; }

    using ConfigStage = ports::GnssConfigStage;

    // INFO: gn 09jun25 t_echo_plus.dts:278 `current-speed` must equal this, nothing checks it
    static constexpr uint32_t kFactoryBaudRate = 9600;

    // INFO: fc 19sep26 $PCAS01 takes 0..5 for 4800..115200, L76K protocol spec V1.1 SS2.3.1
    static constexpr uint32_t kTargetBaudRate = 115200;
    static constexpr const char* kBaudCommand = "$PCAS01,5*19\r\n";

    // INFO: fc 19sep26 SoftRF measures this part talking 70 ms after the second (GNSS.cpp:83-91)
    static constexpr uint32_t kBurstStartMs = 70;

    // INFO: fc 03aug26 a receiver left at another rate stays silent if 9600 is assumed, so the
    // AT6558's rates are walked
    static constexpr int kBaudCandidateCount = 6;
    static constexpr uint32_t kBaudCandidates[kBaudCandidateCount] = {
        9600, 115200, 38400, 57600, 19200, 4800,
    };

    // INFO: fc 19nov26 2 Hz solves on .000 and .500, two of G.1.1's quarters, L76K V1.1 SS2.3.2
    static constexpr uint32_t kSolutionPeriodMs = 500;
    static constexpr uint32_t kFactorySolutionPeriodMs = 1000;

    // INFO: fc 13sep26 GGA + three GSA (one per constellation) + RMC, the widest burst we ask for
    static constexpr uint32_t kBurstBytes = 320;
    static constexpr uint32_t kBurstMs = io::wire_ms(kBurstBytes, kTargetBaudRate);

    // INFO: fc 03aug26 the L76K wakes on UART activity and eats the first bytes, so one byte and
    // 500 ms come first
    static constexpr uint8_t kWakeByte = 0x00;
    static constexpr uint32_t kWakeDelayMs = 500;

    // INFO: fc 03aug26 the $GPTXT answer proves an AT6558 before any $PCAS sentence is trusted, as
    // SoftRF does
    static constexpr const char* kIdentifyCommand = "$PCAS06,0*1B\r\n";
    static constexpr uint32_t kIdentifyWindowMs = 1000;

    // INFO: gn 09jun25 SoftRF sends the first three 250 ms apart (.../driver/GNSS.cpp:1029-1057)
    static constexpr uint32_t kCommandGapMs = 250;
    static constexpr int kCommandCount = 4;
    static constexpr const char* kCommands[kCommandCount] = {
        "$PCAS04,7*1E\r\n",
        "$PCAS03,1,0,1,0,1,0,0,0,0,0,,,0,0*03\r\n",
        "$PCAS11,6*1B\r\n",
        "$PCAS02,1000*2E\r\n",
    };

    // INFO: fc 18nov26 L76K protocol spec V1.1 SS2.3.2 allows under 1000 ms only at 115200
    static constexpr int kRateCommandCount = 2;
    static constexpr const char* kRateCommands[kRateCommandCount] = {
        "$PCAS02,500*1A\r\n",
        "$PCAS03,,,2,,,,,,,,,,,*30\r\n",
    };
    // INFO: fc 19nov26 the second sentence above keeps GSA, and the VDOP it carries, once a second
    static constexpr uint8_t kGsaPeriodSolutions = 2;
    static_assert(kGsaPeriodSolutions * kSolutionPeriodMs == kFactorySolutionPeriodMs,
                  "GSA keeps the once a second it had at the factory rate");

    // INFO: fc 13sep26 GSA is the only sentence carrying VDOP, which G.1.15 asks us to claim
    static constexpr bool kGsaEnabled = true;

    // INFO: fc 29sep26 GSV precedes RMC: nav_ms +55-65 ms on bench 0B1B2C, so one set in five
    static constexpr uint8_t kSatellitesInViewPeriodSolutions = 5;
    // INFO: fc 19nov26 $PCAS03 counts to 9, so at 2 Hz a set every 4.5 s, as one in five at 1 Hz
    static constexpr uint8_t kHalfSecondSatellitesInViewPeriodSolutions = 9;

    // INFO: fc 18sep26 $PCAS03 takes a null per field meaning keep, so these move nGSV alone
    static constexpr const char* kSatellitesInViewOff = "$PCAS03,,,,0,,,,,,,,,,*32\r\n";
    static constexpr const char* kSatellitesInViewEverySolution = "$PCAS03,,,,1,,,,,,,,,,*33\r\n";
    static constexpr const char* kSatellitesInViewPaced = "$PCAS03,,,,5,,,,,,,,,,*37\r\n";
    static constexpr const char* kSatellitesInViewHalfSecondPaced = "$PCAS03,,,,9,,,,,,,,,,*3B\r\n";

    // INFO: fc 18sep26 four talkers, up to 32 satellites, four to a sentence and 72 bytes each
    static constexpr uint32_t kSatellitesInViewBytes = 648;
    static constexpr uint32_t kSearchingBurstBytes = kBurstBytes + kSatellitesInViewBytes;
    static constexpr uint32_t kSearchingBurstMs =
        io::wire_ms(kSearchingBurstBytes, kTargetBaudRate);
    static_assert(kSearchingBurstMs < kSolutionPeriodMs,
                  "the burst must fit inside one solution period, satellites in view and all");

    // INFO: fc 20nov26 $PCAS10 reboots the receiver, which then answers nothing for about a second
    static constexpr uint32_t kRestartSettleMs = 1000;
    static constexpr const char* kRestartCommands[4] = {
        "$PCAS10,0*1C\r\n",
        "$PCAS10,1*1D\r\n",
        "$PCAS10,2*1E\r\n",
        "$PCAS10,3*1F\r\n",
    };

    // INFO: fc 18nov26 Go 000001 went silent, NMEA and PPS, and stayed so until rebooted
    static constexpr uint32_t kSilenceMs = 3000;
    static constexpr uint32_t kProbeListenMs = kWakeDelayMs + kFactorySolutionPeriodMs;
    static constexpr uint32_t kProbePeriodMs = 10000;
    static_assert(kBaudCandidateCount * kProbeListenMs < kProbePeriodMs,
                  "a probe round hears every rate once inside its period");
    static constexpr uint8_t kSilentRoundsBeforeReset = 3;
    // INFO: fc 18nov26 Quectel L76K Hardware Design V1.0 4.2.1: RESET_N low at least 100 ms
    static constexpr uint32_t kResetHoldMs = 150;
    // INFO: fc 19nov26 a part refusing the rate or disobeying is retried for good, ever slower
    static constexpr int kRecoveryBackoffSteps = 3;
    static constexpr uint32_t kRecoveryBackoffMs[kRecoveryBackoffSteps] = {10000, 30000, 60000};

    // INFO: fc 13sep26 obedience is the sentence set | 18nov26 and, after 2 Hz, its cadence
    static constexpr uint32_t kVerifyWindowMs = 3000;
    static constexpr uint32_t kMinVerifySentences = 2;
    static constexpr uint32_t kRateVerifyWindowMs = 2000;
    // INFO: fc 19nov26 2 Hz makes four half steps a window, any other rate none
    static constexpr uint32_t kMinHalfSteps = 2;
    static_assert(kMinHalfSteps < kRateVerifyWindowMs / kSolutionPeriodMs,
                  "a part at the asked rate passes with steps to spare");
    static constexpr int32_t kDayMs = 86400000;
    static constexpr uint8_t kMaxConfigAttempts = 3;

    void service(uint32_t now_ms);

    ConfigStage config_stage() const { return stage_; }
    enum class Cadence : uint8_t { Second, Half };
    bool solving_half_seconds() const {
        return stage_ == ConfigStage::Ready && cadence_ == Cadence::Half;
    }
    Cadence cadence() const { return cadence_; }
    uint32_t solution_period_ms() const;
    bool configured() const { return stage_ == ConfigStage::Ready; }
    // INFO: fc 19nov26 ready at the rate the product asked, which the transmit gate reads
    bool holds_rate() const {
        return configured() &&
               (solution_rate_ == ports::SolutionRate::Second || cadence_ == Cadence::Half);
    }
    uint32_t recoveries() const { return recoveries_; }
    bool degraded() const { return stage_ == ConfigStage::Degraded; }
    bool probing() const { return stage_ == ConfigStage::Probing; }

    // INFO: fc 20nov26 also true when the published fix stopped being one, because a silent
    // receiver must still be reported
    bool poll(uint32_t now_ms);

    // INFO: fc 20nov26 the board polls right after service(), so the last service instant is this
    // poll's clock
    bool poll() { return poll(serviced_ms_); }

    const gnss::GnssSolution& solution() const { return solution_; }

    gnss::FixReject last_reject() const { return validity_.last_reject(); }
    uint32_t rejected() const { return validity_.rejected(); }

    uint32_t baud_rate() const { return kBaudCandidates[baud_index_]; }

    // INFO: gn 09jun25 SoftRF subtracts a per-chip latency the same way (driver/RF.cpp:236-260)
    uint16_t pps_latency_ms() const {
        return static_cast<uint16_t>(io::wire_ms(kBurstBytes, baud_rate()));
    }

    uint32_t port_overruns() const { return uart_.overruns(); }

    uint32_t baud_raises() const { return baud_raises_; }
    uint32_t raise_fallbacks() const { return raise_fallbacks_; }

    ports::GnssHealth health() const override {
        ports::GnssHealth reported{};
        reported.baud = baud_rate();
        reported.overruns = port_overruns();
        reported.sentences = sentences();
        reported.rejected = rejected();
        reported.pps_latency_ms = pps_latency_ms();
        reported.last_reject = last_reject();
        reported.config_stage = stage_;
        reported.identified = identified();
        reported.rate_held = holds_rate();
        return reported;
    }

    bool identified() const { return parser_.identified(); }
    const char* firmware_version() const { return parser_.firmware_version(); }

    // INFO: fc 20nov26 a factory reset loses our configuration, so the sequence runs again behind
    // it
    void request_restart(ports::Restart kind) override;

    void request_satellites_in_view(bool wanted) { satellites_in_view_wanted_ = wanted; }
    bool satellites_in_view_live() const {
        return satellites_in_view_every_ != 0 && satellites_in_view_wanted_;
    }
    // INFO: fc 20nov26 true when the last published solution closed a burst that carried a GSV set
    bool levels_fresh() const { return levels_fresh_; }

    const gnss::SkyView& sky() const { return parser_.sky(); }

    // INFO: fc 20nov26 the DFU health gate watches this, which a silent or wrong-baud receiver
    // never moves off zero
    uint32_t sentences() const { return parser_.solution().sentences; }

   private:
    static constexpr size_t kReadChunkBytes = 64;

    static constexpr uint8_t kNoRestart = 0xFF;

    void start_sequence(uint32_t now_ms);
    void start_rate(uint32_t now_ms);
    void seek_rate(uint32_t now_ms);
    void enter(ConfigStage stage, uint32_t now_ms);
    uint32_t window_steps() const { return cadence_steps_ - verify_steps_; }
    uint32_t window_sentences() const { return parser_.solution().sentences - verify_sentences_; }
    bool silent(uint32_t now_ms) const;
    void start_probing(uint32_t now_ms);
    void listen(uint32_t now_ms);
    void probe(uint32_t now_ms);
    bool step_baud();
    void begin_reset(uint32_t now_ms);
    void hold_reset(uint32_t now_ms);
    void recover(uint32_t now_ms);
    uint32_t recovery_backoff_ms() const;
    void retry_bring_up(uint32_t now_ms);
    void flush();
    void settle(uint32_t now_ms);
    void send_next(uint32_t now_ms);
    void begin_wake(uint32_t now_ms);
    void send(const char* sentence, uint32_t now_ms);
    void wake(uint32_t now_ms);
    void verify_failed(uint32_t now_ms);
    bool obeying() const;
    void begin_verify(uint32_t now_ms);
    bool raise_baud(uint32_t now_ms);
    bool port_can_retune();
    bool adopt_baud(uint32_t baud);
    bool next_baud();
    void pace_satellites_in_view(uint32_t now_ms);
    uint8_t satellites_in_view_every() const;
    static const char* satellites_in_view_command(uint8_t every);
    bool verified() const;
    bool on_cadence() const;
    void observe_cadence();
    void watch_cadence(uint32_t now_ms);
    void rate_failed(uint32_t now_ms);
    bool newer_than_published(int32_t stamp_ms) const;
    bool behind_published(int32_t stamp_ms) const;

    // INFO: fc 13sep26 RMC is last in the cycle, so it is the sentence that completes a solution
    static constexpr gnss::Sentence kBurstClosingSentence = gnss::Sentence::Rmc;

    ports::SolutionRate solution_rate_;
    io::Uart& uart_;
    io::UartRate& rate_;
    io::Gpio* reset_;
    int reset_pin_;
    gnss::NmeaParser parser_{};
    gnss::FixValidity validity_{};
    gnss::GnssSolution solution_{};
    uint32_t unrequested_at_verify_{0};

    ConfigStage stage_{ConfigStage::Idle};
    uint32_t serviced_ms_{0};
    uint32_t last_command_ms_{0};
    uint32_t verify_start_ms_{0};
    uint32_t verify_sentences_{0};
    const char* const* sequence_{kCommands};
    int sequence_count_{kCommandCount};
    int next_command_{0};
    Cadence cadence_{Cadence::Second};
    uint32_t recoveries_{0};
    int failed_rounds_{0};
    uint8_t rate_attempts_{0};
    uint32_t entered_ms_{0};
    uint32_t heard_ms_{0};
    uint32_t heard_sentences_{0};
    uint32_t probe_round_ms_{0};
    int probe_bauds_{0};
    uint8_t silent_rounds_{0};
    bool reset_released_{false};
    uint32_t released_ms_{0};
    int32_t last_stamp_ms_{-1};
    uint32_t unstamped_{0};
    uint32_t cadence_steps_{0};
    uint32_t verify_unstamped_{0};
    uint32_t verify_steps_{0};
    int32_t published_stamp_ms_{-1};
    bool relabeled_{false};
    int baud_index_{0};
    uint32_t baud_before_raise_{kFactoryBaudRate};
    uint8_t baud_tried_{1};
    uint32_t baud_raises_{0};
    uint32_t raise_fallbacks_{0};
    uint8_t attempts_{0};
    uint8_t pending_restart_{kNoRestart};
    bool satellites_in_view_wanted_{false};
    uint8_t satellites_in_view_every_{0};
    bool levels_heard_{false};
    bool burst_levels_{false};
    bool levels_fresh_{false};
};

}  // namespace skyblip::parts

#endif
