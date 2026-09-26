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

constexpr uint32_t wire_ms(uint32_t bytes, uint32_t baud) { return bytes * 10 * 1000 / baud; }

class L76k : public ports::Gnss {
   public:
    explicit L76k(io::Uart& uart, io::UartRate& rate = io::kFixedUartRate)
        : uart_(uart), rate_(rate) {}

    using Config = ports::GnssConfig;

    // INFO: gn 09jun25 t_echo_plus.dts:278 `current-speed` must equal this, nothing checks it
    static constexpr uint32_t kBaudRate = 9600;

    // INFO: fc 19sep26 $PCAS01 takes 0..5 for 4800..115200, L76K protocol spec V1.1 SS2.3.1
    static constexpr uint32_t kTargetBaudRate = 115200;
    static constexpr const char* kBaudCommand = "$PCAS01,5*19\r\n";

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

    // INFO: fc 13sep26 one solution per second, transmitted extrapolated to the burst's own instant
    static constexpr uint32_t kFixRateHz = 1;
    static constexpr uint32_t kSolutionPeriodMs = 1000;
    static constexpr uint32_t kFixPeriodMs = 1000 / kFixRateHz;

    // INFO: fc 13sep26 GGA + three GSA (one per constellation) + RMC, the widest burst we ask for
    static constexpr uint32_t kBurstBytes = 320;
    static constexpr uint32_t kBurstMs = wire_ms(kBurstBytes, kTargetBaudRate);

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

    // INFO: fc 13sep26 GSA is the only sentence carrying VDOP, which G.1.15 asks us to claim
    static constexpr bool kGsaEnabled = true;

    // INFO: fc 18sep26 $PCAS03 takes a null per field meaning keep, so these two move nGSV alone
    static constexpr const char* kSatellitesInViewOn = "$PCAS03,,,,1,,,,,,,,,,*33\r\n";
    static constexpr const char* kSatellitesInViewOff = "$PCAS03,,,,0,,,,,,,,,,*32\r\n";

    // INFO: fc 18sep26 four talkers, up to 32 satellites, four to a sentence and 72 bytes each
    static constexpr uint32_t kSatellitesInViewBytes = 648;
    static constexpr uint32_t kSearchingBurstBytes = kBurstBytes + kSatellitesInViewBytes;
    static constexpr uint32_t kSearchingBurstMs = wire_ms(kSearchingBurstBytes, kTargetBaudRate);
    static_assert(kSearchingBurstMs < kFixPeriodMs,
                  "the burst must fit inside one solution period, satellites in view and all");

    // $PCAS10 reboots the receiver. It answers nothing for about a second after
    // it, so the sequence behind a factory reset waits before it starts talking.
    static constexpr uint32_t kRestartSettleMs = 1000;
    static constexpr const char* kRestartCommands[4] = {
        "$PCAS10,0*1C\r\n",
        "$PCAS10,1*1D\r\n",
        "$PCAS10,2*1E\r\n",
        "$PCAS10,3*1F\r\n",
    };

    // INFO: fc 13sep26 the rate we ask for is the factory rate, so obedience is the sentence set
    static constexpr uint32_t kVerifyWindowMs = 3000;
    static constexpr uint32_t kMinVerifyUpdates = 2;
    static constexpr uint8_t kMaxConfigAttempts = 3;

    // Drive the configuration sequence. Until it has run the receiver is on its
    // factory defaults, which is a degraded receiver wearing a working one's face.
    void service(uint32_t now_ms);

    Config config_state() const { return state_; }
    bool configured() const { return state_ == Config::Ready; }
    bool degraded() const { return state_ == Config::Degraded; }

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
    gnss::FixReject reject_reason() const { return validity_.last_reject(); }
    uint32_t rejected() const { return validity_.rejected(); }

    // The rate we are actually talking to the receiver at, which is only the
    // devicetree's rate until autobaud has had to move.
    uint32_t baud_rate() const { return kBaudCandidates[baud_index_]; }

    // INFO: gn 09jun25 SoftRF subtracts a per-chip latency the same way (driver/RF.cpp:236-260)
    uint16_t pps_latency_ms() const {
        return static_cast<uint16_t>(wire_ms(kBurstBytes, baud_rate()));
    }

    uint32_t port_overruns() const { return uart_.overruns(); }

    ports::GnssHealth health() const override {
        ports::GnssHealth h{};
        h.baud = baud_rate();
        h.overruns = port_overruns();
        h.sentences = updates();
        h.rejected = rejected();
        h.pps_latency_ms = pps_latency_ms();
        h.reject = reject_reason();
        h.config = state_;
        h.identified = identified();
        return h;
    }

    // Did the part name itself, and as what. An unidentified receiver still gets
    // the $PCAS sequence, because the alternative is no configuration at all,
    // but the self-test says so and a support case has the firmware string.
    bool identified() const { return parser_.identified(); }
    const char* firmware_version() const { return parser_.firmware_version(); }

    // Throw away the receiver's stored state. A factory reset takes our
    // configuration with it, so the sequence runs again behind it.
    void request_restart(ports::Restart kind) override;

    void request_satellites_in_view(bool wanted) { gsv_wanted_ = wanted; }
    bool satellites_in_view_live() const { return gsv_on_ && gsv_wanted_; }

    const gnss::SkyView& sky() const { return parser_.sky(); }

    // Sentences the parser accepted since boot. A receiver that is wired but
    // silent (or babbling at the wrong baud) never moves this off zero, which is
    // what the DFU health gate watches.
    uint32_t updates() const { return parser_.solution().updates; }

   private:
    static constexpr size_t kChunk = 64;

    static constexpr uint8_t kNoRestart = 0xFF;

    void start_sequence(uint32_t now_ms);
    void send_next(uint32_t now_ms);
    void begin_wake(uint32_t now_ms);
    void send(const char* sentence, uint32_t now_ms);
    void verify_failed(uint32_t now_ms);
    bool obeying() const;
    void begin_verify(uint32_t now_ms);
    bool raise_baud(uint32_t now_ms);
    bool port_can_retune();
    bool adopt_baud(uint32_t baud);
    bool next_baud();

    // INFO: fc 13sep26 RMC is last in the cycle, so it is the sentence that completes a solution
    static constexpr gnss::Sentence kBurstClosingSentence = gnss::Sentence::Rmc;

    io::Uart& uart_;
    io::UartRate& rate_;
    gnss::NmeaParser parser_{};
    gnss::FixValidity validity_{};
    gnss::GnssSolution solution_{};
    uint32_t verify_unrequested_{0};

    Config state_{Config::Idle};
    uint32_t serviced_ms_{0};
    uint32_t last_command_ms_{0};
    uint32_t verify_start_ms_{0};
    uint32_t verify_updates_{0};
    int next_command_{0};
    int baud_index_{0};
    uint32_t baud_before_raise_{kBaudRate};
    uint8_t baud_tried_{1};
    uint8_t attempts_{0};
    uint8_t pending_restart_{kNoRestart};
    bool gsv_wanted_{false};
    bool gsv_on_{false};
};

}  // namespace skyblip::parts

#endif
