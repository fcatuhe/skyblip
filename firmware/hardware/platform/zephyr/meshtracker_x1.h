#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_MESHTRACKER_X1_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_MESHTRACKER_X1_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

#include "core/util/result.h"
#include "hardware/platform/zephyr/adsb_receiver.h"
#include "hardware/platform/zephyr/ag3335.h"
#include "hardware/platform/zephyr/bluetooth_tag.h"
#include "hardware/platform/zephyr/dfu.h"
#include "hardware/platform/zephyr/flash_region.h"
#include "hardware/platform/zephyr/io.h"
#include "hardware/platform/zephyr/link.h"
#include "hardware/platform/zephyr/meshtracker_x1_lines.h"
#include "hardware/platform/zephyr/meshtracker_x1_power.h"
#include "ports/capabilities.h"
#include "ports/null.h"

namespace skyblip::platform::zephyr {

static_assert(!IS_ENABLED(CONFIG_SKYBLIP_ACTIVATION),
              "Bravo is never activated, so the X1 provisions no unit key");

class MeshtrackerX1 {
   public:
    using AdsbReceiver = zephyr::AdsbReceiver;

    // INFO: fc 12nov26 what the boot path is decided on: why the SoC woke, the button and the cell
    void begin_wake() {
        system_power_.read_causes();
        (void)lines_.begin_inputs();
    }

    Status begin() {
        system_power_.arm_supply_monitor();
        if (lines_.begin() != Status::Ok) return Status::Down;
        if (!device_is_ready(radio_spi_device_)) return Status::Down;
        gnss_ready_ = gnss_.begin() == Status::Ok;
        // TODO: fc 12nov26 P0.09 powers the YSN8900 per SoftRF and Seeed, unverified until #169
        rtc_powered_ = gpio_pin_configure_dt(&rtc_enable_, GPIO_OUTPUT_ACTIVE) == 0;
        (void)log_flash_.begin();
        link_up_ = link_.begin() == Status::Ok;
        return Status::Ok;
    }

    io::Spi& spi(io::BusId) { return radio_spi_; }
    io::Uart& uart(io::BusId) { return gnss_.uart(); }
    io::I2c& i2c(io::BusId bus) {
        if (bus == io::BusId::Rtc) return rtc_i2c_;
        return lines_.i2c(bus);
    }
    io::Gpio& gpio() { return lines_.gpio(); }
    io::Delay& delay() { return lines_.delay(); }
    const struct gpio_dt_spec& radio_irq() const { return radio_irq_; }

    zephyr::Clock& clock() { return lines_.clock(); }
    zephyr::Link& link() { return link_; }
    zephyr::KeyValueStore& key_value() { return lines_.key_value(); }
    zephyr::FlashRegion& log_flash() { return log_flash_; }
    zephyr::Annunciator& annunciator() { return lines_.annunciator(); }
    zephyr::Indicator& indicator() { return lines_.indicator(); }
    zephyr::Dfu& dfu() { return dfu_; }
    zephyr::DieTemperature& die_temperature() { return lines_.die_temperature(); }
    zephyr::Pps& pps() { return gnss_.pps(); }
    zephyr::Watchdog& watchdog() { return lines_.watchdog(); }
    MeshtrackerX1Power& system_power() { return system_power_; }
    ports::KeylessIdentity& identity() { return identity_; }

    bool button_down() { return lines_.button_down(); }
    bool read_battery_mv(uint16_t& out_mv) { return lines_.read_battery_mv(out_mv); }
    bool external_power() { return lines_.external_power(); }

    bool gnss_wired() const { return gnss_ready_; }
    bool pps_armed() const { return gnss_.pps_armed(); }
    bool storage_mounted() const { return lines_.storage_mounted(); }
    bool link_up() const { return link_up_; }
    bool rtc_powered() const { return rtc_powered_; }

    // INFO: fc 12nov26 the board grants the radio, the barometer and the haptic driver from their
    // probes
    ports::Capabilities capabilities() const {
        ports::Capabilities found =
            lines_.capabilities() | ports::Capability::Link | ports::Capability::Dfu;
        if (device_is_ready(gnss_uart_device_)) found |= ports::Capability::Gnss;
        return found;
    }

   private:
    // TODO: fc 12nov26 the SX1262's 8 MHz mode 0 SPI, unverified for the LR2021 until #169
    static constexpr struct spi_config kSpiConfig = {
        .frequency = 8000000U,
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
        .slave = 0,
        .cs = {},
    };

    MeshtrackerX1Power system_power_{};
    MeshtrackerX1Lines lines_{};
    const struct device* radio_spi_device_{DEVICE_DT_GET(DT_ALIAS(radio_spi))};
    const struct device* gnss_uart_device_{DEVICE_DT_GET(DT_ALIAS(gnss_uart))};
    struct gpio_dt_spec radio_cs_ = GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(radio_spi), cs_gpios, 0);
    struct gpio_dt_spec radio_irq_ = GPIO_DT_SPEC_GET(DT_NODELABEL(radio_irq_gpio), gpios);
    struct gpio_dt_spec rtc_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(rtc_enable), gpios);
    Spi radio_spi_{radio_spi_device_, kSpiConfig, radio_cs_};
    I2c rtc_i2c_{DEVICE_DT_GET(DT_ALIAS(rtc_i2c))};
    Ag3335Wiring gnss_{};
    FlashRegion log_flash_{PARTITION_ID(log_partition)};
    Dfu dfu_{};
    zephyr::Link& link_{zephyr::link()};
    ports::KeylessIdentity identity_{zephyr::bluetooth_tag()};
    bool gnss_ready_{false};
    bool rtc_powered_{false};
    bool link_up_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
