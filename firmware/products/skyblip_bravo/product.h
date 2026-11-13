#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_PRODUCT_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_PRODUCT_H

#include <optional>

#include "boards/seeed/meshtracker_x1/board.h"
#include "core/indication/lamp.h"
#include "core/power/reset_reason.h"
#include "core/power/shutdown.h"
#include "core/power/wake.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "products/common/services/alarm.h"
#include "products/common/services/capture.h"
#include "products/common/services/config_link.h"
#include "products/common/services/flight_log.h"
#include "products/common/services/nmea.h"
#include "products/common/services/ownship.h"
#include "products/common/services/power.h"
#include "products/skyblip_bravo/capture_settings.h"
#include "products/skyblip_bravo/features.h"
#include "products/skyblip_bravo/flash_window.h"
#include "products/skyblip_bravo/gate_reference.h"
#include "products/skyblip_bravo/name.h"
#include "products/skyblip_bravo/services/adsb.h"
#include "products/skyblip_bravo/services/button.h"
#include "products/skyblip_bravo/services/peer.h"
#include "products/skyblip_bravo/settings.h"
#include "products/skyblip_bravo/settings_store.h"
#include "runtime/loop.h"

namespace skyblip::bravo {

constexpr ports::Capabilities kRequired = ports::Capability::AdsbReceiver | ports::Capability::Gnss;
constexpr ports::Capabilities kOptional =
    ports::Capability::Barometer | ports::Capability::Buzzer | ports::Capability::Haptic |
    ports::Capability::Link | ports::Capability::Storage | ports::Capability::Dfu |
    ports::Capability::Contacts | ports::Capability::Battery | ports::Capability::Indicator |
    ports::Capability::DieTemperature;

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
    }

    Status setup() {
        // INFO: fc 12nov26 decided before bring-up, so a refused cable wake starts no radio, link
        // or NVS
        platform_.begin_wake();
        const power::ResetCause causes = platform_.system_power().reset_causes();
        reset_reason_ = power::classify(causes);
        config_link_.config().set_reset_reason(reset_reason_);
        boot_cell_ = read_boot_cell();
        boot_path_ = power::boot_path(causes, platform_.button_down(), boot_cell_);
        if (boot_path_ == power::BootPath::SleepAgain) return Status::Ok;

        board_.probe();
        roles_.capabilities = board_.capabilities();
        const Status board = board_.begin();
        if (board == Status::Ok) config_link_.load();
        flyable_ = board == Status::Ok &&
                   ports::without(kRequired, board_.capabilities()) == ports::Capability::None;
        if (flyable_) config_link_.load_identity();
        if (board != Status::Ok) return board;
        if (!flyable_) return Status::Down;
        if (reset_reason_ == power::ResetReason::LowPowerWake) alarm_.sign_on();
        const Status loop = loop_.setup();
        state_.started = loop == Status::Ok;
        return loop;
    }

    void run_pass(uint32_t now_ms) {
        if (!flyable_ && !shutdown_.going_down()) {
            guard_cell(now_ms);
            show_refusal(now_ms);
        }
        if (flyable_ && !shutdown_.going_down()) {
            meter_.begin_pass(context_.instant(now_ms).into_ms);
            meter_.timed(diagnostics::kLoopBoardPoll, [&] { board_.poll(state_, now_ms); });
            if (platform_.system_power().take_supply_warning()) power_.on_supply_warning();
            loop_.run_pass(now_ms, &meter_);
            meter_.end_pass();
            record_loop(now_ms);
            if (power_.cutoff()) shutdown_.request(power::ShutdownReason::LowBattery, now_ms);
            take_link_requests(now_ms);
        }
        shutdown_.tick(now_ms, platform_.button_down());
        drive_shutdown(now_ms);
        if (shutdown_.going_down()) finish_sign_off(now_ms);
        if (shutdown_.going_down()) await_receiver_park(now_ms, ready_but_for_receiver());
        enter_recovery_once_parked();
    }

    // INFO: fc 12nov26 the refused press may still be down, and the wake pin senses a level
    bool park_refusal(uint32_t now_ms) {
        const bool released = refusal_release_.settled(now_ms, platform_.button_down());
        // INFO: fc 12nov26 after a reset the LR2021's sleep is unknown, so a refusal holds it by
        // GPIO
        if (!refusal_asked_) {
            refusal_asked_ = true;
            if (board_.probed())
                park_parts(now_ms);
            else
                board_.hold_radio_in_reset();
        }
        if (!board_.probed()) return released;
        await_receiver_park(now_ms, released);
        return released && board_.receiver().parked();
    }

    ports::Capabilities capabilities() const { return board_.capabilities(); }
    ports::Capabilities degraded() const {
        return ports::without(kOptional, board_.capabilities());
    }

    bool flyable() const { return flyable_; }
    indication::Condition refusal_lamp() const { return refusal_lamp_.condition(); }
    power::ResetReason reset_reason() const { return reset_reason_; }
    power::BootPath boot_path() const { return boot_path_; }
    const power::BootCell& boot_cell() const { return boot_cell_; }

    power::ShutdownSequencer& shutdown() { return shutdown_; }
    const power::ShutdownSequencer& shutdown() const { return shutdown_; }
    bool ready_to_power_off() const {
        return ready_but_for_receiver() && board_.receiver().parked();
    }
    bool installing() const { return shutdown_.reason() == power::ShutdownReason::Install; }
    bool recovering() const { return shutdown_.reason() == power::ShutdownReason::Recovery; }

    // INFO: fc 12nov26 a deliberate shutdown is fed through, so a held button never turns into a
    // reset
    bool may_feed_watchdog(uint32_t now_ms) const {
        if (!flyable_ || shutdown_.going_down()) return true;
        return loop_.may_feed_watchdog(now_ms);
    }
    int stalled_service(uint32_t now_ms) const { return loop_.stalled_service(now_ms); }
    const char* service_name(int index) const { return loop_.feed_decision().name(index); }

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
    power::BootCell read_boot_cell() {
        power::BootCell cell{};
        if (!ports::has(board_.capabilities(), ports::Capability::Battery)) return cell;
        uint16_t raw_mv = 0;
        cell.valid = platform_.read_battery_mv(raw_mv);
        cell.cell_mv = raw_mv;
        cell.external_power = platform_.external_power();
        return cell;
    }

    void guard_cell(uint32_t now_ms) {
        board_.poll_battery(now_ms);
        power_.tick(now_ms);
        if (power_.cutoff()) shutdown_.request(power::ShutdownReason::LowBattery, now_ms);
    }

    // INFO: fc 12nov26 no service runs to light the lamp, so the refusal blinks the critical row
    // here
    void show_refusal(uint32_t now_ms) {
        indication::Situation refused{};
        refused.power_level = power::PowerLevel::Critical;
        const indication::Command command = refusal_lamp_.update(refused, now_ms);
        if (command.changed) roles_.indicator.show(command.lamp);
    }

    void take_link_requests(uint32_t now_ms) {
        comms::ConfigService& config = config_link_.config();
        if (config.power_off_requested()) {
            config.clear_power_off_request();
            shutdown_.request(power::ShutdownReason::LinkRequest, now_ms);
        }
        if (config.install_requested()) {
            config.clear_install_request();
            shutdown_.request(power::ShutdownReason::Install, now_ms);
        }
        if (config.recovery_requested()) {
            config.clear_recovery_request();
            shutdown_.request(power::ShutdownReason::Recovery, now_ms);
        }
        if (const std::optional<comms::CaptureProfile> profile = config.capture_requested()) {
            config.clear_capture_request();
            order_capture(*profile);
        }
    }

    void order_capture(comms::CaptureProfile profile) {
        switch (profile) {
            case comms::CaptureProfile::Full: diagnostics_.arm(diagnostics::Profile::Full); return;
            case comms::CaptureProfile::PowerRun:
                diagnostics_.arm(diagnostics::Profile::PowerRun);
                return;
            case comms::CaptureProfile::FlightRun:
                diagnostics_.arm(diagnostics::Profile::FlightRun);
                return;
            case comms::CaptureProfile::Stop: diagnostics_.disarm(); return;
        }
    }

    void drive_shutdown(uint32_t now_ms) {
        const power::ShutdownPhase phase = shutdown_.phase();
        if (phase == acted_phase_) return;
        acted_phase_ = phase;
        if (phase == power::ShutdownPhase::Off && installing()) {
            config_link_.record_update();
            roles_.dfu.trigger();
            return;
        }
        if (phase != power::ShutdownPhase::Parking) return;
        board_.receiver().sleep();
        if (power::ends_switched_off(shutdown_.reason(), recovery_powers_off()))
            alarm_.sign_off(now_ms);
        else
            park_outputs(now_ms);
        config_link_.flush_settings(now_ms);
        power_.record_last_pass(now_ms);
        capture_.park(now_ms);
    }

    // INFO: fc 13nov26 a refused boot parks these at once, and a switch-off parks the outputs once
    // its sign ends
    void park_parts(uint32_t now_ms) {
        board_.receiver().sleep();
        park_outputs(now_ms);
    }

    void park_outputs(uint32_t now_ms) {
        alarm_.park(now_ms);
        board_.park();
    }

    void finish_sign_off(uint32_t now_ms) {
        if (!alarm_.signing()) return;
        alarm_.advance_signature(now_ms);
        if (!alarm_.signing()) park_outputs(now_ms);
    }

    bool recovery_powers_off() const {
        return roles_.dfu.recovery_path() == ports::RecoveryPath::PowerOffToFinish;
    }

    bool ready_but_for_receiver() const {
        return shutdown_.ready_to_power_off() && !installing() && !alarm_.signing() &&
               (!recovering() || recovery_taken_ == ports::RecoveryPath::PowerOffToFinish);
    }

    // INFO: fc 12nov26 a receiver that never parks must not keep a switched-off unit awake
    void await_receiver_park(uint32_t now_ms, bool waiting) {
        if (!waiting || board_.receiver().parked()) {
            receiver_waited_since_ms_.reset();
            return;
        }
        if (!receiver_waited_since_ms_) receiver_waited_since_ms_ = now_ms;
        if (now_ms - *receiver_waited_since_ms_ >= kReceiverParkCeilingMs)
            board_.receiver().abandon();
    }

    void enter_recovery_once_parked() {
        if (!recovering() || recovery_taken_ || !shutdown_.ready_to_power_off()) return;
        recovery_taken_ = roles_.dfu.enter_recovery();
    }

    void record_loop(uint32_t now_ms) {
        const diagnostics::Instant at = context_.instant(now_ms);
        if (at.at_s == loop_window_s_) return;
        loop_window_s_ = at.at_s;
        const diagnostics::Loop window = meter_.take();
        if (diagnostics_.armed()) diagnostics_.record(window, at);
    }

    bus::Bus bus_{};
    bus::AdsbQueue adsb_frames_{};
    bus::State state_{};
    diagnostics::Recorder diagnostics_{};
    Settings settings_{};
    Platform& platform_;
    Board board_;
    ports::Roles roles_{board_.roles()};
    runtime::Context context_{roles_, bus_, state_, diagnostics_};
    runtime::LoopMeter meter_{roles_.clock};
    uint32_t loop_window_s_{0};
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
        kProductName, bravo::bluetooth_name, dfu::Product::SkyblipBravo};
    common::OwnshipService ownship_{context_, aircraft_category_};
    GateReference gate_{};
    AdsbService adsb_{context_, adsb_frames_, settings_.own_icao, gate_};
    common::AlarmService alarm_{
        context_, kFeatures, {settings_.alarm_enabled, settings_.alarm_volume}};
    PeerService peer_{context_, settings_, config_link_.config(), board_.receiver(), gate_};
    ButtonService button_{context_, config_link_.config(), alarm_, peer_};
    common::NmeaService nmea_{context_, kFeatures, config_link_.config()};
    common::FlightLogService flight_log_{context_, flights_store_, &capture_store_,
                                         config_link_.config()};
    common::CaptureService capture_{context_, capture_store_, flights_store_, capture_settings_,
                                    config_link_.config()};

    // INFO: fc 12nov26 ownship fixes before adsb folds, and the lamp's prompt before the button
    // answers
    // INFO: fc 11nov26 peer goes last, so the loop codes of the nine services before it hold
    static constexpr int kServiceCount = 10;
    runtime::Service* services_[kServiceCount]{&config_link_, &ownship_, &power_, &adsb_,
                                               &alarm_,       &button_,  &nmea_,  &flight_log_,
                                               &capture_,     &peer_};
    static constexpr const char* kServiceNames[kServiceCount] = {
        "config", "ownship", "power",      "adsb",    "alarm",
        "button", "nmea",    "flight_log", "capture", "peer"};
    runtime::Loop loop_{services_, kServiceCount, kServiceNames};

    power::ShutdownSequencer shutdown_{};
    power::ShutdownPhase acted_phase_{power::ShutdownPhase::Running};
    power::ResetReason reset_reason_{power::ResetReason::Unknown};
    power::BootPath boot_path_{power::BootPath::Run};
    power::BootCell boot_cell_{};
    std::optional<ports::RecoveryPath> recovery_taken_{};
    std::optional<uint32_t> receiver_waited_since_ms_{};
    power::ButtonRelease refusal_release_{};
    indication::Policy refusal_lamp_{};
    bool refusal_asked_{false};
    bool flyable_{false};
};

}  // namespace skyblip::bravo

#endif
