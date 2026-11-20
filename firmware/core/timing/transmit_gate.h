// INFO: fc 19nov26 what own-ship transmits ADS-L under, each requirement's clause in README.md
#ifndef SKYBLIP_CORE_TIMING_TRANSMIT_GATE_H
#define SKYBLIP_CORE_TIMING_TRANSMIT_GATE_H

#include <cstdint>
#include <optional>

namespace skyblip::timing {

// INFO: fc 19nov26 in the order a reason is named: the first one unmet is the one the glass says
enum class NoTx : uint8_t { None = 0, Address = 1, Radio = 2, Gnss = 3, Fix = 4, Utc = 5, Pps = 6 };

constexpr const char* to_string(NoTx reason) {
    switch (reason) {
        case NoTx::None: return "";
        case NoTx::Address: return "ADDR";
        case NoTx::Radio: return "RADIO";
        case NoTx::Gnss: return "GNSS";
        case NoTx::Fix: return "FIX";
        case NoTx::Utc: return "UTC";
        case NoTx::Pps: return "PPS";
    }
    return "";
}

struct Requirements {
    bool address{false};
    bool radio{false};
    bool receiver{false};
    bool fix{false};
    bool settled{false};
    bool utc{false};
    bool pps{false};
};

NoTx first_unmet(const Requirements& met);

class TransmitGate {
   public:
    // INFO: fc 19nov26 a failure shown only once it lasts, so a short gap never flickers
    static constexpr uint32_t kNoTxHoldMs = 10000;
    // INFO: fc 19nov26 the sky's requirements are judged this long after the first fix
    static constexpr uint32_t kVerdictSettleMs = 10000;

    void judge(const Requirements& met, uint32_t now_ms);

    // INFO: fc 19nov26 the settle after the first fix holds the burst and is never a reason
    bool transmits() const { return transmits_; }
    NoTx unmet() const { return unmet_; }
    NoTx shown() const { return shown_; }

   private:
    bool judged(uint32_t now_ms) const;

    std::optional<uint32_t> first_fix_ms_{};
    std::optional<uint32_t> unmet_since_ms_{};
    NoTx unmet_{NoTx::Address};
    NoTx shown_{NoTx::None};
    bool transmits_{false};
};

}  // namespace skyblip::timing

#endif
