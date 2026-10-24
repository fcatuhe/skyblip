#ifndef SKYBLIP_PORTS_GNSS_H
#define SKYBLIP_PORTS_GNSS_H

#include <cstdint>

#include "core/gnss/validity.h"

namespace skyblip::ports {

enum class Restart : uint8_t { Hot, Warm, Cold, Factory };

enum class GnssConfigStage : uint8_t {
    Idle,
    Restarting,
    Waking,
    Identifying,
    Sending,
    Verifying,
    Confirming,
    Ready,
    Degraded
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
    }
    return "?";
}

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
