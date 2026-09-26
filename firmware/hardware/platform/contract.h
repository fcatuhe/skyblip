#ifndef SKYBLIP_HARDWARE_PLATFORM_CONTRACT_H
#define SKYBLIP_HARDWARE_PLATFORM_CONTRACT_H

#include <cstdint>
#include <type_traits>
#include <utility>

#include "core/util/result.h"
#include "core/util/span.h"
#include "hardware/io/io.h"
#include "hardware/parts/ssd1681/panel.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "ports/annunciator.h"
#include "ports/capabilities.h"
#include "ports/clock.h"
#include "ports/dfu.h"
#include "ports/die_temperature.h"
#include "ports/flash_region.h"
#include "ports/indicator.h"
#include "ports/kvstore.h"
#include "ports/link.h"
#include "ports/system_power.h"
#include "ports/watchdog.h"

namespace skyblip::platform {

struct BaroReading {
    uint32_t pressure_mpa{0};
    int16_t temperature_decicelsius{0};
    bool temperature_valid{false};
};

template <class P>
constexpr bool fills_the_platform_contract() {
    using Self = P&;

    static_assert(
        std::is_convertible_v<decltype(std::declval<Self>().spi(io::BusId::Radio)), io::Spi&>,
        "platform: spi(io::BusId) -> io::Spi&");
    static_assert(
        std::is_convertible_v<decltype(std::declval<Self>().i2c(io::BusId::Sensor)), io::I2c&>,
        "platform: i2c(io::BusId) -> io::I2c&");
    static_assert(
        std::is_convertible_v<decltype(std::declval<Self>().uart(io::BusId::Gnss)), io::Uart&>,
        "platform: uart(io::BusId) -> io::Uart&");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().uart_rate(io::BusId::Gnss)),
                                        io::UartRate&>,
                  "platform: uart_rate(io::BusId) -> io::UartRate&, the receiver's baud");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().gpio()), io::Gpio&>,
                  "platform: gpio() -> io::Gpio&");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().delay()), io::Delay&>,
                  "platform: delay() -> io::Delay&, the windows a datasheet asks a part to hold");
    static_assert(
        std::is_void_v<decltype(std::declval<Self>().wire(std::declval<const io::PinMap&>()))>,
        "platform: wire(const io::PinMap&), the board's map of the lines a part drives");

    static_assert(std::is_convertible_v<decltype(std::declval<Self>().clock()), ports::Clock&>,
                  "platform: clock() -> ports::Clock&");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().link()), ports::Link&>,
                  "platform: link() -> ports::Link&");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().kv()), ports::KvStore&>,
                  "platform: kv() -> ports::KvStore&");
    static_assert(
        std::is_convertible_v<decltype(std::declval<Self>().log_flash()), ports::FlashRegion&>,
        "platform: log_flash() -> ports::FlashRegion&");
    static_assert(
        std::is_convertible_v<decltype(std::declval<Self>().annunciator()), ports::Annunciator&>,
        "platform: annunciator() -> ports::Annunciator&");
    static_assert(
        std::is_convertible_v<decltype(std::declval<Self>().indicator()), ports::Indicator&>,
        "platform: indicator() -> ports::Indicator&");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().dfu()), ports::Dfu&>,
                  "platform: dfu() -> ports::Dfu&");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().die_temperature()),
                                        ports::DieTemperature&>,
                  "platform: die_temperature() -> ports::DieTemperature&");
    static_assert(
        std::is_convertible_v<decltype(std::declval<Self>().watchdog()), ports::Watchdog&>,
        "platform: watchdog() -> ports::Watchdog&");
    static_assert(
        std::is_convertible_v<decltype(std::declval<Self>().system_power()), ports::SystemPower&>,
        "platform: system_power() -> ports::SystemPower&");

    static_assert(std::is_convertible_v<decltype(std::declval<Self>().begin()), Status>,
                  "platform: begin() -> Status");
    static_assert(
        std::is_convertible_v<decltype(std::declval<Self>().capabilities()), ports::Capabilities>,
        "platform: capabilities() -> ports::Capabilities, what this unit was found to have");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().device_addr()), uint32_t>,
                  "platform: device_addr() -> uint32_t");

    static_assert(
        std::is_convertible_v<
            decltype(std::declval<Self>().read_baro(std::declval<BaroReading&>())), bool>,
        "platform: read_baro(BaroReading&) -> bool, one fetch of the part, which becomes "
        "events::BaroSample. Temperature carries its own validity: a part that answers with "
        "pressure and no temperature still answers true");
    static_assert(
        std::is_convertible_v<
            decltype(std::declval<Self>().read_battery_mv(std::declval<uint16_t&>())), bool>,
        "platform: read_battery_mv(uint16_t&) -> bool, which becomes events::BatterySample");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().external_power()), bool>,
                  "platform: external_power() -> bool");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().button_down()), bool>,
                  "platform: button_down() -> bool, which becomes events::ContactEvent");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().pad_down()), bool>,
                  "platform: pad_down() -> bool, which becomes events::ContactEvent");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().buzzer_pin_held_low()), bool>,
                  "platform: buzzer_pin_held_low() -> bool, read before any driver owns the pin");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().glass_rotation()),
                                        parts::GlassRotation>,
                  "platform: glass_rotation() -> parts::GlassRotation");
    static_assert(std::is_convertible_v<decltype(std::declval<Self>().read_panel_signature(
                                            std::declval<parts::PanelSignature&>())),
                                        bool>,
                  "platform: read_panel_signature(parts::PanelSignature&) -> bool");
    static_assert(
        std::is_convertible_v<decltype(std::declval<Self>().imu_firmware()), ConstByteSpan>,
        "platform: imu_firmware() -> ConstByteSpan, the image the BHI260AP is booted from");

    static_assert(std::is_void_v<decltype(void(std::declval<Self>().pps()))>,
                  "platform: pps(), the edge the clock's phase is carried from");

    return true;
}

}  // namespace skyblip::platform

#endif
