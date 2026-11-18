// A GNSS receiver has no registers to drive: it produces. This driver owns the
// UART and the NMEA parser and hands out a fix, which the board pushes onto the bus.
// It also owns the two things the receiver will not do for us: come up in the
// aviation dynamic model, on the constellations and at the rate we need, and say
// whether what it is producing is a fix at all.
#ifndef SKYBLIP_HARDWARE_PARTS_L76K_H
#define SKYBLIP_HARDWARE_PARTS_L76K_H

#include "core/gnss/nmea.h"
#include "core/gnss/validity.h"
#include "hardware/io/io.h"
#include "ports/gnss.h"

namespace skyblip::parts {

// The rate port is io::UartRate (hardware/io/io.h): a board whose platform can
// retune the port hands the driver one, and autobaud recovery becomes available.

class L76k : public ports::Gnss {
   public:
    explicit L76k(io::Uart& uart, io::UartRate& rate = io::kFixedUartRate,
                  io::Gpio* reset = nullptr, int reset_pin = -1)
        : uart_(uart), rate_(rate), reset_(reset), reset_pin_(reset_pin) {}

    using ConfigStage = ports::GnssConfigStage;

    // INFO: gn 09jun25 t_echo_plus.dts:278 `current-speed` must equal this, nothing checks it
    static constexpr uint32_t kFactoryBaudRate = 9600;

    // INFO: fc 19sep26 $PCAS01 takes 0..5 for 4800..115200, L76K protocol spec V1.1 SS2.3.1
    static constexpr uint32_t kTargetBaudRate = 115200;

    // INFO: fc 19sep26 SoftRF measures this part talking 70 ms after the second (GNSS.cpp:83-91)
    static constexpr uint32_t kBurstStartMs = 70;

    // INFO: fc 03aug26 A receiver that comes up at another rate (a returned unit
    // reflashed by someone else, a module whose backup domain kept a $PCAS01) is
    // silently GNSS-less forever if 9600 is an assumption. moshe-braner walks the
    // rates until NMEA appears (.../src/driver/GNSS.cpp:1700-1739); OGN cycles to
    // the next rate after 2 s with no valid data (src/gps.cpp:89-96, 1205-1222).
    // Ours is the AT6558's own list, most likely first.
    static constexpr int kBaudCandidateCount = 6;
    static constexpr uint32_t kBaudCandidates[kBaudCandidateCount] = {
        9600, 115200, 38400, 57600, 19200, 4800,
    };

    // INFO: fc 18nov26 CASIC protocol V4.2.0.3 1.6.3: 250 is 4 Hz, solutions on G.1.1's quarters
    static constexpr uint32_t kSolutionPeriodMs = 250;
    static constexpr uint32_t kFactorySolutionPeriodMs = 1000;

    // INFO: fc 13sep26 GGA + three GSA (one per constellation) + RMC, the widest burst we ask for
    static constexpr uint32_t kBurstBytes = 320;
    static constexpr uint32_t kBurstMs = io::wire_ms(kBurstBytes, kTargetBaudRate);

    // INFO: fc 03aug26 The L76K wakes on UART activity, so a receiver that is
    // asleep when we start talking eats the first thing we say. SoftRF sends one
    // 0x00 and waits 500 ms before it probes anything (oss/SoftRF-lyusupov
    // .../src/driver/GNSS.cpp:1383-1387).
    static constexpr uint8_t kWakeByte = 0x00;
    static constexpr uint32_t kWakeDelayMs = 500;

    // INFO: fc 03aug26 $PCAS06 asks the receiver to name itself and it answers
    // "$GPTXT,01,01,02,SW=<version>". SoftRF treats that exchange as the proof
    // the part is an AT6558 before it trusts a single $PCAS sentence
    // (.../src/driver/GNSS.cpp:981-1010), and logs the version for support.
    static constexpr uint32_t kIdentifyWindowMs = 1000;

    // INFO: gn 09jun25 SoftRF sends the first three 250 ms apart (.../driver/GNSS.cpp:1029-1057)
    static constexpr uint32_t kCommandGapMs = 250;
    static constexpr int kCommandCount = 4;
    static constexpr ports::GnssCommand kCommands[kCommandCount] = {
        ports::GnssCommand::Constellations,
        ports::GnssCommand::SentenceSet,
        ports::GnssCommand::DynamicModel,
        ports::GnssCommand::Rate1Hz,
    };

    // INFO: fc 18nov26 L76K protocol spec V1.1 SS2.3.2 allows under 1000 ms only at 115200
    static constexpr int kQuarterRateCommandCount = 2;
    static constexpr ports::GnssCommand kQuarterRateCommands[kQuarterRateCommandCount] = {
        ports::GnssCommand::Rate4Hz,
        ports::GnssCommand::Gsa4,
    };
    // INFO: fc 18nov26 2 Hz solves on .000 and .500, both quarters, for a part that refuses 4 Hz
    static constexpr uint32_t kHalfSolutionPeriodMs = 500;
    static constexpr ports::GnssCommand kHalfRateCommands[kQuarterRateCommandCount] = {
        ports::GnssCommand::Rate2Hz,
        ports::GnssCommand::Gsa2,
    };
    // INFO: fc 18nov26 Gsa4 keeps GSA, and the VDOP it carries, once a second
    static constexpr uint8_t kGsaPeriodSolutions = 4;
    static_assert(kGsaPeriodSolutions * kSolutionPeriodMs == kFactorySolutionPeriodMs,
                  "GSA keeps the once a second it had at the factory rate");

    // INFO: fc 13sep26 GSA is the only sentence carrying VDOP, which G.1.15 asks us to claim
    static constexpr bool kGsaEnabled = true;

    // INFO: fc 29sep26 GSV precedes RMC: nav_ms +55-65 ms on bench 0B1B2C, so one set in five
    static constexpr uint8_t kSatellitesInViewPeriodSolutions = 5;
    // INFO: fc 18nov26 $PCAS03 counts to 9, so at 4 Hz a set every 2.25 s is the slowest pace
    static constexpr uint8_t kQuarterSatellitesInViewPeriodSolutions = 9;

    // INFO: fc 18sep26 four talkers, up to 32 satellites, four to a sentence and 72 bytes each
    static constexpr uint32_t kSatellitesInViewBytes = 648;
    static constexpr uint32_t kSearchingBurstBytes = kBurstBytes + kSatellitesInViewBytes;
    static constexpr uint32_t kSearchingBurstMs =
        io::wire_ms(kSearchingBurstBytes, kTargetBaudRate);
    static_assert(kSearchingBurstMs < kSolutionPeriodMs,
                  "the burst must fit inside one solution period, satellites in view and all");

    // $PCAS10 reboots the receiver. It answers nothing for about a second after
    // it, so the sequence behind a factory reset waits before it starts talking.
    static constexpr uint32_t kRestartSettleMs = 1000;
    static constexpr ports::GnssCommand kRestartCommands[4] = {
        ports::GnssCommand::RestartHot,
        ports::GnssCommand::RestartWarm,
        ports::GnssCommand::RestartCold,
        ports::GnssCommand::RestartFactory,
    };
    static const char* sentence_of(ports::GnssCommand command);

    // INFO: fc 18nov26 Go 000001 went silent, NMEA and PPS, and stayed so until rebooted
    static constexpr uint32_t kSilenceMs = 3000;
    static constexpr uint32_t kProbeListenMs = kWakeDelayMs + kFactorySolutionPeriodMs;
    static constexpr uint32_t kProbePeriodMs = 10000;
    static_assert(kBaudCandidateCount * kProbeListenMs < kProbePeriodMs,
                  "a probe round hears every rate once inside its period");
    static constexpr uint8_t kSilentRoundsBeforeReset = 3;
    // INFO: fc 18nov26 Quectel L76K Hardware Design V1.0 4.2.1: RESET_N low at least 100 ms
    static constexpr uint32_t kResetHoldMs = 150;
    static constexpr int kJournalEvents = 8;

    // INFO: fc 13sep26 obedience is the sentence set | 18nov26 and, after 4 or 2 Hz, its cadence
    static constexpr uint32_t kVerifyWindowMs = 3000;
    static constexpr uint32_t kMinVerifySentences = 2;
    static constexpr uint32_t kQuarterVerifyWindowMs = 2000;
    // INFO: fc 18nov26 4 Hz makes eight quarter steps a window, 2 Hz four half steps, others none
    static constexpr uint32_t kMinQuarterSteps = 3;
    static constexpr uint32_t kMinHalfSteps = 2;
    static_assert(kMinQuarterSteps < kQuarterVerifyWindowMs / kSolutionPeriodMs &&
                      kMinHalfSteps < kQuarterVerifyWindowMs / kHalfSolutionPeriodMs,
                  "a part at the asked rate passes with steps to spare");
    static constexpr int32_t kDayMs = 86400000;
    static constexpr uint8_t kMaxConfigAttempts = 3;

    // Drive the configuration sequence. Until it has run the receiver is on its
    // factory defaults, which is a degraded receiver wearing a working one's face.
    void service(uint32_t now_ms);

    ConfigStage config_stage() const { return stage_; }
    enum class Cadence : uint8_t { Second, Half, Quarter };
    bool solving_quarters() const {
        return stage_ == ConfigStage::Ready && cadence_ == Cadence::Quarter;
    }
    Cadence cadence() const { return cadence_; }
    uint32_t solution_period_ms() const;
    bool configured() const { return stage_ == ConfigStage::Ready; }
    bool degraded() const { return stage_ == ConfigStage::Degraded; }

    // Drain whatever the receiver has said into the parser. Returns true when
    // that produced a NEW fix, or when the fix we already published stopped
    // being one: a receiver that goes silent has to be reported, and it says
    // nothing by definition.
    bool poll(uint32_t now_ms);

    // The board polls immediately after service(), so the instant of the last
    // service call is this poll's instant. Ages measured from anywhere else
    // would be measured from a clock this part does not have.
    bool poll() { return poll(serviced_ms_); }

    const gnss::GnssSolution& solution() const { return solution_; }

    // Why the last solution was not a fix, and how many times that has happened.
    // Both are for the self-test page and a support case, and nothing else reads
    // them: the fix's own `valid` is the answer every service uses.
    gnss::FixReject last_reject() const { return validity_.last_reject(); }
    uint32_t rejected() const { return validity_.rejected(); }

    // The rate we are actually talking to the receiver at, which is only the
    // devicetree's rate until autobaud has had to move.
    uint32_t baud_rate() const { return kBaudCandidates[baud_index_]; }

    // INFO: gn 09jun25 SoftRF subtracts a per-chip latency the same way (driver/RF.cpp:236-260)
    uint16_t pps_latency_ms() const {
        return static_cast<uint16_t>(io::wire_ms(kBurstBytes, baud_rate()));
    }

    uint32_t port_overruns() const { return uart_.overruns(); }

    uint32_t baud_raises() const { return baud_raises_; }
    uint32_t raise_fallbacks() const { return raise_fallbacks_; }

    ports::GnssBringUp bring_up() const;
    // INFO: fc 18nov26 the board drains this every pass, a ninth event unread overwrites the oldest
    bool take_event(ports::GnssEvent& out);

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
        return reported;
    }

    // Did the part name itself, and as what. An unidentified receiver still gets
    // the $PCAS sequence, because the alternative is no configuration at all,
    // but the self-test says so and a support case has the firmware string.
    bool identified() const { return parser_.identified(); }
    const char* firmware_version() const { return parser_.firmware_version(); }

    // Throw away the receiver's stored state. A factory reset takes our
    // configuration with it, so the sequence runs again behind it.
    void request_restart(ports::Restart kind) override;

    void request_satellites_in_view(bool wanted) { satellites_in_view_wanted_ = wanted; }
    bool satellites_in_view_live() const {
        return satellites_in_view_every_ != 0 && satellites_in_view_wanted_;
    }
    // The last published solution closed a burst that carried a GSV set.
    bool levels_fresh() const { return levels_fresh_; }

    const gnss::SkyView& sky() const { return parser_.sky(); }

    // Sentences the parser accepted since boot. A receiver that is wired but
    // silent (or babbling at the wrong baud) never moves this off zero, which is
    // what the DFU health gate watches.
    uint32_t sentences() const { return parser_.solution().sentences; }

   private:
    static constexpr size_t kReadChunkBytes = 64;

    static constexpr uint8_t kNoRestart = 0xFF;

    void start_sequence(uint32_t now_ms);
    void start_rate(Cadence cadence, uint32_t now_ms);
    void seek_rate(uint32_t now_ms);
    void enter(ConfigStage stage, uint32_t now_ms);
    void note(ports::GnssEventKind kind, uint32_t now_ms);
    void conclude(ports::GnssVerdict verdict, uint32_t now_ms);
    ports::GnssVerdict failure() const;
    static uint8_t saturated(uint32_t count) {
        return static_cast<uint8_t>(count < 0xFF ? count : 0xFF);
    }
    uint32_t window_steps() const { return cadence_steps_ - verify_steps_; }
    uint32_t window_sentences() const { return parser_.solution().sentences - verify_sentences_; }
    bool silent(uint32_t now_ms) const;
    void start_probing(uint32_t now_ms);
    void listen(uint32_t now_ms);
    void probe(uint32_t now_ms);
    bool step_baud();
    void begin_reset(uint32_t now_ms);
    void hold_reset(uint32_t now_ms);
    void flush();
    void settle(uint32_t now_ms);
    void send_next(uint32_t now_ms);
    void begin_wake(uint32_t now_ms);
    void send(ports::GnssCommand command, uint32_t now_ms);
    void verify_failed(uint32_t now_ms);
    bool obeying() const;
    void begin_verify(uint32_t now_ms);
    bool raise_baud(uint32_t now_ms);
    bool port_can_retune();
    bool adopt_baud(uint32_t baud);
    bool next_baud();
    void pace_satellites_in_view(uint32_t now_ms);
    uint8_t satellites_in_view_every() const;
    static ports::GnssCommand satellites_in_view_command(uint8_t every);
    bool verified() const;
    bool on_cadence() const;
    void observe_cadence();
    void watch_cadence(uint32_t now_ms);
    void rate_failed(uint32_t now_ms);
    bool newer_than_published(int32_t stamp_ms) const;

    // INFO: fc 13sep26 RMC is last in the cycle, so it is the sentence that completes a solution
    static constexpr gnss::Sentence kBurstClosingSentence = gnss::Sentence::Rmc;

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
    const ports::GnssCommand* sequence_{kCommands};
    int sequence_count_{kCommandCount};
    int next_command_{0};
    Cadence cadence_{Cadence::Second};
    bool quarter_tried_{false};
    bool half_refused_{false};
    bool rebooting_{false};
    uint32_t rebooted_ms_{0};
    uint8_t rate_attempts_{0};
    uint32_t entered_ms_{0};
    uint32_t heard_ms_{0};
    uint32_t heard_sentences_{0};
    uint32_t probe_round_ms_{0};
    int probe_bauds_{0};
    uint8_t silent_rounds_{0};
    bool reset_released_{false};
    uint32_t released_ms_{0};
    uint16_t interval_seen_ms_{0};
    ports::GnssBringUp bring_up_{};
    ports::GnssEvent journal_[kJournalEvents]{};
    int journal_next_{0};
    int journal_count_{0};
    int32_t last_stamp_ms_{-1};
    uint32_t unstamped_{0};
    uint32_t cadence_steps_{0};
    uint32_t verify_unstamped_{0};
    uint32_t verify_steps_{0};
    int32_t published_stamp_ms_{-1};
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
