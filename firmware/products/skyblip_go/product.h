#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_H

#include "boards/lilygo/t_echo_plus/board.h"
#include "core/power/reset_reason.h"
#include "core/power/shutdown.h"
#include "core/power/wake.h"
#include "core/util/format.h"
#include "products/skyblip_go/features.h"
#include "products/skyblip_go/pages/boot.h"
#include "products/skyblip_go/services/alarm.h"
#include "products/skyblip_go/services/capture.h"
#include "products/skyblip_go/services/config.h"
#include "products/skyblip_go/services/flight_log.h"
#include "products/skyblip_go/services/nmea.h"
#include "products/skyblip_go/services/ownship.h"
#include "products/skyblip_go/services/power.h"
#include "products/skyblip_go/services/radio.h"
#include "products/skyblip_go/services/screen.h"
#include "products/skyblip_go/services/traffic.h"
#include "products/skyblip_go/settings.h"
#include "runtime/loop.h"

namespace skyblip::go {

// What this product cannot fly without, and what it can lose and keep flying.
constexpr ports::Capabilities kRequired = ports::Capability::Rf | ports::Capability::Gnss;
constexpr ports::Capabilities kOptional =
    ports::Capability::Display | ports::Capability::Baro | ports::Capability::Buzzer |
    ports::Capability::Haptic | ports::Capability::Link | ports::Capability::Storage |
    ports::Capability::Dfu | ports::Capability::Contacts | ports::Capability::Battery |
    ports::Capability::Indicator | ports::Capability::Inclinometer;

// INFO: fc 21sep26 a full frame is 2.6 s of BUSY on this panel, and a refusal waits out two
constexpr uint32_t kRefusalParkCeilingMs = 6000;

struct BootPartSpec {
    const char* name;
    ports::Capability capability;
    const char* wired_part;
};

constexpr BootPartSpec kBootParts[] = {
    {"RADIO", ports::Capability::Rf, "SX1262"},
    {"GNSS", ports::Capability::Gnss, "L76K"},
    {"PANEL", ports::Capability::Display, "SSD1681"},
    {"BARO", ports::Capability::Baro, "BME280"},
    {"IMU", ports::Capability::Inclinometer, "BHI260AP"},
    {"TEMP", ports::Capability::DieTemperature, "NRF52840"},
    {"BATTERY", ports::Capability::Battery, "DIVIDER"},
    {"CONTACTS", ports::Capability::Contacts, "P1.10 P0.11"},
    {"BUZZER", ports::Capability::Buzzer, "PIEZO"},
    {"HAPTIC", ports::Capability::Haptic, "DRV2605"},
    {"LAMP", ports::Capability::Indicator, "RGB"},
    {"LINK", ports::Capability::Link, "BLE"},
    {"STORAGE", ports::Capability::Storage, "NVS+NOR"},
    {"DFU", ports::Capability::Dfu, "MCUBOOT"},
};

constexpr int kBootPartCount = static_cast<int>(sizeof(kBootParts) / sizeof(kBootParts[0]));
static_assert(kBootPartCount < kBootRows, "the self-test page would drop the bus scan");

// skyBlip Go: one board, one service list. The shell around it only decides how
// often step() is called and where the pixels go.
template <class P>
class Product {
   public:
    using Board = boards::TEchoPlus<P>;

    explicit Product(P& platform) : platform_(platform), board_(platform, bus_) {}

    Status setup() {
        const Status board = board_.begin();
        const power::ResetCause causes = platform_.system_power().reset_causes();
        reset_reason_ = power::classify(causes);
        // The register is read once and then only remembered, so the companion
        // link is told at boot: a watchdog bite in the field is diagnosable
        // from a phone, without the panel in hand.
        config_.config().set_reset_reason(reset_reason_);
        if (board == Status::Ok) config_.load();

        // The wake cause decides whether this boot becomes a device at all
        // (core/power/wake.h). Answered here, before the panel is painted and
        // before the loop is set up: a charger plugged into a device in a flight
        // bag must leave no trace on the glass and start nothing. The shell drops
        // the rails; every part is already in a known state, which is why this
        // sits after board_.begin() rather than in front of it.
        boot_cell_ = read_boot_cell();
        boot_path_ = power::boot_path(causes, platform_.button_down(), boot_cell_);
        flat_remembered_ = platform_.system_power().flat_on_glass();
        take_went_dark_flat();
        config_.config().set_went_dark_flat(went_dark_flat_);
        if (boot_path_ == power::BootPath::SleepAgain) {
            refused_frame_ = power::refused_frame(boot_cell_, flat_remembered_);
            return Status::Ok;
        }

        flyable_ = board == Status::Ok &&
                   ports::missing(board_.capabilities(), kRequired) == ports::Capability::None;

        draw_self_test();
        if (!flyable_) roles_.display.present(boot_fb_, ports::Refresh::Full, 0);

        if (board != Status::Ok) return board;
        if (!flyable_) return Status::Down;
        const Status loop = loop_.setup();
        state_.started = loop == Status::Ok;
        return loop;
    }

    void step(uint32_t now_ms) {
        if (!flyable_ && !shutdown_.going_down()) guard_cell(now_ms);
        if (flyable_ && !shutdown_.going_down()) {
            board_.poll(state_, now_ms);
            // Polled before the services run, so the pass that may write flash is
            // the pass that already knows the rail is going. One atomic read on
            // the silicon side; nothing here is allowed to be slower than that,
            // because the warning's whole value is the milliseconds it is early.
            if (platform_.system_power().take_supply_warning()) power_.on_supply_warning();
            loop_.step(now_ms);
            if (power_.cutoff()) shutdown_.request(power::ShutdownReason::LowBattery, now_ms);
            if (config_.config().power_off_requested()) {
                config_.config().clear_power_off_request();
                shutdown_.request(power::ShutdownReason::LinkRequest, now_ms);
            }
            if (config_.config().install_requested()) {
                config_.config().clear_install_request();
                shutdown_.request(power::ShutdownReason::Install, now_ms);
            }
        }
        shutdown_.tick(now_ms, platform_.button_down(), platform_.pad_down());
        drive_shutdown(now_ms);
        if (shutdown_.going_down()) screen_.settle_park(now_ms);
        remember_glass();
    }

    bool park_refusal(uint32_t now_ms) {
        if (refused_frame_ == power::RefusedFrame::Leave) return true;
        if (!refusal_asked_) {
            refusal_asked_ = true;
            refusal_since_ms_ = now_ms;
            if (refused_frame_ == power::RefusedFrame::FlatCell)
                screen_.park_for_flat_cell();
            else
                screen_.park_for_off();
        }
        screen_.settle_park(now_ms);
        remember_glass();
        if (!screen_.parking()) return true;
        // INFO: fc 21sep26 a panel that never reports ready must not hold a flat cell awake
        return now_ms - refusal_since_ms_ >= kRefusalParkCeilingMs;
    }

    power::RefusedFrame refused_frame() const { return refused_frame_; }

    ports::Capabilities capabilities() const { return board_.capabilities(); }
    ports::Capabilities degraded() const {
        return ports::missing(board_.capabilities(), kOptional);
    }

    // False when a required capability is missing: the loop refuses to fly, the
    // self-test page stays on the glass and the button still works.
    bool flyable() const { return flyable_; }
    power::ResetReason reset_reason() const { return reset_reason_; }
    bool went_dark_flat() const { return went_dark_flat_; }
    // Run, or straight back to SYSTEM OFF. The shell reads this immediately after
    // setup() and performs the second one.
    power::BootPath boot_path() const { return boot_path_; }
    const power::BootCell& boot_cell() const { return boot_cell_; }
    const Glass& boot_page() const { return boot_fb_; }
    // The rows behind that page, so a test can read what a part answered instead
    // of reading pixels back off the glass to find out.
    const BootPart* boot_rows() const { return boot_parts_; }

    power::ShutdownSequencer& shutdown() { return shutdown_; }
    const power::ShutdownSequencer& shutdown() const { return shutdown_; }
    // INFO: fc 12sep26 rails cut mid-frame leave the ink half-driven, and the sun develops it
    bool ready_to_power_off() const {
        return shutdown_.ready_to_power_off() && !installing() && !screen_.parking();
    }
    bool installing() const { return shutdown_.reason() == power::ShutdownReason::Install; }
    bool stowing() const { return shutdown_.reason() == power::ShutdownReason::Stow; }
    bool cell_ran_out() const { return shutdown_.reason() == power::ShutdownReason::LowBattery; }

    // Feeding through a deliberate shutdown is correct: the device is doing what
    // it was told, and a held button must not turn a power-off into a reboot.
    bool may_feed_watchdog(uint32_t now_ms) const {
        if (!flyable_ || shutdown_.going_down()) return true;
        return loop_.may_feed_watchdog(now_ms);
    }
    int stalled_service(uint32_t now_ms) const { return loop_.stalled_service(now_ms); }
    const char* service_name(int index) const { return loop_.feed_decision().name(index); }

    Board& board() { return board_; }
    bus::Bus& bus() { return bus_; }
    bus::State& state() { return state_; }
    Settings& settings() { return settings_; }
    const Settings& settings() const { return settings_; }
    const bus::State& state() const { return state_; }

    OwnshipService& ownship() { return ownship_; }
    PowerService& power() { return power_; }
    RadioService& radio() { return radio_; }
    TrafficService& traffic() { return traffic_; }
    AlarmService& alarm() { return alarm_; }
    NmeaService& nmea() { return nmea_; }
    ScreenService& screen() { return screen_; }
    ConfigLinkService& config() { return config_; }
    FlightLogService& flight_log() { return flight_log_; }
    CaptureService& capture() { return capture_; }
    diag::Recorder& diag() { return diag_; }

   private:
    // Which part answered, for the three footprints LilyGO ships more than one
    // part against. Everything here comes from the bring-up probes
    // (ports/inventory.h), never from what the image was compiled expecting, which
    // is the whole point of the page.
    const char* boot_detail(const BootPartSpec& spec) {
        const ports::Inventory& found = board_.inventory();
        switch (spec.capability) {
            case ports::Capability::Baro:
                return found.baro_address == 0 ? spec.wired_part : baro_part(found.baro_address);
            case ports::Capability::Display:
                return ports::has(board_.capabilities(), ports::Capability::Display)
                           ? found.panel
                           : spec.wired_part;
            case ports::Capability::Haptic:
                return found.haptic == ports::HapticKind::PinMotor ? "PIN" : spec.wired_part;
            default: return spec.wired_part;
        }
    }

    const char* baro_part(uint8_t address) {
        int n = skyblip::fmt_string(baro_part_, "BME280 ");
        n += skyblip::fmt_hex(baro_part_ + n, address, 2);
        baro_part_[n] = 0;
        return baro_part_;
    }

    power::BootCell read_boot_cell() {
        power::BootCell cell{};
        if (!ports::has(board_.capabilities(), ports::Capability::Battery)) return cell;
        uint16_t raw_mv = 0;
        cell.valid = platform_.read_battery_mv(raw_mv);
        cell.millivolts = power::calibrated_mv(raw_mv, settings_.battery_offset_mv);
        cell.external_power = platform_.external_power();
        return cell;
    }

    void take_went_dark_flat() {
        ports::SystemPower& retained = platform_.system_power();
        went_dark_flat_ = flat_remembered_ || retained.went_dark_flat();
        retained.set_went_dark_flat(boot_path_ == power::BootPath::SleepAgain && went_dark_flat_);
    }

    void remember_glass() {
        const bool flat = screen_.flat_on_glass();
        if (flat == flat_remembered_) return;
        flat_remembered_ = flat;
        platform_.system_power().set_flat_on_glass(flat);
        if (flat) platform_.system_power().set_went_dark_flat(true);
    }

    void guard_cell(uint32_t now_ms) {
        board_.poll_battery(now_ms);
        power_.tick(now_ms);
        if (power_.cutoff()) shutdown_.request(power::ShutdownReason::LowBattery, now_ms);
    }

    void draw_self_test() {
        const ports::Capabilities fitted = board_.capabilities();
        for (int i = 0; i < kBootPartCount; i++) {
            const BootPartSpec& spec = kBootParts[i];
            boot_parts_[i].name = spec.name;
            boot_parts_[i].state = ports::has(fitted, spec.capability)      ? PartState::Pass
                                   : ports::has(kRequired, spec.capability) ? PartState::Fail
                                                                            : PartState::Absent;
            boot_parts_[i].detail = boot_detail(spec);
        }

        boot_snapshot_.device_addr = roles_.device_addr;
        boot_snapshot_.reset_reason = power::to_string(reset_reason_);
        boot_snapshot_.went_dark_flat = went_dark_flat_;
        boot_snapshot_.parts = boot_parts_;
        boot_snapshot_.n_parts = kBootPartCount;
        boot_snapshot_.flyable = flyable_;
        boot_snapshot_.battery_valid = boot_cell_.valid;
        boot_snapshot_.battery_mv = boot_cell_.millivolts;
        const ports::Inventory& found = board_.inventory();
        for (uint8_t i = 0; i < found.i2c_count; i++)
            i2c_roles_[i] = boards::t_echo_plus::i2c_role(found.i2c_addresses[i]);
        boot_snapshot_.i2c_addresses = found.i2c_addresses;
        boot_snapshot_.i2c_roles = i2c_roles_;
        boot_snapshot_.n_i2c_addresses = found.i2c_count;
        draw_boot(boot_fb_, boot_snapshot_);
    }

    void drive_shutdown(uint32_t now_ms) {
        const power::ShutdownPhase phase = shutdown_.phase();
        if (phase == acted_phase_) return;
        acted_phase_ = phase;
        if (phase == power::ShutdownPhase::Off && installing()) {
            config_.record_update();
            roles_.dfu.trigger();
            return;
        }
        if (phase != power::ShutdownPhase::Parking) return;
        // The radio goes first. An armed dwell keeps the receiver and the PA
        // alive right through the seconds the panel takes to park.
        roles_.rf.abort();
        roles_.rf.sleep();
        publish_radio_asleep();
        // And now that nothing is armed, the second is nobody's: a settings change
        // still waiting for a free phase (core/timing/durable_write.h) goes to
        // flash here rather than dying with the rails. From this point the service
        // loop no longer runs, so this is the last chance there is.
        config_.flush_settings(now_ms);
        power_.record_last_pass(now_ms);
        capture_.park(now_ms);
        // Every peripheral that can be left driven is switched off by the owner
        // that drives it, because from here the service loop no longer runs: a
        // buzzer mid-pattern would sound until the rails drop.
        alarm_.park(now_ms);
        // The haptic is a part on a bus, so silence is not enough: a DRV2605 left
        // out of standby draws through the rail it shares with the flash, and its
        // enable pin has to be released before that rail goes
        // (core/power/shutdown.h kPowerDownOrder).
        board_.park();
        if (installing())
            screen_.park_for_install();
        else if (stowing())
            screen_.park_for_stow();
        else if (cell_ran_out())
            screen_.park_for_flat_cell();
        else
            screen_.set_power(false);
    }

    void publish_radio_asleep() {
        state_.rf.plan = timing::SlotPlan{};
        state_.rf.dwell = timing::DwellPhase{};
    }

    bus::Bus bus_{};
    bus::State state_{};
    diag::Recorder diag_{};
    Settings settings_{};
    Glass boot_fb_{};
    BootSnapshot boot_snapshot_{};
    P& platform_;
    Board board_;
    ports::Roles roles_{board_.roles()};
    runtime::Context ctx_{roles_, bus_, state_, diag_};
    RecordPool pool_{ctx_};
    RecordStore flights_store_{pool_, store::SectorOwner::Flights};
    RecordStore capture_store_{pool_, store::SectorOwner::Diagnostics};

    // Declared before the config service, which is handed it: the settings writer
    // asks core/power whether the cell will survive a write before it makes one.
    PowerService power_{ctx_, settings_};
    ConfigLinkService config_{ctx_, settings_, power_};
    OwnshipService ownship_{ctx_, settings_};
    RadioService radio_{ctx_, settings_};
    TrafficService traffic_{ctx_, kFeatures};
    AlarmService alarm_{ctx_, settings_};
    NmeaService nmea_{ctx_, kFeatures, config_.config()};
    FlightLogService flight_log_{ctx_, flights_store_, &capture_store_, config_.config()};
    CaptureService capture_{ctx_, capture_store_, flights_store_, settings_, config_.config()};
    ScreenService screen_{ctx_, settings_, config_.config(), alarm_, capture_, boot_snapshot_};

    // The log ticks after own-ship has published the fix and after the radio has
    // published the slot plan it defers to, and before the screen, which is the
    // only service that may spend a whole pass pushing pixels.
    // The tablet is told after the table and the levels for this pass are
    // settled and after the config service has drained the connection, and
    // before the two services that may spend a pass on flash or on pixels.
    static constexpr int kServiceCount = 10;
    runtime::Service* services_[kServiceCount]{&config_,  &ownship_, &power_, &radio_,
                                               &traffic_, &alarm_,   &nmea_,  &flight_log_,
                                               &capture_, &screen_};
    static constexpr const char* kServiceNames[kServiceCount] = {
        "config", "ownship", "power",      "radio",   "traffic",
        "alarm",  "nmea",    "flight_log", "capture", "screen"};
    runtime::Loop loop_{services_, kServiceCount, kServiceNames};

    BootPart boot_parts_[kBootPartCount]{};
    // The barometer's address as the page prints it. A member and not a local:
    // BootPart holds a pointer, and the page is drawn after boot_detail()
    // has returned.
    char baro_part_[10]{};
    const char* i2c_roles_[ports::Inventory::kMaxI2cAddresses]{};
    power::ShutdownSequencer shutdown_{};
    power::ShutdownPhase acted_phase_{power::ShutdownPhase::Running};
    power::ResetReason reset_reason_{power::ResetReason::Unknown};
    power::BootPath boot_path_{power::BootPath::Run};
    power::BootCell boot_cell_{};
    power::RefusedFrame refused_frame_{power::RefusedFrame::Leave};
    uint32_t refusal_since_ms_{0};
    bool refusal_asked_{false};
    bool flat_remembered_{false};
    bool went_dark_flat_{false};
    bool flyable_{false};
};

}  // namespace skyblip::go

#endif
