#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_BRAVO_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_BRAVO_H

#include <cstdint>
#include <optional>

#include "core/comms/config.h"
#include "core/comms/peer.h"
#include "core/events/central.h"
#include "ports/central.h"
#include "products/common/services/adsb_capture.h"
#include "products/common/services/alarm.h"
#include "products/skyblip_go/aircraft_address_warning.h"
#include "products/skyblip_go/bravo_log.h"
#include "products/skyblip_go/pairing.h"
#include "products/skyblip_go/settings.h"
#include "runtime/service.h"

namespace skyblip::go {

class BravoService : public runtime::Service {
   public:
    // INFO: fc 17nov26 schemas/link.v1.json: a claim once a second, and at once when the call
    // changes
    static constexpr uint32_t kClaimPeriodMs = 1000;
    // INFO: fc 11nov26 unverified: a Bravo in range answers a connection in a few scan windows
    static constexpr uint32_t kConnectCeilingMs = 10000;
    // INFO: fc 14nov26 Bravo's 30 s press window, so the glass counts down the time Bravo allows
    static constexpr uint32_t kAnswerCeilingMs = 30000;
    // INFO: fc 11nov26 GAP fast scanning, 30 ms in 60 (Core 6.2 Vol 3 Part C App A), pilot waiting
    static constexpr uint32_t kPairingScanWindowUs = 30000;
    static constexpr uint32_t kPairingScanPeriodUs = 60000;
    // INFO: fc 11nov26 GAP slow scanning 1, 11.25 ms in 1.28 s, until #194 measures the cost
    static constexpr uint32_t kReconnectScanWindowUs = 11250;
    static constexpr uint32_t kReconnectScanPeriodUs = 1280000;
    // INFO: fc 12nov26 schemas/link.v1.json: Bravo sends health once a second whatever the traffic
    static constexpr uint32_t kHealthPeriodMs = 1000;
    // INFO: fc 12nov26 unverified: three missed health frames, so one lost notification is no loss
    static constexpr uint32_t kHealthMaxAgeMs = 3 * kHealthPeriodMs;
    // INFO: fc 12nov26 a paired Go names no loss before a connection has had its ceiling after boot
    static constexpr uint32_t kBootSettleMs = kConnectCeilingMs;
    // INFO: fc 12nov26 unverified: two GNSS clocks part by a second at most, and more is no clock
    static constexpr uint32_t kAheadCeilingMs = 1000;
    // INFO: fc 11nov26 two health periods for Bravo to answer an unpairing Go, then the link drops
    static constexpr uint32_t kFarewellCeilingMs = 2000;

    BravoService(runtime::Context& context, Settings& settings, comms::ConfigService& config,
                 ports::Central& central, bus::CentralQueue& central_events,
                 common::AlarmService& alarm)
        : runtime::Service(context),
          settings_(settings),
          config_(config),
          central_(central),
          central_events_(central_events),
          alarm_(alarm) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;

    void begin_pairing(uint32_t now_ms);
    void end_pairing();
    void pair(int found_index, uint32_t now_ms);
    void unpair();
    // INFO: fc 16nov26 a browser names the Bravo by its tag and hears the verdict once it is
    // settled
    void pair_with(uint16_t tag, uint32_t now_ms);
    std::optional<uint16_t> seeking() const { return wanted_; }
    // INFO: fc 16nov26 true once when a browser starts a pairing, so the glass shows the BRAVO page
    bool take_link_pairing_notice();

    BravoStep step() const { return step_; }
    uint32_t step_since_ms() const { return step_since_ms_; }
    BravoRefusal refusal() const { return refusal_; }
    BravoLink link() const { return link_; }
    int found_count() const { return found_count_; }
    const events::BluetoothAddress& found(int index) const { return found_[index]; }
    const events::BluetoothAddress& pairing_with() const { return pairing_with_; }
    AircraftAddressWarning& aircraft_address_warning() { return aircraft_address_warning_; }
    const AircraftAddressWarning& aircraft_address_warning() const {
        return aircraft_address_warning_;
    }
    // INFO: fc 12nov26 observations with no UTC of ours to place them by, or dated too far ahead
    uint32_t unplaced() const { return unplaced_; }
    // INFO: fc 12nov26 observation frames comms::read_observation() refused
    uint32_t refused() const { return refused_; }
    uint32_t folded() const { return folded_; }
    const BravoLog& log() const { return log_; }
    bool leaving() const { return leaving_; }
    // INFO: fc 13nov26 the transponder the last fresh health named, for AIRCRAFT to propose
    std::optional<comms::OwnTransponderHeard> own_transponder() const {
        return health_heard_ ? health_.own_transponder : std::nullopt;
    }
    // INFO: fc 15nov26 the last health from the pinned Bravo while fresh, nullptr once it ages out
    const comms::PeerHealth* health() const { return health_heard_ ? &health_ : nullptr; }
    // INFO: fc 11nov26 true once after Bravo said it holds no pin for this Go, for the glass
    bool take_forgotten_notice();
    // INFO: fc 15nov26 true once when the Go asks its Bravo again, for the glass
    bool take_asking_notice();
    bool asking_again() const { return asking_again_; }

   private:
    void take_event(const events::CentralEvent& event, uint32_t now_ms);
    void take_frame(const events::RxFrame& frame, uint32_t now_ms);
    void take_health(ConstByteSpan bytes, uint32_t now_ms);
    void fold(const comms::PeerObservation& peer, uint32_t now_ms);
    void log(BravoVerdict verdict, const comms::PeerObservation* peer, uint32_t now_ms);
    std::optional<uint32_t> second_edge_ms(uint32_t at_s) const;
    comms::BravoState state_now() const;
    void came_up(uint16_t payload_bytes, uint32_t now_ms);
    void went_down();
    void hear(const events::CentralEvent& event, uint32_t now_ms);
    void pick(const events::BluetoothAddress& address, uint32_t now_ms);
    void seek(uint32_t now_ms);
    void answer_link(link::Reason verdict);
    void start_scan();
    void refuse(BravoRefusal refusal);
    void refuse_protocol();
    void release_unless_paired();
    void connect(const events::BluetoothAddress& address, uint32_t window_us, uint32_t period_us);
    void claim(uint32_t now_ms);
    bool call_changed() const;
    std::optional<comms::OwnTransponderHeard> own_transponder_on_ground() const;
    void ask_again(uint32_t now_ms);
    void drop_pin();
    void forget();
    void leave();
    // INFO: fc 17nov26 Bravo was live, or the Go has taxied: the pilot counts on Bravo from then on
    bool counted_on() const { return utc_heard_ || rolled_; }
    bool linked() const {
        return up_ &&
               (step_ == BravoStep::Waiting || (step_ == BravoStep::Idle && paired(settings_)));
    }

    Settings& settings_;
    comms::ConfigService& config_;
    ports::Central& central_;
    bus::CentralQueue& central_events_;
    common::AlarmService& alarm_;
    AircraftAddressWarning aircraft_address_warning_{};
    common::AdsbCapture adsb_capture_{context_};
    comms::PeerHealth health_{};
    events::BluetoothAddress found_[kBravoFoundRows]{};
    events::BluetoothAddress pairing_with_{};
    int found_count_{0};
    BravoStep step_{BravoStep::Idle};
    BravoRefusal refusal_{BravoRefusal::None};
    BravoLink link_{BravoLink::Down};
    uint32_t step_since_ms_{0};
    uint32_t claimed_ms_{0};
    annunciation::Call claimed_call_{};
    Alarm claimed_alarm_{Alarm::Off};
    uint32_t boot_epoch_{0};
    uint32_t health_heard_ms_{0};
    uint32_t started_ms_{0};
    uint32_t unplaced_{0};
    uint32_t refused_{0};
    uint32_t folded_{0};
    BravoLog log_{};
    bool health_heard_{false};
    // INFO: fc 12nov26 the pinned Bravo sent UTC since the Go booted, so no UTC from now is lost
    bool utc_heard_{false};
    bool rolled_{false};
    bool started_{false};
    uint32_t farewell_ms_{0};
    bool connecting_{false};
    bool up_{false};
    bool leaving_{false};
    bool farewell_owed_{false};
    bool forgotten_{false};
    bool asking_again_{false};
    bool asking_noticed_{false};
    std::optional<uint16_t> wanted_{};
    uint32_t wanted_since_ms_{0};
    bool seek_due_{false};
    bool link_answer_owed_{false};
    bool link_pairing_noticed_{false};
};

}  // namespace skyblip::go

#endif
