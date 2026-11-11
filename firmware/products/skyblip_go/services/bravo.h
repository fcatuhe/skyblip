#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_BRAVO_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_BRAVO_H

#include <cstdint>

#include "core/comms/config.h"
#include "core/events/central.h"
#include "ports/central.h"
#include "products/common/services/alarm.h"
#include "products/skyblip_go/own_icao_warning.h"
#include "products/skyblip_go/pairing.h"
#include "products/skyblip_go/settings.h"
#include "runtime/service.h"

namespace skyblip::go {

class BravoService : public runtime::Service {
   public:
    // INFO: fc 11nov26 schemas/link.v1.json: the Go writes peer_claim once a second while paired
    static constexpr uint32_t kClaimPeriodMs = 1000;
    // INFO: fc 11nov26 unverified: a Bravo in range answers a connection in a few scan windows
    static constexpr uint32_t kConnectCeilingMs = 10000;
    // INFO: fc 11nov26 unverified: #192's proposed 30 s press window, a health period, a margin
    static constexpr uint32_t kAnswerCeilingMs = 35000;
    // INFO: fc 11nov26 GAP fast scanning, 30 ms in 60 (Core 6.2 Vol 3 Part C App A), pilot waiting
    static constexpr uint32_t kPairingScanWindowUs = 30000;
    static constexpr uint32_t kPairingScanPeriodUs = 60000;
    // INFO: fc 11nov26 GAP slow scanning 1, 11.25 ms in 1.28 s, until #194 measures the cost
    static constexpr uint32_t kReconnectScanWindowUs = 11250;
    static constexpr uint32_t kReconnectScanPeriodUs = 1280000;

    BravoService(runtime::Context& context, Settings& settings, comms::ConfigService& config,
                 ports::Central& central, const common::AlarmService& alarm)
        : runtime::Service(context),
          settings_(settings),
          config_(config),
          central_(central),
          alarm_(alarm) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;

    void begin_pairing(uint32_t now_ms);
    void end_pairing();
    void pair(int found_index, uint32_t now_ms);
    void unpair();

    BravoStep step() const { return step_; }
    BravoRefusal refusal() const { return refusal_; }
    BravoLink link() const { return link_; }
    int found_count() const { return found_count_; }
    const events::BluetoothAddress& found(int index) const { return found_[index]; }
    const events::BluetoothAddress& pairing_with() const { return pairing_with_; }
    OwnIcaoWarning& own_icao_warning() { return own_icao_warning_; }
    const OwnIcaoWarning& own_icao_warning() const { return own_icao_warning_; }

   private:
    void take_event(const events::CentralEvent& event, uint32_t now_ms);
    void take_frame(const events::RxFrame& frame);
    void came_up(uint16_t payload_bytes, uint32_t now_ms);
    void went_down();
    void hear(const events::CentralEvent& event);
    void start_scan();
    void refuse(BravoRefusal refusal);
    void refuse_protocol();
    void release_unless_paired();
    void connect(const events::BluetoothAddress& address, uint32_t window_us, uint32_t period_us);
    void claim(uint32_t now_ms);
    bool linked() const { return up_ && (step_ == BravoStep::Waiting || link_ == BravoLink::Up); }

    Settings& settings_;
    comms::ConfigService& config_;
    ports::Central& central_;
    const common::AlarmService& alarm_;
    OwnIcaoWarning own_icao_warning_{};
    events::BluetoothAddress found_[kBravoFoundRows]{};
    events::BluetoothAddress pairing_with_{};
    int found_count_{0};
    BravoStep step_{BravoStep::Idle};
    BravoRefusal refusal_{BravoRefusal::None};
    BravoLink link_{BravoLink::Down};
    uint32_t step_since_ms_{0};
    uint32_t claimed_ms_{0};
    uint32_t boot_epoch_{0};
    bool connecting_{false};
    bool up_{false};
};

}  // namespace skyblip::go

#endif
