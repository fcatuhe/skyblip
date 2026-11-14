#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_PEER_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_PEER_H

#include <array>
#include <cstdint>
#include <optional>

#include "core/comms/config.h"
#include "core/comms/link_sessions.h"
#include "core/comms/peer.h"
#include "core/events/central.h"
#include "ports/adsb_receiver.h"
#include "products/skyblip_bravo/gate_reference.h"
#include "products/skyblip_bravo/observation_pacer.h"
#include "products/skyblip_bravo/own_transponder.h"
#include "products/skyblip_bravo/pairing.h"
#include "products/skyblip_bravo/settings.h"
#include "runtime/service.h"

namespace skyblip::bravo {

// INFO: fc 11nov26 Bravo's side of the peer endpoint: the pin, health, observations (README.md)
class PeerService : public runtime::Service {
   public:
    // INFO: fc 11nov26 schemas/link.v1.json health: once a second whatever the traffic
    static constexpr uint32_t kHealthPeriodMs = 1000;
    // INFO: fc 12nov26 unverified: three claim periods, so one lost claim keeps Bravo silent (#194)
    static constexpr uint32_t kAlarmRequestMaxAgeMs = 3000;

    PeerService(runtime::Context& context, Settings& settings, comms::ConfigService& config,
                const ports::AdsbReceiver& receiver, GateReference& gate,
                const OwnTransponder& own_transponder)
        : runtime::Service(context),
          settings_(settings),
          config_(config),
          receiver_(receiver),
          gate_(gate),
          own_transponder_(own_transponder) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;

    bool answer(uint32_t now_ms);
    bool requested() const { return pairing_.requested(); }
    // INFO: fc 11nov26 both sides hold the pin, the one state a Go+ may take the alarm in (#179)
    bool paired() const { return pairing_.pinned(); }
    uint32_t boot_epoch() const { return boot_epoch_; }
    uint32_t refused() const { return refused_; }
    uint32_t dropped() const { return dropped_; }

   private:
    struct Go {
        events::Peer peer{};
        events::BluetoothAddress address{};
        bool asked{false};
        bool used{false};
    };

    struct AlarmRequest {
        events::BluetoothAddress go{};
        uint32_t heard_ms{0};
    };

    void forget_gone();
    void hear_alarm_request(const events::BluetoothAddress& go, bool requested, uint32_t now_ms);
    void expire_alarm_request(uint32_t now_ms);
    bool grants_alarm_to(const events::BluetoothAddress& go) const;
    void take(const events::RxFrame& frame, uint32_t now_ms);
    Go* go_of(const events::RxFrame& frame, bool& new_connection);
    void keep_pin();
    void adopt_own_icao(uint32_t own_icao);
    void describe_self(comms::PeerHealth& health) const;
    void send_health(const GateChoice& choice, bool utc_valid, uint32_t now_ms);
    void send_observations(const GateChoice& choice, bool utc_valid, uint32_t now_ms);
    void serve(int slot, const traffic::Target& target,
               std::array<bool, ObservationPacer::kRecipients>& blocked, uint32_t now_ms);

    Settings& settings_;
    comms::ConfigService& config_;
    const ports::AdsbReceiver& receiver_;
    GateReference& gate_;
    const OwnTransponder& own_transponder_;
    Pairing pairing_{settings_.peer};
    ObservationPacer pacer_{};
    std::array<Go, ObservationPacer::kRecipients> gos_{};
    PeerPin kept_{};
    std::optional<AlarmRequest> alarm_request_{};
    uint32_t boot_epoch_{0};
    uint32_t sequence_{0};
    int next_target_{0};
    uint32_t health_sent_ms_{0};
    uint32_t refused_{0};
    uint32_t dropped_{0};
    bool health_owed_{true};
    bool observed_{false};
};

}  // namespace skyblip::bravo

#endif
