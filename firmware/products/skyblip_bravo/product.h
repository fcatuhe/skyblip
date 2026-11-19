#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_PRODUCT_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_PRODUCT_H

#include "boards/seeed/meshtracker_x1/board.h"
#include "core/indication/lamp.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "products/common/services/alarm.h"
#include "products/common/services/capture.h"
#include "products/common/services/config_link.h"
#include "products/common/services/flight_log.h"
#include "products/common/services/nmea.h"
#include "products/common/services/ownship.h"
#include "products/common/services/power.h"
#include "products/common/shell.h"
#include "products/skyblip_bravo/capture_settings.h"
#include "products/skyblip_bravo/flash_window.h"
#include "products/skyblip_bravo/gate_reference.h"
#include "products/skyblip_bravo/name.h"
#include "products/skyblip_bravo/radio_protocols.h"
#include "products/skyblip_bravo/services/adsb.h"
#include "products/skyblip_bravo/services/button.h"
#include "products/skyblip_bravo/services/peer.h"
#include "products/skyblip_bravo/settings.h"
#include "products/skyblip_bravo/settings_store.h"
#include "runtime/loop.h"

namespace skyblip::bravo {

constexpr ports::Capabilities kRequired = ports::Capability::AdsbReceiver | ports::Capability::Gnss;

// INFO: fc 12nov26 ADS-L G.1.3 code 0, no emitter category: Bravo carries no aircraft type setting
constexpr uint8_t kAircraftCategoryUnknown = 0;

// INFO: fc 12nov26 the LR2021's worst sleep: a stuck standby, the reset after it, then the settle
constexpr uint32_t kReceiverParkCeilingMs =
    (parts::lr2021::kCommandBusyCeilingUs + parts::lr2021::kBootBusyCeilingUs +
     parts::lr2021::kSleepSettleUs) /
    1000;

template <class Platform>
class Product {
   public:
    using Board = boards::MeshtrackerX1<Platform>;

    explicit Product(Platform& platform)
        : platform_(platform), board_(platform, bus_, adsb_frames_) {
        config_link_.config().serve_captures();
        config_link_.config().set_radio_protocols(kRadioProtocols);
    }

    Status setup() {
        // INFO: fc 12nov26 decided before bring-up, so a refused cable wake starts no radio, link
        // or NVS
        platform_.begin_wake();
        const power::ResetCause causes = shell_.read_reset();
        if (shell_.read_boot(causes, battery_offset_mv_) == power::BootPath::SleepAgain)
            return Status::Ok;

        board_.probe();
        roles_.capabilities = board_.capabilities();
        const Status board = board_.begin();
        if (board == Status::Ok) config_link_.load();
        if (shell_.judge(board, kRequired)) config_link_.load_identity();
        return shell_.start(board);
    }

    void run_pass(uint32_t now_ms) {
        if (shell_.refusing()) show_refusal(now_ms);
        shell_.serve(now_ms);
        shell_.wind_down(now_ms, false);
    }

    // INFO: fc 12nov26 the refused press may still be down, and the wake pin senses a level
    bool park_refusal(uint32_t now_ms) {
        const bool released = refusal_release_.settled(now_ms, platform_.button_down());
        // INFO: fc 12nov26 after a reset the LR2021's sleep is unknown, so a refusal holds it by
        // GPIO
        if (!refusal_asked_) {
            refusal_asked_ = true;
            if (board_.probed())
                shell_.park_parts(now_ms);
            else
                board_.hold_radio_in_reset();
        }
        if (!board_.probed()) return released;
        shell_.await_radio_park(now_ms, released);
        return released && board_.receiver().parked();
    }

    ports::Capabilities capabilities() const { return board_.capabilities(); }
    bool flyable() const { return shell_.flyable(); }
    indication::Condition refusal_lamp() const { return refusal_lamp_.condition(); }
    power::ResetReason reset_reason() const { return shell_.reset_reason(); }
    power::BootPath boot_path() const { return shell_.boot_path(); }
    const power::BootCell& boot_cell() const { return shell_.boot_cell(); }
    power::ShutdownSequencer& shutdown() { return shell_.shutdown(); }
    const power::ShutdownSequencer& shutdown() const { return shell_.shutdown(); }
    bool ready_to_power_off() const { return shell_.ready_to_power_off(); }
    bool may_feed_watchdog(uint32_t now_ms) const { return shell_.may_feed_watchdog(now_ms); }

    Board& board() { return board_; }
    bus::Bus& bus() { return bus_; }
    bus::AdsbQueue& adsb_frames() { return adsb_frames_; }
    bus::State& state() { return state_; }
    const bus::State& state() const { return state_; }
    Settings& settings() { return settings_; }
    const Settings& settings() const { return settings_; }

    common::OwnshipService& ownship() { return ownship_; }
    common::PowerService& power() { return power_; }
    AdsbService& adsb() { return adsb_; }
    common::AlarmService& alarm() { return alarm_; }
    ButtonService& button() { return button_; }
    PeerService& peer() { return peer_; }
    const GateReference& gate() const { return gate_; }
    common::NmeaService& nmea() { return nmea_; }
    common::ConfigLinkService& config_link() { return config_link_; }
    common::FlightLogService& flight_log() { return flight_log_; }
    common::CaptureService& capture() { return capture_; }
    diagnostics::Recorder& diagnostics() { return diagnostics_; }

   private:
    friend class common::Shell<Product>;

    // INFO: fc 12nov26 no service runs to light the lamp, so the refusal blinks the critical row
    // here
    void show_refusal(uint32_t now_ms) {
        indication::Situation refused{};
        refused.power_level = power::PowerLevel::Critical;
        const indication::Command command = refusal_lamp_.update(refused, now_ms, state_.clock);
        if (command.changed) roles_.indicator.show(command.lamp);
    }

    void park_radio() { board_.receiver().sleep(); }
    typename Platform::AdsbReceiver& radio_part() { return board_.receiver(); }
    void settle_glass(uint32_t) {}
    bool glass_parking() const { return false; }
    void park_glass() {}

    bus::Bus bus_{};
    bus::AdsbQueue adsb_frames_{};
    bus::State state_{};
    diagnostics::Recorder diagnostics_{};
    Settings settings_{};
    Platform& platform_;
    Board board_;
    ports::Roles roles_{board_.roles()};
    runtime::Context context_{roles_, bus_, state_, diagnostics_};
    common::RecordPool pool_{context_, book_flash_beside_adsb};
    common::RecordStore flights_store_{pool_, store::SectorOwner::Flights};
    common::RecordStore capture_store_{pool_, store::SectorOwner::Diagnostics};
    CaptureSettings capture_settings_{settings_};
    // INFO: fc 12nov26 Bravo stores neither a battery trim nor an aircraft type, the Go's settings
    const int16_t battery_offset_mv_{0};
    const uint8_t aircraft_category_{kAircraftCategoryUnknown};

    // TODO: fc 11nov26 the Go's LiPo curve and ladder, until #169 measures the X1's cell to cutoff
    common::PowerService power_{context_, battery_offset_mv_};
    SettingsStore settings_store_{settings_};
    common::ConfigLinkService config_link_{
        context_,     settings_store_,       power_.cutoff_monitor(),
        kProductName, bravo::bluetooth_name, dfu::Product::Bravo};
    common::OwnshipService ownship_{context_, aircraft_category_};
    GateReference gate_{};
    AdsbService adsb_{context_, adsb_frames_, settings_.aircraft_address, gate_};
    // INFO: fc 19nov26 Bravo has no glass, so its lamp also shows a prompt, a pairing and a phone
    common::AlarmService alarm_{
        context_, common::AlarmSettings{settings_.alarm, settings_.go_alarm, settings_.peer.pinned},
        true};
    PeerService peer_{context_,
                      settings_,
                      config_link_.config(),
                      board_.receiver(),
                      gate_,
                      adsb_.own_transponder(),
                      alarm_};
    ButtonService button_{context_, config_link_.config(), alarm_, peer_};
    common::NmeaService nmea_{context_, config_link_.config()};
    common::FlightLogService flight_log_{context_, flights_store_, &capture_store_,
                                         config_link_.config()};
    common::CaptureService capture_{context_, capture_store_, flights_store_, capture_settings_,
                                    config_link_.config()};

    // INFO: fc 12nov26 ownship fixes before adsb folds, and the lamp's prompt before the button
    // answers
    static constexpr int kServiceCount = 10;
    const runtime::Loop::Entry services_[kServiceCount]{
        {&config_link_, diagnostics::LoopService::Config},
        {&ownship_, diagnostics::LoopService::Ownship},
        {&power_, diagnostics::LoopService::Power},
        {&adsb_, diagnostics::LoopService::Adsb},
        {&alarm_, diagnostics::LoopService::Alarm},
        {&button_, diagnostics::LoopService::Button},
        {&nmea_, diagnostics::LoopService::Nmea},
        {&flight_log_, diagnostics::LoopService::FlightLog},
        {&capture_, diagnostics::LoopService::Capture},
        {&peer_, diagnostics::LoopService::Peer}};
    runtime::Loop loop_{services_, kServiceCount};
    common::Shell<Product> shell_{*this, kReceiverParkCeilingMs};

    power::ButtonRelease refusal_release_{};
    indication::Policy refusal_lamp_{};
    bool refusal_asked_{false};
};

}  // namespace skyblip::bravo

#endif
