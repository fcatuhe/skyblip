#ifndef SKYBLIP_HARDWARE_PARTS_AG3335_H
#define SKYBLIP_HARDWARE_PARTS_AG3335_H

#include "core/gnss/nmea.h"
#include "core/gnss/validity.h"
#include "hardware/io/io.h"
#include "hardware/parts/ag3335/pair.h"
#include "ports/gnss.h"

namespace skyblip::parts {

class Ag3335 : public ports::Gnss {
   public:
    explicit Ag3335(io::Uart& uart) : uart_(uart) {}

    using ConfigStage = ports::GnssConfigStage;

    // TODO: fc 11nov26 SoftRF nRF52.cpp:4022 and Meshtastic variant.h:126, unverified until #169
    static constexpr uint32_t kBaudRate = 115200;

    // TODO: fc 11nov26 SoftRF's GGA after the second, GNSS.cpp:1342, unverified until #169
    static constexpr uint32_t kBurstStartMs = 90;

    static constexpr uint32_t kSolutionPeriodMs = 1000;

    // INFO: fc 11nov26 GGA, four GSA and RMC, each at NMEA 0183's 82-byte ceiling
    static constexpr uint32_t kSentenceCeilingBytes = 82;
    static constexpr uint32_t kBurstSentences = 6;
    static constexpr uint32_t kBurstBytes = kBurstSentences * kSentenceCeilingBytes;
    static constexpr uint32_t kBurstMs = io::wire_ms(kBurstBytes, kBaudRate);
    static_assert(kBurstStartMs + kBurstMs < kSolutionPeriodMs,
                  "the burst must close inside the second it describes");

    // INFO: fc 11nov26 the reply names the project and S or D for one band or two, SIM65M §2.3.12
    static constexpr const char* kIdentifyCommand = "$PAIR021*39\r\n";

    // TODO: fc 11nov26 Meshtastic GPS.cpp:1353 waits this for $PAIR021, unverified until #169
    static constexpr uint32_t kAckWindowMs = 1000;
    static constexpr uint8_t kCommandAttempts = 3;
    // TODO: fc 11nov26 result 1 waits for a result, SIM65M §2.3.1, for a time unverified until #169
    static constexpr uint32_t kProcessingWindowMs = kCommandAttempts * kAckWindowMs;

    // INFO: fc 11nov26 SoftRF locks sleep on the X1 first, nRF52.cpp:4061-4067, SIM65M §2.3.73
    static constexpr const char* kLockSleep = "$PAIR382,1*2E\r\n";
    // INFO: fc 11nov26 GREB and QZSS, in SIM65M §2.3.36 and dual-band LC29H V1.4 §2.4.15
    static constexpr const char* kConstellations = "$PAIR066,1,1,1,1,1,0*3B\r\n";
    // INFO: fc 11nov26 NMEA 4.10, whose GSA carries the system id in field 18, SIM65M §2.3.61
    static constexpr const char* kNmeaVersion = "$PAIR100,1,0*3A\r\n";
    // INFO: fc 11nov26 types 0..6 are GGA, GLL, GSA, GSV, RMC, VTG and ZDA, SIM65M §2.3.32
    static constexpr const char* kGgaEverySolution = "$PAIR062,0,1*3F\r\n";
    static constexpr const char* kGllOff = "$PAIR062,1,0*3F\r\n";
    // INFO: fc 11nov26 GSA is the only sentence carrying VDOP, as on the L76K
    static constexpr const char* kGsaEverySolution = "$PAIR062,2,1*3D\r\n";
    static constexpr const char* kGsvOff = "$PAIR062,3,0*3D\r\n";
    static constexpr const char* kRmcEverySolution = "$PAIR062,4,1*3B\r\n";
    static constexpr const char* kVtgOff = "$PAIR062,5,0*3B\r\n";
    static constexpr const char* kZdaOff = "$PAIR062,6,0*38\r\n";
    static constexpr const char* kSolutionPeriod = "$PAIR050,1000*12\r\n";
    // INFO: fc 11nov26 SBAS is off in fitness mode, the default of SIM65M §2.3.50
    static constexpr const char* kNormalNavigation = "$PAIR080,0*2E\r\n";
    // INFO: fc 11nov26 SBAS covers EGNOS as the source, SIM65M §2.3.78, and §2.3.80 searches
    static constexpr const char* kSbasCorrections = "$PAIR400,2*20\r\n";
    static constexpr const char* kSbasSearch = "$PAIR410,1*22\r\n";
    // INFO: fc 11nov26 a 100 ms pulse from the first fix on, SIM65M §2.3.141
    static constexpr const char* kPpsAfterFirstFix = "$PAIR752,1,100*3A\r\n";

    // TODO: fc 11nov26 unverified on the X1 until #169 sees its acks, L5 and SBAS
    static constexpr int kCommandCount = 15;
    static constexpr const char* kCommands[kCommandCount] = {
        kLockSleep,       kConstellations,   kNmeaVersion,      kGgaEverySolution,
        kGllOff,          kGsaEverySolution, kGsvOff,           kRmcEverySolution,
        kVtgOff,          kZdaOff,           kSolutionPeriod,   kNormalNavigation,
        kSbasCorrections, kSbasSearch,       kPpsAfterFirstFix,
    };

    // INFO: fc 11nov26 hot, warm, cold and full cold start with factory settings, SIM65M §2.3.4-7
    static constexpr const char* kRestartCommands[4] = {
        "$PAIR004*3E\r\n",
        "$PAIR005*3F\r\n",
        "$PAIR006*3C\r\n",
        "$PAIR007*3D\r\n",
    };

    void service(uint32_t now_ms);

    ConfigStage config_stage() const { return stage_; }
    bool configured() const { return stage_ == ConfigStage::Ready; }
    bool degraded() const { return stage_ == ConfigStage::Degraded; }

    bool poll(uint32_t now_ms);
    bool poll() { return poll(serviced_ms_); }

    const gnss::GnssSolution& solution() const { return solution_; }

    gnss::FixReject last_reject() const { return validity_.last_reject(); }
    uint32_t rejected() const { return validity_.rejected(); }

    static uint16_t pps_latency_ms() { return static_cast<uint16_t>(kBurstStartMs + kBurstMs); }

    uint32_t port_overruns() const { return uart_.overruns(); }

    ports::GnssHealth health() const override {
        ports::GnssHealth reported{};
        reported.baud = kBaudRate;
        reported.overruns = port_overruns();
        reported.sentences = sentences();
        reported.rejected = rejected();
        reported.pps_latency_ms = pps_latency_ms();
        reported.last_reject = last_reject();
        reported.config_stage = stage_;
        reported.identified = identified();
        return reported;
    }

    bool identified() const { return reader_.identified(); }
    const char* firmware_version() const { return reader_.firmware_version(); }
    bool dual_band() const { return reader_.dual_band(); }

    uint32_t refused_commands() const { return refused_commands_; }

    void request_restart(ports::Restart kind) override;

    const gnss::SkyView& sky() const { return parser_.sky(); }

    uint32_t sentences() const { return parser_.solution().sentences; }

   private:
    static constexpr size_t kReadChunkBytes = 64;
    static constexpr uint8_t kNoRestart = 0xFF;
    static constexpr gnss::Sentence kBurstClosingSentence = gnss::Sentence::Rmc;

    void identify(uint32_t now_ms);
    void start_sequence(uint32_t now_ms);
    void send_command(uint32_t now_ms);
    void next_command(uint32_t now_ms);
    void retry_command(uint32_t now_ms);
    void await(const char* sentence, uint32_t now_ms);
    void send(const char* sentence);
    void hear(PairReader::Heard heard);

    io::Uart& uart_;
    gnss::NmeaParser parser_{};
    PairReader reader_{};
    gnss::FixValidity validity_{};
    gnss::GnssSolution solution_{};

    ConfigStage stage_{ConfigStage::Idle};
    uint32_t serviced_ms_{0};
    uint32_t last_command_ms_{0};
    uint32_t refused_commands_{0};
    int next_command_{0};
    uint16_t awaited_id_{0};
    uint8_t attempts_{0};
    uint8_t pending_restart_{kNoRestart};
    PairResult ack_result_{PairResult::Success};
    bool ack_heard_{false};
    bool setting_heard_{false};
    bool sequence_refused_{false};
    bool processing_{false};
};

}  // namespace skyblip::parts

#endif
