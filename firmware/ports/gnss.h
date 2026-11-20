#ifndef SKYBLIP_PORTS_GNSS_H
#define SKYBLIP_PORTS_GNSS_H

#include <cstdint>

#include "core/gnss/validity.h"

namespace skyblip::ports {

enum class Restart : uint8_t { Hot, Warm, Cold, Factory };

enum class SolutionRate : uint8_t { Second, Half };

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

struct GnssHealth {
    uint32_t baud{0};
    uint32_t overruns{0};
    uint32_t sentences{0};
    uint32_t rejected{0};
    uint16_t pps_latency_ms{0};
    gnss::FixReject last_reject{gnss::FixReject::None};
    GnssConfigStage config_stage{GnssConfigStage::Idle};
    bool identified{false};
    // INFO: fc 19nov26 solving at the rate the product asked: 2 Hz on the Go, only at 115200
    bool rate_held{false};
};

class Gnss {
   public:
    virtual ~Gnss() = default;

    virtual void request_restart(Restart) {}

    virtual GnssHealth health() const { return {}; }
};

}  // namespace skyblip::ports

#endif
