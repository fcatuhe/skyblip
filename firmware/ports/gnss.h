#ifndef SKYBLIP_PORTS_GNSS_H
#define SKYBLIP_PORTS_GNSS_H

#include <cstdint>

#include "core/gnss/validity.h"

namespace skyblip::ports {

enum class Restart : uint8_t { Hot, Warm, Cold, Factory };

enum class GnssConfigStage : uint8_t {
    Idle = 0,
    Restarting = 1,
    Waking = 2,
    Identifying = 3,
    Sending = 4,
    Verifying = 5,
    Confirming = 6,
    Ready = 7,
    Degraded = 8,
    Probing = 9,
    Resetting = 10
};

constexpr const char* to_string(GnssConfigStage state) {
    switch (state) {
        case GnssConfigStage::Idle: return "IDLE";
        case GnssConfigStage::Restarting: return "RESTART";
        case GnssConfigStage::Waking: return "WAKING";
        case GnssConfigStage::Identifying: return "IDENT";
        case GnssConfigStage::Sending: return "SENDING";
        case GnssConfigStage::Verifying: return "VERIFY";
        case GnssConfigStage::Confirming: return "CONFIRM";
        case GnssConfigStage::Ready: return "READY";
        case GnssConfigStage::Degraded: return "DEGRADED";
        case GnssConfigStage::Probing: return "PROBING";
        case GnssConfigStage::Resetting: return "RESET";
    }
    return "?";
}

enum class GnssCommand : uint8_t {
    None = 0,
    Wake = 1,
    Identify = 2,
    Constellations = 3,
    SentenceSet = 4,
    DynamicModel = 5,
    Rate1Hz = 6,
    Baud115200 = 7,
    Rate4Hz = 8,
    Gsa4 = 9,
    Rate2Hz = 10,
    Gsa2 = 11,
    GsvOff = 12,
    GsvEvery = 13,
    GsvPaced5 = 14,
    GsvPaced9 = 15,
    RestartHot = 16,
    RestartWarm = 17,
    RestartCold = 18,
    RestartFactory = 19,
    ResetPulse = 20,
};

enum class GnssVerdict : uint8_t {
    None = 0,
    Passed = 1,
    Silent = 2,
    Disobeying = 3,
    OffCadence = 4,
    Drifted = 5
};

enum class GnssEventKind : uint8_t { Command = 0, Stage = 1, Verdict = 2 };

struct GnssEvent {
    GnssEventKind kind{GnssEventKind::Stage};
    GnssCommand command{GnssCommand::None};
    GnssVerdict verdict{GnssVerdict::None};
    GnssConfigStage stage{GnssConfigStage::Idle};
    uint32_t at_ms{0};
    uint32_t baud{0};
    uint16_t interval_ms{0};
    uint16_t interval_seen_ms{0};
    int16_t stamp_ms{-1};
    uint8_t steps{0};
    uint8_t heard{0};
};

struct GnssBringUp {
    uint16_t interval_ms{0};
    uint16_t interval_seen_ms{0};
    GnssVerdict verdict{GnssVerdict::None};
    uint8_t verify_steps{0};
    GnssVerdict quarter_verdict{GnssVerdict::None};
    uint8_t quarter_steps{0};
    uint32_t commands{0};
    GnssCommand command{GnssCommand::None};
    uint32_t command_at_ms{0};
    uint32_t probes{0};
    uint32_t resets{0};
};

struct GnssHealth {
    uint32_t baud{0};
    uint32_t overruns{0};
    uint32_t sentences{0};
    uint32_t rejected{0};
    uint16_t pps_latency_ms{0};
    gnss::FixReject last_reject{gnss::FixReject::None};
    GnssConfigStage config_stage{GnssConfigStage::Idle};
    bool identified{false};
};

class Gnss {
   public:
    virtual ~Gnss() = default;

    virtual void request_restart(Restart) {}

    virtual GnssHealth health() const { return {}; }
};

}  // namespace skyblip::ports

#endif
