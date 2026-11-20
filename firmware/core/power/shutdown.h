#ifndef SKYBLIP_CORE_POWER_SHUTDOWN_H
#define SKYBLIP_CORE_POWER_SHUTDOWN_H

#include <cstdint>

namespace skyblip::power {

// INFO: fc 12sep26 Stow is the long press with the pad held: same road out, blank glass
enum class ShutdownReason : uint8_t {
    None,
    LongPress,
    Stow,
    LowBattery,
    LinkRequest,
    Install,
    Recovery
};
enum class ShutdownPhase : uint8_t { Running, Parking, AwaitRelease, Off };

// INFO: fc 13nov26 an install and a rebooting recovery come straight back, so neither signs off
constexpr bool ends_switched_off(ShutdownReason reason, bool recovery_powers_off) {
    switch (reason) {
        case ShutdownReason::None:
        case ShutdownReason::Install: return false;
        case ShutdownReason::Recovery: return recovery_powers_off;
        case ShutdownReason::LongPress:
        case ShutdownReason::Stow:
        case ShutdownReason::LowBattery:
        case ShutdownReason::LinkRequest: return true;
    }
    return true;
}

// INFO: fc 08nov26 P0.13 has no step: it enables U11, the AP2112K-3.3 feeding the SoC and radio
// (T-Echo schematic)
enum class PowerDownStep : uint8_t {
    // INFO: fc 08nov26 NRESET is pulsed before SetSleep, never after, so the part sleeps from
    // STDBY_RC whatever state it was in
    RadioSleep,
    ExternalFlashDeepPowerDown,
    ExternalFlashLinesReleased,
    // INFO: fc 13sep26 the panel sleeps at the park frame, but CS, DC and RES# stay driven high
    PanelLinesReleased,
    // INFO: fc 08nov26 the I2C peripheral stays enabled without device PM, so it is stopped and
    // its lines let go
    SensorBusReleased,
    // INFO: fc 08nov26 L76K WAKE_UP held low into SYSTEM OFF, as its pull-up wakes a powered
    // receiver to 29 mA
    GnssStandby,
    // INFO: fc 08nov26 the UART is stopped, and its lines and RESET_N are left undriven, unpulled
    // and never pulsed
    GnssLinesReleased,
    // INFO: fc 08nov26 its enable stays driven low into SYSTEM OFF, as a floating one restarted a
    // unit (Meshtastic #10510)
    PeripheralRailOff,
    WakePinArmed,
};

constexpr int kPowerDownStepCount = 9;

inline constexpr PowerDownStep kPowerDownOrder[kPowerDownStepCount] = {
    PowerDownStep::RadioSleep,
    PowerDownStep::ExternalFlashDeepPowerDown,
    PowerDownStep::ExternalFlashLinesReleased,
    PowerDownStep::PanelLinesReleased,
    PowerDownStep::SensorBusReleased,
    PowerDownStep::GnssStandby,
    PowerDownStep::GnssLinesReleased,
    PowerDownStep::PeripheralRailOff,
    PowerDownStep::WakePinArmed,
};

constexpr int step_order(PowerDownStep step) {
    for (int i = 0; i < kPowerDownStepCount; i++)
        if (kPowerDownOrder[i] == step) return i;
    return -1;
}

constexpr bool order_lists_each_step_once() {
    for (int i = 0; i < kPowerDownStepCount; i++)
        for (int j = i + 1; j < kPowerDownStepCount; j++)
            if (kPowerDownOrder[i] == kPowerDownOrder[j]) return false;
    return true;
}

static_assert(static_cast<int>(PowerDownStep::WakePinArmed) == kPowerDownStepCount - 1,
              "a step was added to the enum and not to the order");
static_assert(order_lists_each_step_once(), "a step listed twice is a step performed twice");
static_assert(step_order(PowerDownStep::WakePinArmed) == kPowerDownStepCount - 1,
              "the wake pin is armed last, after the rails it would otherwise race");
static_assert(step_order(PowerDownStep::ExternalFlashDeepPowerDown) <
                  step_order(PowerDownStep::PeripheralRailOff),
              "0xB9 sent to a flash whose rail is already down is a no-op");
static_assert(step_order(PowerDownStep::ExternalFlashDeepPowerDown) <
                  step_order(PowerDownStep::ExternalFlashLinesReleased),
              "the flash cannot be commanded over lines that have been released");
static_assert(step_order(PowerDownStep::GnssStandby) < step_order(PowerDownStep::GnssLinesReleased),
              "the GNSS is in standby before its reset line is let go");
static_assert(step_order(PowerDownStep::GnssLinesReleased) <
                  step_order(PowerDownStep::PeripheralRailOff),
              "a GNSS line left driven high back-feeds the receiver through its protection diodes");
static_assert(step_order(PowerDownStep::PanelLinesReleased) <
                  step_order(PowerDownStep::PeripheralRailOff),
              "a panel line left driven high back-feeds the panel through its protection diodes");
static_assert(step_order(PowerDownStep::ExternalFlashLinesReleased) <
                  step_order(PowerDownStep::PeripheralRailOff),
              "a flash line left driven high back-feeds the flash through its protection diodes");
static_assert(step_order(PowerDownStep::SensorBusReleased) <
                  step_order(PowerDownStep::PeripheralRailOff),
              "the sensor bus lets go of its lines while the parts on them still have a supply");
static_assert(step_order(PowerDownStep::PeripheralRailOff) == kPowerDownStepCount - 2,
              "the rail drops once every part behind it has been told and every line let go");

// INFO: fc 20nov26 the rail's discharge before the wake pin is armed, the 20 ms MB waits after
// driving the enables low (nRF52.cpp:2075)
constexpr uint32_t kRailSettleMs = 20;

class PowerDownSink {
   public:
    virtual ~PowerDownSink() = default;
    virtual void perform(PowerDownStep step) = 0;
};

void power_down(PowerDownSink& sink);

// INFO: fc 12nov26 GPREGRET for the factory bootloaders: each board's factory/README.md
constexpr uint8_t kNoBootloaderMagic = 0x00;
constexpr uint8_t kSkipBootloaderMagic = 0x6d;
constexpr uint8_t kUf2MassStorageMagic = 0x57;

enum class DfuButton : uint8_t { PressEntersDfu, HoldEntersDfu };

constexpr uint8_t boot_magic_for_system_off(DfuButton button, bool recovery_armed) {
    if (recovery_armed) return kUf2MassStorageMagic;
    if (button == DfuButton::PressEntersDfu) return kSkipBootloaderMagic;
    return kNoBootloaderMagic;
}

// INFO: fc 20nov26 long enough that it cannot be a page press, short enough to do with gloves on
constexpr uint32_t kLongPressHoldMs = 2000;

// INFO: fc 20nov26 the SSD1681 clocks out a full park refresh in about 2.5 s, and rails dropped
// earlier leave half an image
constexpr uint32_t kParkSettleMs = 3000;

// INFO: hk 02aug26 SENSE detects a level, so the button stays up 100 ms before the wake pin is
// armed, as in SoftRF (src/platform/nRF52.cpp:3199-3203)
constexpr uint32_t kReleaseSettleMs = 100;

class ButtonRelease {
   public:
    bool settled(uint32_t now_ms, bool button_down);

   private:
    bool released_{false};
    uint32_t released_at_ms_{0};
};

class ShutdownSequencer {
   public:
    // INFO: fc 20nov26 button_down is the debounced level, not the page-press edge, because a hold
    // produces no edges
    void tick(uint32_t now_ms, bool button_down, bool pad_down = false);

    void request(ShutdownReason reason, uint32_t now_ms);

    ShutdownPhase phase() const { return phase_; }
    ShutdownReason reason() const { return reason_; }
    bool going_down() const { return phase_ != ShutdownPhase::Running; }
    bool ready_to_power_off() const { return phase_ == ShutdownPhase::Off; }

    uint32_t held_ms(uint32_t now_ms) const;

   private:
    void enter(ShutdownPhase phase, uint32_t now_ms);

    ShutdownPhase phase_{ShutdownPhase::Running};
    ShutdownReason reason_{ShutdownReason::None};
    uint32_t phase_since_ms_{0};
    uint32_t hold_since_ms_{0};
    ButtonRelease release_{};
    bool holding_{false};
    bool stowing_{false};
    // INFO: fc 20nov26 the press that woke the device is still down at boot, and counting it as a
    // hold powers off again
    bool hold_armed_{false};
    bool pad_armed_{false};
};

}  // namespace skyblip::power

#endif
