#ifndef SKYBLIP_PRODUCTS_COMMON_SERVICES_NMEA_H
#define SKYBLIP_PRODUCTS_COMMON_SERVICES_NMEA_H

#include "core/events/link.h"
#include "core/gnss/nmea.h"
#include "core/traffic/alarm.h"
#include "core/traffic/table.h"
#include "products/common/services/config_service.h"
#include "runtime/service.h"

namespace skyblip::common {

class NmeaService : public runtime::Service {
   public:
    // INFO: nm 04aug26 ADS-L and ALP-TAS key once a second, so a faster pass repeats a position and
    // a slower one makes the map jump
    static constexpr uint32_t kMovingTargetRedrawPeriodMs = 1000;

    // INFO: nm 04aug26 core/traffic's freshness rule, so the tablet never holds a target longer
    // than the device would alarm on it
    static constexpr uint32_t kTargetRefreshCeilingMs = traffic::kAlertMaxAgeMs;
    static constexpr int kPassesPerRefreshCeiling =
        static_cast<int>(kTargetRefreshCeilingMs / kMovingTargetRedrawPeriodMs);

    static constexpr int kTargetsPerPass =
        (traffic::TrafficTable::kMaxTargets + kPassesPerRefreshCeiling - 1) /
        kPassesPerRefreshCeiling;
    static_assert(kTargetsPerPass * kPassesPerRefreshCeiling >= traffic::TrafficTable::kMaxTargets,
                  "a full table would not be refreshed inside its bound");

    // INFO: nm 04aug26 a pass waits while own-ship's burst is armed, never longer than this, so the
    // cadence stays a cadence
    static constexpr uint32_t kPassDeferralCeilingMs = 100;

    // INFO: fc 25nov26 a whole second this late is lost, so the pass goes without it
    static constexpr uint32_t kWholeSecondWaitMs = gnss::kSolutionLatencyCeilingMs;

    // INFO: fc 25nov26 two passes this close would send the tablet one second twice
    static constexpr uint32_t kPassGapFloorMs = 500;

    // INFO: nm 04aug26 the largest BLE frame: 251 bytes of link-layer payload, less 4 for L2CAP and
    // 3 for the notification header
    static constexpr int kFrameBytes = 244;

    // INFO: fc 20nov26 wider than the worst case test_nmea_out.cpp pins, and checked where it is
    // used
    static constexpr int kSentenceBytes = 128;

    // INFO: fc 20nov26 the 5 are $PFLAU, $GPRMC, $GPGGA, $PGRMZ and $LK8EX1
    static constexpr int kMaxPassSentences = 5 + kTargetsPerPass;
    static constexpr int kPassBytes = kMaxPassSentences * kSentenceBytes;

    NmeaService(runtime::Context& context, const ConfigService& config)
        : runtime::Service(context), config_(config) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;
    uint32_t due_in_ms(uint32_t now_ms) const override;

    uint32_t link_drops() const { return link_drops_; }
    bool enabled() const { return enabled_; }

   private:
    bool listening() const;
    void note_whole_second(uint32_t now_ms);
    bool pass_due(uint32_t now_ms);
    uint32_t wait_for_pass_ms(uint32_t now_ms) const;
    void run_pass(uint32_t now_ms);
    void emit_status(uint32_t now_ms);
    void emit_ownship();
    const model::Ownship* unsent_solution() const;
    void emit_altitude();
    void emit_vario_and_battery();
    void emit_targets(uint32_t now_ms);
    void write(const char* sentence, int bytes);
    void drain();
    void abandon_pass();

    struct Resume {
        int end;
        int cursor;
    };

    const ConfigService& config_;
    char pass_[kPassBytes]{};
    char sentence_[kSentenceBytes]{};
    Resume resumes_[kTargetsPerPass]{};
    int resume_count_{0};
    int pass_bytes_{0};
    int pass_sent_{0};
    uint32_t last_pass_ms_{0};
    uint32_t landed_second_s_{0};
    uint32_t landed_at_ms_{0};
    uint32_t passed_second_s_{0};
    uint32_t held_since_ms_{0};
    uint32_t sent_second_s_{0};
    uint32_t sent_utc_s_{0};
    uint32_t link_drops_{0};
    int payload_{ports::kMinimumLinkPayloadBytes};
    uint16_t sent_utc_ms_{0};
    // INFO: fc 20nov26 advances only past a target that went out, so a pass a link cut short
    // resends its tail first
    int cursor_{0};
    bool passed_once_{false};
    bool sent_solution_{false};
    bool held_{false};
    bool enabled_{false};
};

}  // namespace skyblip::common

#endif
