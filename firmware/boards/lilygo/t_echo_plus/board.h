#ifndef SKYBLIP_BOARDS_T_ECHO_PLUS_BOARD_H
#define SKYBLIP_BOARDS_T_ECHO_PLUS_BOARD_H

#include <algorithm>

#include "boards/common/producer.h"
#include "boards/lilygo/t_echo_plus/glass.h"
#include "boards/lilygo/t_echo_plus/i2c_scan.h"
#include "boards/lilygo/t_echo_plus/imu_mount.h"
#include "boards/lilygo/t_echo_plus/pins.h"
#include "boards/lilygo/t_echo_plus/probed_roles.h"
#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/events/input.h"
#include "core/events/link.h"
#include "core/events/sensor.h"
#include "core/input/contact.h"
#include "core/timing/transmit.h"
#include "hardware/parts/bhi260/bhi260.h"
#include "hardware/parts/bhi260/image_store.h"
#include "hardware/parts/bme280/bme280.h"
#include "hardware/parts/drv2605/drv2605.h"
#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "hardware/platform/contract.h"
#include "ports/inventory.h"
#include "ports/null.h"
#include "ports/roles.h"
#include "runtime/tasks.h"

namespace skyblip::boards {

constexpr const char* kImuWithoutImage = "NOBLOB";

static_assert(t_echo_plus::kGlassWidth == parts::Ssd1681::kGlassWidth &&
                  t_echo_plus::kGlassHeight == parts::Ssd1681::kGlassHeight,
              "the glass on this board is what its controller drives");

// INFO: fc 03oct26 four forced conversions a second feed IIR 4: 0.75 s of lag for 0.1 mA
constexpr uint32_t kBarometerConversionPeriodMs = 250;

static_assert(kBarometerConversionCeilingMs < kBarometerConversionPeriodMs,
              "a barometer conversion is read or given up before the next one is triggered");

static_assert(parts::L76k::kBurstStartMs + parts::L76k::kSearchingBurstMs +
                      runtime::kServicePassCeilingMs <
                  timing::dwell_of(timing::kWindowAMs).start_ms,
              "a solution at the receiver's target rate is parsed before slot 0 arms its burst");

template <class Platform>
class TEchoPlus {
   public:
    using Gnss = parts::L76k;
    TEchoPlus(Platform& platform, bus::GoBus& bus, ports::SolutionRate solution_rate)
        : platform_(platform),
          bus_(bus),
          radio_(platform.spi(io::BusId::Radio), platform.gpio(), platform.delay(),
                 t_echo_plus::kRadioBusy, t_echo_plus::kRadioReset, t_echo_plus::kRadioDio1),
          display_(platform.spi(io::BusId::Epd), platform.gpio(), platform.delay(),
                   t_echo_plus::kEpdDc, t_echo_plus::kEpdReset, t_echo_plus::kEpdBusy,
                   t_echo_plus::kEpdBacklight, platform.glass_rotation()),
          gnss_(solution_rate, platform.uart(io::BusId::Gnss), platform.uart_rate(io::BusId::Gnss),
                &platform.gpio(), t_echo_plus::kGnssReset),
          haptic_(platform.i2c(io::BusId::Sensor), platform.gpio(), platform.delay(),
                  t_echo_plus::kHapticEnable, parts::Drv2605::Actuator::Erm),
          imu_(platform.i2c(io::BusId::Sensor)),
          imu_store_(platform.imu_flash()),
          rf_(radio_, platform.clock(), bus.rf),
          producer_(platform, bus),
          capabilities_(platform.capabilities()) {
        platform_.wire(t_echo_plus::kPinMap);
        platform_.link().deliver_to(bus);
        platform_.central().deliver_to(bus);
    }

    // INFO: fc 11nov26 not in the constructor: the product is a global, built before the watchdog
    void probe() {
        // INFO: fc 11nov26 a register read back proves the radio: asserting it hides a dead MISO
        if (radio_.probe() == Status::Ok) capabilities_ = capabilities_ | ports::Capability::Rf;

        identify_panel();
        establish_haptic();
        establish_buzzer();
        establish_barometer();
        prove_roles();
    }

    // INFO: fc 20nov26 a part present but refusing to start is a degraded state here, not a missing
    // capability
    Status begin() {
        const Status status = platform_.begin();
        if (status != Status::Ok) return status;
        // INFO: fc 14nov26 after the platform's 50 ms TCXO wait, which a BHI260AP's start-up needs
        // too
        establish_inclinometer();
        if (ports::has(capabilities_, ports::Capability::Display)) display_.begin();
        read_stored_imu_image();
        if (!ports::has(capabilities_, ports::Capability::Rf)) return Status::Ok;
        return rf_.begin();
    }

    // INFO: fc 20nov26 called on every road into SYSTEM OFF, so the haptic driver parks before its
    // rail collapses
    void park() { haptic_.park(); }

    ports::Capabilities capabilities() const { return capabilities_; }
    uint32_t barometer_faults() const { return barometer_faults_; }

    // INFO: fc 20nov26 the self-test page reads which part answered where, which a capability bit
    // does not say
    const ports::Inventory& inventory() const { return inventory_; }

    // INFO: fc 14nov26 only the self-test page reads the bus scan, so it runs once, when first
    // asked
    bool take_bus_inventory() {
        if (bus_scanned_) return false;
        bus_scanned_ = true;
        t_echo_plus::scan_i2c(platform_.i2c(io::BusId::Sensor), inventory_);
        inventory_.barometer_address =
            inventory_.has_i2c_address(parts::Bme280::kAddressPrimary)
                ? parts::Bme280::kAddressPrimary
            : inventory_.has_i2c_address(parts::Bme280::kAddressAlternate)
                ? parts::Bme280::kAddressAlternate
                : 0;
        return true;
    }

    ports::Roles roles() {
        return ports::Roles{
            platform_.clock(),
            probed_rf_,
            ports::has(capabilities_, ports::Capability::Link)
                ? static_cast<ports::Link&>(platform_.link())
                : null_.link,
            ports::has(capabilities_, ports::Capability::Display)
                ? static_cast<ports::Display&>(display_)
                : null_.display,
            ports::has(capabilities_, ports::Capability::Storage)
                ? static_cast<ports::KeyValueStore&>(platform_.key_value())
                : null_.key_value,
            ports::has(capabilities_, ports::Capability::Storage)
                ? static_cast<ports::FlashRegion&>(platform_.log_flash())
                : null_.log_flash,
            probed_annunciator_,
            ports::has(capabilities_, ports::Capability::Dfu)
                ? static_cast<ports::Dfu&>(platform_.dfu())
                : null_.dfu,
            ports::has(capabilities_, ports::Capability::DieTemperature)
                ? static_cast<ports::DieTemperature&>(platform_.die_temperature())
                : null_.die_temperature,
            ports::has(capabilities_, ports::Capability::Indicator)
                ? static_cast<ports::Indicator&>(platform_.indicator())
                : null_.indicator,
            ports::has(capabilities_, ports::Capability::Gnss) ? static_cast<ports::Gnss&>(gnss_)
                                                               : null_.gnss,
            platform_.identity(),
            platform_.random(),
            capabilities_,
        };
    }

    // INFO: fc 11nov26 the Go's alone, so it rides beside the roles rather than in them
    ports::Central& central() {
        return ports::has(capabilities_, ports::Capability::Link)
                   ? static_cast<ports::Central&>(platform_.central())
                   : null_.central;
    }

    void poll_barometer_on_pps(const timing::ClockState& clock, uint32_t now_ms) {
        if (!ports::has(capabilities_, ports::Capability::Barometer)) return;
        if (barometer_converting_) collect_barometer(now_ms);
        if (barometer_converting_) return;
        if (barometer_due(clock, now_ms - last_barometer_ms_)) {
            last_barometer_ms_ = now_ms;
            barometer_quarter_ = 0;
            trigger_barometer(now_ms);
        } else if (quarter_barometer_due(clock, now_ms)) {
            barometer_quarter_ = clock.since_edge_ms / kBarometerConversionPeriodMs;
            trigger_barometer(now_ms);
        }
    }

    void trigger_barometer(uint32_t now_ms) {
        barometer_triggered_ms_ = now_ms;
        barometer_converting_ = platform_.trigger_barometer();
        if (!barometer_converting_) barometer_faults_++;
    }

    // INFO: fc 22nov26 G.1.16: every conversion is read, so the newest pressure is a quarter old
    void collect_barometer(uint32_t now_ms) {
        platform::BarometerReading reading{};
        if (platform_.read_barometer(reading)) {
            barometer_converting_ = false;
            bus_.barometer.push(events::BarometerSample{
                reading.pressure_mpa,
                barometer_triggered_ms_ + parts::Bme280::kConversionMidpointMs,
                reading.temperature_dc, reading.temperature_valid});
            return;
        }
        if (now_ms - barometer_triggered_ms_ < kBarometerConversionCeilingMs) return;
        barometer_converting_ = false;
        barometer_faults_++;
    }

    bool quarter_barometer_due(const timing::ClockState& clock, uint32_t now_ms) const {
        if (!clock.pps_locked)
            return now_ms - barometer_triggered_ms_ >= kBarometerConversionPeriodMs;
        const uint32_t quarter = clock.since_edge_ms / kBarometerConversionPeriodMs;
        return quarter > barometer_quarter_ && quarter < kBarometerConversionsPerSecond;
    }

    void poll_battery(uint32_t now_ms) { producer_.poll_battery(capabilities_, now_ms); }

    void poll(bus::State& state, uint32_t now_ms) {
        rf_.service(now_ms);
        // INFO: fc 17oct26 serves the host annunciator's guards: a test catches an output left on
        platform_.annunciator().service(now_ms);

        if (ports::has(capabilities_, ports::Capability::Gnss)) {
            gnss_.request_satellites_in_view(state.gnss.levels_wanted);
            gnss_.service(now_ms);
            if (gnss_.poll()) {
                bus_.gnss.push(gnss_.solution());
                state.gnss.sky = gnss_.sky();
                state.gnss.levels_fresh = gnss_.levels_fresh();
                state.gnss.reject = gnss_.last_reject();
                state.gnss.rejected = gnss_.rejected();
            }
            state.gnss.levels_live = gnss_.satellites_in_view_live();
        }

        poll_battery(now_ms);
        poll_inclinometer(state, now_ms);

        const bool button = producer_.publish_contact(events::Contact::Button, button_,
                                                      platform_.button_down(), now_ms);
        const bool pad =
            producer_.publish_contact(events::Contact::Pad, pad_, platform_.pad_down(), now_ms);
        contacts_active_ = button || pad;

        producer_.publish_clock(state);
        poll_barometer_on_pps(state.clock, now_ms);
    }

    // INFO: fc 19nov26 the loop sleeps until the earliest of these, or until an edge wakes it
    uint32_t due_in_ms(const timing::ClockState& clock, uint32_t now_ms) const {
        if (contacts_active_ || gnss_bringing_up() || imu_bringing_up())
            return runtime::kServicePassCeilingMs;
        uint32_t due_ms = std::min(producer_.battery_due_in_ms(capabilities_, now_ms),
                                   barometer_due_in_ms(clock, now_ms));
        if (imu_.running()) due_ms = std::min(due_ms, imu_.read_due_in_ms(now_ms));
        return due_ms;
    }

    typename Platform::Rf& rf() { return rf_; }
    parts::L76k& gnss() { return gnss_; }
    parts::Bhi260& imu() { return imu_; }
    parts::Bhi260ImageStore& imu_store() { return imu_store_; }
    bool imu_without_image() const {
        return ports::has(capabilities_, ports::Capability::Inclinometer) && !imu_image_bootable();
    }
    parts::Ssd1681& display() { return display_; }
    parts::Drv2605& haptic() { return haptic_; }

   private:
    static constexpr uint32_t kBarometerConversionsPerSecond =
        kBarometerPublishPeriodMs / kBarometerConversionPeriodMs;
    static constexpr uint32_t kBarometerMeasurementMs =
        (parts::Bme280::kMeasurementUs + 999) / 1000;

    uint32_t barometer_due_in_ms(const timing::ClockState& clock, uint32_t now_ms) const {
        if (!ports::has(capabilities_, ports::Capability::Barometer))
            return runtime::kServiceRestCeilingMs;
        // INFO: fc 22nov26 a conversion is read once it can be done, then on each pass until it is
        if (barometer_converting_) {
            const uint32_t converting_ms = now_ms - barometer_triggered_ms_;
            return converting_ms < kBarometerMeasurementMs ? kBarometerMeasurementMs - converting_ms
                                                           : runtime::kServicePassCeilingMs;
        }
        const uint32_t published_ms =
            boards::barometer_due_in_ms(clock, now_ms - last_barometer_ms_);
        const uint32_t period_ms = kBarometerConversionPeriodMs;
        if (!clock.pps_locked)
            return std::min(published_ms,
                            runtime::remaining_ms(now_ms - barometer_triggered_ms_, period_ms));
        const uint32_t next_quarter = clock.since_edge_ms / period_ms + 1;
        if (next_quarter >= kBarometerConversionsPerSecond) return published_ms;
        return std::min(published_ms, next_quarter * period_ms - clock.since_edge_ms);
    }

    // INFO: fc 19nov26 a probed part waits on its own timers, and a line it says wakes the loop
    bool gnss_bringing_up() const {
        return ports::has(capabilities_, ports::Capability::Gnss) && !gnss_.configured() &&
               !gnss_.degraded() && !gnss_.probing();
    }

    bool imu_bringing_up() const {
        if (!ports::has(capabilities_, ports::Capability::Inclinometer)) return false;
        if (imu_store_.writing()) return true;
        switch (imu_.stage()) {
            case parts::Bhi260::Stage::Absent:
            case parts::Bhi260::Stage::Failed:
            case parts::Bhi260::Stage::Running: return false;
            case parts::Bhi260::Stage::Idle: return imu_image_bootable();
            default: return true;
        }
    }

    void prove_roles() {
        if (ports::has(capabilities_, ports::Capability::Rf)) probed_rf_.prove(rf_);
        // INFO: fc 20nov26 buzzer or haptic, one role and two parts, so a dead buzzer pin still
        // leaves the pulse
        if (ports::has(capabilities_, ports::Capability::Buzzer) ||
            ports::has(capabilities_, ports::Capability::Haptic))
            probed_annunciator_.prove(platform_.annunciator());
    }

    ports::Capabilities without(ports::Capability bit) const {
        return ports::without(capabilities_, bit);
    }

    // INFO: fc 20nov26 an unread ID leaves the panel Unknown, the safest refresh policy
    void identify_panel() {
        parts::PanelId panel_id{};
        platform_.read_panel_id(panel_id);
        display_.adopt(panel_id);
        inventory_.panel_name = display_.panel_name();
    }

    // INFO: fc 06aug26 the DRV2605 answering grants the haptic, never the board being a Plus:
    // README.md
    void establish_haptic() {
        capabilities_ = without(ports::Capability::Haptic);
        inventory_.haptic = ports::HapticKind::None;
        if (haptic_.begin() != Status::Ok) return;
        platform_.annunciator().attach_haptic(haptic_);
        inventory_.haptic = ports::HapticKind::WaveformDriver;
        capabilities_ = capabilities_ | ports::Capability::Haptic | ports::Capability::HapticDriver;
    }

    // INFO: fc 18sep26 a fitted piezo stage holds P0.06 down, an empty pad follows the pull-up up
    void establish_buzzer() {
        capabilities_ = platform_.buzzer_pin_held_low() ? capabilities_ | ports::Capability::Buzzer
                                                        : without(ports::Capability::Buzzer);
    }

    void establish_barometer() {
        capabilities_ = platform_.barometer_answers() ? capabilities_ | ports::Capability::Barometer
                                                      : without(ports::Capability::Barometer);
    }

    void establish_inclinometer() {
        capabilities_ = without(ports::Capability::Inclinometer);
        if (imu_.probe() != Status::Ok) return;
        capabilities_ = capabilities_ | ports::Capability::Inclinometer;
    }

    void poll_inclinometer(bus::State& state, uint32_t now_ms) {
        state.imu.stage = imu_.stage_text();
        state.imu.fault = imu_.fault_text();
        state.imu.fifo_bytes = imu_.fifo_bytes();
        state.imu.unparsed_events = imu_.unparsed_events();
        state.imu.error = imu_.hub_error();
        state.imu.interrupt = imu_.interrupt_status();
        state.imu.meta_event = imu_.meta_event();
        state.imu.sensor_error = imu_.sensor_error();
        state.imu.errored_sensor = imu_.errored_sensor();
        if (ports::has(capabilities_, ports::Capability::Inclinometer)) {
            read_stored_imu_image();
            keep_imu_image_stored(state, now_ms);
            if (imu_.stage() == parts::Bhi260::Stage::Idle) load_imu(now_ms);
            imu_.service(now_ms);
            if (imu_.poll())
                bus_.accelerometer.push(t_echo_plus::device_frame(imu_.acceleration(), now_ms));
        }
        publish_imu_image(state);
    }

    void read_stored_imu_image() {
        if (!ports::has(capabilities_, ports::Capability::Inclinometer)) return;
        if (imu_store_.holding() == parts::Bhi260ImageStore::Holding::Unread) imu_store_.check();
    }

    bool imu_image_linked() const { return !platform_.imu_firmware().empty(); }

    bool imu_image_bootable() const {
        return imu_image_linked() || imu_store_.holds(platform_.imu_firmware_digest());
    }

    void load_imu(uint32_t now_ms) {
        if (imu_image_linked()) {
            linked_image_ = parts::LinkedImage{platform_.imu_firmware()};
            imu_.load(linked_image_, now_ms);
        } else if (imu_image_bootable()) {
            imu_.load(imu_store_, now_ms);
        }
    }

    // INFO: fc 03oct26 only a confirmed image writes: a reverted one must not leave its blob behind
    void keep_imu_image_stored(bus::State& state, uint32_t now_ms) {
        if (!imu_image_linked()) return;
        if (!imu_image_store_tried_ && !imu_store_.holds(platform_.imu_firmware_digest()) &&
            platform_.dfu().confirmed()) {
            imu_image_store_tried_ = true;
            imu_store_.begin_write(platform_.imu_firmware(), platform_.imu_firmware_digest());
        }
        for (uint32_t step = 0; step < kImuImageStepsPerPass && imu_store_.writing(); step++) {
            const uint32_t cost_ms = imu_store_.next_cost_ms();
            if (cost_ms > 0 &&
                !state.rf.book_flash_window(now_ms, platform_.clock().milliseconds(), cost_ms))
                return;
            imu_store_.step();
        }
    }

    void publish_imu_image(bus::State& state) const {
        state.imu.bootable = !imu_without_image();
        if (state.imu.fault[0] == 0 && !state.imu.bootable) state.imu.fault = kImuWithoutImage;
        dfu::HubImageReport& report = state.imu.image;
        report = dfu::HubImageReport{};
        if (!ports::has(capabilities_, ports::Capability::Inclinometer)) return;
        switch (imu_store_.holding()) {
            case parts::Bhi260ImageStore::Holding::Unread:
            case parts::Bhi260ImageStore::Holding::Blank:
                report.holding = dfu::HubImage::Missing;
                return;
            case parts::Bhi260ImageStore::Holding::Corrupt:
                report.holding = dfu::HubImage::Corrupt;
                return;
            case parts::Bhi260ImageStore::Holding::Unreadable:
                report.holding = dfu::HubImage::Unreadable;
                return;
            case parts::Bhi260ImageStore::Holding::Writing:
                report.holding = dfu::HubImage::Writing;
                return;
            case parts::Bhi260ImageStore::Holding::Held:
                report.holding = dfu::HubImage::Held;
                for (size_t i = 0; i < dfu::HubImageReport::kDigestBytes; i++)
                    report.digest[i] = imu_store_.digest()[i];
                return;
        }
    }

    // INFO: fc 03oct26 as the record store's bulk erase: a window at a time, eight steps at most
    static constexpr uint32_t kImuImageStepsPerPass = 8;

    Platform& platform_;
    bus::GoBus& bus_;
    parts::Sx1262 radio_;
    parts::Ssd1681 display_;
    Gnss gnss_;
    parts::Drv2605 haptic_;
    parts::Bhi260 imu_;
    parts::Bhi260ImageStore imu_store_;
    parts::LinkedImage linked_image_{ConstByteSpan{}};
    bool imu_image_store_tried_{false};
    typename Platform::Rf rf_;
    ports::Inventory inventory_{};
    bool bus_scanned_{false};
    input::Contact button_{t_echo_plus::kButtonHoldMs};
    input::Contact pad_{t_echo_plus::kPadSettleMs};
    ports::NullRoles null_{};
    Producer<Platform> producer_;
    bool contacts_active_{false};
    t_echo_plus::ProbedRf probed_rf_{null_.rf};
    t_echo_plus::ProbedAnnunciator probed_annunciator_{null_.annunciator};
    ports::Capabilities capabilities_;
    uint32_t last_barometer_ms_{0};
    uint32_t barometer_triggered_ms_{0};
    uint32_t barometer_quarter_{0};
    uint32_t barometer_faults_{0};
    bool barometer_converting_{false};
};

}  // namespace skyblip::boards

#endif
