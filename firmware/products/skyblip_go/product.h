#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_H

#include "boards/lilygo/t_echo_plus/board.h"
#include "core/util/format.h"
#include "products/common/services/alarm.h"
#include "products/common/services/capture.h"
#include "products/common/services/config_link.h"
#include "products/common/services/flight_log.h"
#include "products/common/services/nmea.h"
#include "products/common/services/ownship.h"
#include "products/common/services/power.h"
#include "products/common/shell.h"
#include "products/common/solution_rate.h"
#include "products/skyblip_go/capture_settings.h"
#include "products/skyblip_go/flash_window.h"
#include "products/skyblip_go/product_name.h"
#include "products/skyblip_go/radio_protocols.h"
#include "products/skyblip_go/self_test_parts.h"
#include "products/skyblip_go/services/bravo.h"
#include "products/skyblip_go/services/radio.h"
#include "products/skyblip_go/services/screen.h"
#include "products/skyblip_go/services/traffic.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"
#include "runtime/loop.h"

namespace skyblip::go {

constexpr ports::Capabilities kRequired =
    ports::Capability::Rf | ports::Capability::Gnss | ports::Capability::Barometer;

// INFO: fc 21sep26 a full frame is 2.6 s of BUSY on this panel, and a refusal waits out two
constexpr uint32_t kRefusalParkCeilingMs = 6000;

// INFO: fc 19nov26 the request wakes every executor wait, so only a radio that hangs takes 2 s
constexpr uint32_t kRadioParkCeilingMs = 2000;

// INFO: fc 19nov26 the Go transmits ADS-L, so it solves on .000 and .500, two of G.1.1's quarters
constexpr ports::SolutionRate kSolutionRate = ports::SolutionRate::Half;

template <class Platform>
class Product {
   public:
    using Board = boards::TEchoPlus<Platform>;
    static_assert(common::solution_rate_serves<typename Board::Gnss>(kRadioProtocols,
                                                                     kSolutionRate),
                  "an ADS-L transmitter needs a receiver solving twice a second");

    explicit Product(Platform& platform)
        : platform_(platform), board_(platform, bus_, kSolutionRate) {
        config_link_.config().serve_captures();
        config_link_.config().set_radio_protocols(kRadioProtocols);
    }

    Status setup() {
        board_.probe();
        const Status board = board_.begin();
        roles_.capabilities = board_.capabilities();
        config_link_.set_product_name(product_name(roles_.capabilities));
        config_link_.config().set_variant(variant_of(roles_.capabilities));
        const power::ResetCause causes = shell_.read_reset();
        if (board == Status::Ok) config_link_.load();

        // INFO: fc 20nov26 answered before the panel is painted, so a charger wake leaves no trace
        const power::BootPath path = shell_.read_boot(causes, settings_.battery_offset_mv);
        glass_remembered_ = platform_.system_power().cell_on_glass();
        take_went_dark_flat();
        config_link_.config().set_went_dark_flat(went_dark_flat_);
        if (path == power::BootPath::SleepAgain) {
            refused_frame_ = power::refused_frame(shell_.boot_cell(), glass_remembered_);
            return Status::Ok;
        }
        // INFO: fc 18oct26 a refused unit serves nothing, so it advertises nothing either
        if (shell_.judge(board, kRequired)) config_link_.load_identity();

        if (!flyable()) (void)board_.take_bus_inventory();
        take_self_test();
        if (!flyable()) screen_.present_self_test();
        return shell_.start(board);
    }

    void run_pass(uint32_t now_ms) {
        if (shell_.serve(now_ms)) scan_for_self_test();
        shell_.wind_down(now_ms, platform_.pad_down());
        remember_glass();
    }

    bool park_refusal(uint32_t now_ms) {
        // INFO: fc 05oct26 the refused press may still be down, and the wake pin senses a level
        const bool released = refusal_release_.settled(now_ms, platform_.button_down());
        if (!refusal_asked_) {
            refusal_asked_ = true;
            refusal_since_ms_ = now_ms;
            shell_.park_parts(now_ms);
            if (refused_frame_ == power::RefusedFrame::Leave)
                screen_.sleep_panel(now_ms);
            else if (refused_frame_ == power::RefusedFrame::FlatCell)
                park_flat_cell();
            else
                screen_.park_for_off();
        }
        if (refused_frame_ != power::RefusedFrame::Leave) {
            screen_.settle_park(now_ms);
            remember_glass();
            // INFO: fc 21sep26 a panel that never reports ready must not hold a flat cell awake
            if (screen_.parking() && now_ms - refusal_since_ms_ >= kRefusalParkCeilingMs)
                screen_.sleep_panel(now_ms);
        }
        const bool ready_but_radio = released && !screen_.parking();
        shell_.await_radio_park(now_ms, ready_but_radio);
        return ready_but_radio && roles_.rf.parked();
    }

    power::RefusedFrame refused_frame() const { return refused_frame_; }

    ports::Capabilities capabilities() const { return board_.capabilities(); }
    bool flyable() const { return shell_.flyable(); }
    power::ResetReason reset_reason() const { return shell_.reset_reason(); }
    bool went_dark_flat() const { return went_dark_flat_; }
    power::BootPath boot_path() const { return shell_.boot_path(); }
    const power::BootCell& boot_cell() const { return shell_.boot_cell(); }
    const SelfTestSnapshot& self_test() const { return self_test_snapshot_; }
    power::ShutdownSequencer& shutdown() { return shell_.shutdown(); }
    const power::ShutdownSequencer& shutdown() const { return shell_.shutdown(); }
    bool ready_to_power_off() const { return shell_.ready_to_power_off(); }
    bool may_feed_watchdog(uint32_t now_ms) const { return shell_.may_feed_watchdog(now_ms); }
    uint32_t due_in_ms(uint32_t now_ms) const { return shell_.due_in_ms(now_ms); }

    Board& board() { return board_; }
    bus::GoBus& bus() { return bus_; }
    bus::State& state() { return state_; }
    Settings& settings() { return settings_; }
    const Settings& settings() const { return settings_; }
    const bus::State& state() const { return state_; }

    common::OwnshipService& ownship() { return ownship_; }
    common::PowerService& power() { return power_; }
    RadioService& radio() { return radio_; }
    TrafficService& traffic() { return traffic_; }
    common::AlarmService& alarm() { return alarm_; }
    common::NmeaService& nmea() { return nmea_; }
    ScreenService& screen() { return screen_; }
    common::ConfigLinkService& config_link() { return config_link_; }
    common::FlightLogService& flight_log() { return flight_log_; }
    common::CaptureService& capture() { return capture_; }
    BravoService& bravo() { return bravo_; }
    diagnostics::Recorder& diagnostics() { return diagnostics_; }

   private:
    friend class common::Shell<Product>;

    // INFO: fc 20nov26 read the bring-up probes, never what the image was built expecting
    const char* self_test_detail(const SelfTestPartSpec& spec) {
        const ports::Inventory& found = board_.inventory();
        switch (spec.capability) {
            case ports::Capability::Barometer:
                return found.barometer_address == 0 ? spec.wired_part
                                                    : barometer_part(found.barometer_address);
            case ports::Capability::Display:
                return ports::has(board_.capabilities(), ports::Capability::Display)
                           ? found.panel_name
                           : spec.wired_part;
            case ports::Capability::Haptic:
                return found.haptic == ports::HapticKind::PinMotor ? "PIN" : spec.wired_part;
            case ports::Capability::Inclinometer:
                return board_.imu_without_image() ? kImuDetailWithoutImage : spec.wired_part;
            case ports::Capability::Storage:
                return config_link_.settings_fallback() == settings::Fallback::Defaults
                           ? kStorageOnDefaults
                           : spec.wired_part;
            default: return spec.wired_part;
        }
    }

    const char* barometer_part(uint8_t address) {
        int written = skyblip::format_string(barometer_part_, "BME280 ");
        written += skyblip::format_hex(barometer_part_ + written, address, 2);
        barometer_part_[written] = 0;
        return barometer_part_;
    }

    void take_went_dark_flat() {
        auto& retained = platform_.system_power();
        went_dark_flat_ =
            glass_remembered_ == power::CellOnGlass::Flat || retained.went_dark_flat();
        retained.set_went_dark_flat(boot_path() == power::BootPath::SleepAgain && went_dark_flat_);
    }

    void remember_glass() {
        const power::CellOnGlass on_glass = screen_.cell_on_glass();
        if (on_glass == glass_remembered_) return;
        glass_remembered_ = on_glass;
        platform_.system_power().set_cell_on_glass(on_glass);
    }

    void park_flat_cell() {
        platform_.system_power().set_went_dark_flat(true);
        screen_.park_for_flat_cell();
    }

    // INFO: fc 14nov26 a menu is a tap away from the self-test page, so the bus is scanned before
    // it draws
    void scan_for_self_test() {
        const bool on_page = screen_.mode() == Mode::Page && screen_.page() == Page::SelfTest;
        if (screen_.mode() != Mode::Menu && !on_page) return;
        if (!board_.take_bus_inventory()) return;
        take_self_test();
        if (on_page) screen_.mark_dirty();
    }

    void take_self_test() {
        const ports::Capabilities fitted = board_.capabilities();
        for (int i = 0; i < kSelfTestPartCount; i++) {
            const SelfTestPartSpec& spec = kSelfTestParts[i];
            self_test_parts_[i].name = spec.name;
            self_test_parts_[i].state = ports::has(fitted, spec.capability) ? PartState::Pass
                                        : ports::has(kRequired, spec.capability)
                                            ? PartState::Fail
                                            : PartState::Absent;
            self_test_parts_[i].detail = self_test_detail(spec);
        }

        self_test_snapshot_.address = state_.activation.address();
        self_test_snapshot_.reset_reason = power::to_string(reset_reason());
        self_test_snapshot_.went_dark_flat = went_dark_flat_;
        self_test_snapshot_.parts = self_test_parts_;
        self_test_snapshot_.part_count = kSelfTestPartCount;
        self_test_snapshot_.flyable = flyable();
        self_test_snapshot_.battery_valid = boot_cell().valid;
        self_test_snapshot_.battery_mv = boot_cell().cell_mv;
        const ports::Inventory& found = board_.inventory();
        for (uint8_t i = 0; i < found.i2c_count; i++)
            i2c_roles_[i] = boards::t_echo_plus::i2c_role(found.i2c_addresses[i]);
        self_test_snapshot_.i2c_addresses = found.i2c_addresses;
        self_test_snapshot_.i2c_roles = i2c_roles_;
        self_test_snapshot_.i2c_address_count = found.i2c_count;
    }

    void park_glass() {
        switch (shutdown().reason()) {
            case power::ShutdownReason::Install: screen_.park_for_install(); return;
            case power::ShutdownReason::Recovery:
                screen_.park_for_recovery(roles_.dfu.recovery_path());
                return;
            case power::ShutdownReason::Stow: screen_.park_for_stow(); return;
            case power::ShutdownReason::LowBattery: park_flat_cell(); return;
            default: break;
        }
        if (power::needs_charge(state_.power.level))
            screen_.park_for_low_cell();
        else
            screen_.set_power(false);
    }

    void settle_glass(uint32_t now_ms) { screen_.settle_park(now_ms); }
    bool glass_parking() const { return screen_.parking(); }

    // INFO: fc 19nov26 the radio parks first, since an armed dwell keeps the PA up through a park
    void park_radio() {
        roles_.rf.abort();
        roles_.rf.sleep();
        publish_radio_asleep();
    }

    ports::Rf& radio_part() const { return roles_.rf; }

    void publish_radio_asleep() {
        state_.rf.plan = timing::SlotPlan{};
        state_.rf.dwell = timing::DwellPhase{};
    }

    bus::GoBus bus_{};
    bus::State state_{};
    diagnostics::Recorder diagnostics_{};
    Settings settings_{};
    SelfTestSnapshot self_test_snapshot_{};
    Platform& platform_;
    Board board_;
    ports::Roles roles_{board_.roles()};
    runtime::Context context_{roles_, bus_, state_, diagnostics_};
    common::RecordPool pool_{context_, book_flash_in_dwell};
    common::RecordStore flights_store_{pool_, store::SectorOwner::Flights};
    common::RecordStore capture_store_{pool_, store::SectorOwner::Diagnostics};
    CaptureSettings capture_settings_{settings_};

    // INFO: fc 20nov26 declared before the config service, which asks it whether a write is safe
    common::PowerService power_{context_, settings_.battery_offset_mv};
    SettingsStore settings_store_{settings_, state_.activation, state_.own_transponder,
                                  roles_.capabilities};
    common::ConfigLinkService config_link_{
        context_,     settings_store_,          power_.cutoff_monitor(),
        "skyBlip Go", identity::bluetooth_name, dfu::Product::Go};
    common::OwnshipService ownship_{context_, settings_.aircraft_type, &bus_.accelerometer};
    RadioService radio_{context_, settings_};
    TrafficService traffic_{context_, bus_.rf, settings_.aircraft_address};
    common::AlarmService alarm_{context_, common::AlarmSettings{settings_.alarm}};
    common::NmeaService nmea_{context_, config_link_.config()};
    common::FlightLogService flight_log_{context_, flights_store_, &capture_store_,
                                         config_link_.config()};
    common::CaptureService capture_{context_, capture_store_, flights_store_, capture_settings_,
                                    config_link_.config()};
    BravoService bravo_{context_,         settings_,           config_link_.config(),
                        board_.central(), bus_.central_events, alarm_};
    ScreenService screen_{context_, settings_,           config_link_.config(),
                          alarm_,   self_test_snapshot_, bravo_};

    // INFO: fc 20nov26 the order is the dataflow (README.md, What a service is), never a preference
    static constexpr int kServiceCount = 11;
    const runtime::Loop::Entry services_[kServiceCount]{
        {&config_link_, diagnostics::LoopService::Config},
        {&ownship_, diagnostics::LoopService::Ownship},
        {&power_, diagnostics::LoopService::Power},
        {&radio_, diagnostics::LoopService::Radio},
        {&traffic_, diagnostics::LoopService::Traffic},
        {&alarm_, diagnostics::LoopService::Alarm},
        {&nmea_, diagnostics::LoopService::Nmea},
        {&flight_log_, diagnostics::LoopService::FlightLog},
        {&capture_, diagnostics::LoopService::Capture},
        {&screen_, diagnostics::LoopService::Screen},
        {&bravo_, diagnostics::LoopService::Bravo}};
    runtime::Loop loop_{services_};
    common::Shell<Product> shell_{*this, kRadioParkCeilingMs};

    SelfTestPart self_test_parts_[kSelfTestPartCount]{};
    // INFO: fc 20nov26 a member, since SelfTestPart keeps the pointer after self_test_detail()
    char barometer_part_[10]{};
    const char* i2c_roles_[ports::Inventory::kMaxI2cAddresses]{};
    power::RefusedFrame refused_frame_{power::RefusedFrame::Leave};
    uint32_t refusal_since_ms_{0};
    power::ButtonRelease refusal_release_{};
    bool refusal_asked_{false};
    power::CellOnGlass glass_remembered_{power::CellOnGlass::None};
    bool went_dark_flat_{false};
};

}  // namespace skyblip::go

#endif
