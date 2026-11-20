#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_T_ECHO_PLUS_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_T_ECHO_PLUS_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#include <cstddef>

#include "core/util/span.h"
#include "hardware/parts/bme280/bme280.h"
#include "hardware/parts/ssd1681/panel.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "hardware/platform/contract.h"
#include "hardware/platform/zephyr/annunciator.h"
#include "hardware/platform/zephyr/battery.h"
#include "hardware/platform/zephyr/central.h"
#include "hardware/platform/zephyr/clock.h"
#include "hardware/platform/zephyr/dfu.h"
#include "hardware/platform/zephyr/die_temperature.h"
#include "hardware/platform/zephyr/flash_region.h"
#include "hardware/platform/zephyr/identity.h"
#include "hardware/platform/zephyr/imu_firmware.h"
#include "hardware/platform/zephyr/indicator.h"
#include "hardware/platform/zephyr/io.h"
#include "hardware/platform/zephyr/kvstore.h"
#include "hardware/platform/zephyr/link.h"
#include "hardware/platform/zephyr/loop_wake.h"
#include "hardware/platform/zephyr/pps.h"
#include "hardware/platform/zephyr/random.h"
#include "hardware/platform/zephyr/rf.h"
#include "hardware/platform/zephyr/system_power.h"
#include "hardware/platform/zephyr/watchdog.h"
#include "ports/capabilities.h"

// INFO: fc 20nov26 board.c takes these at PRE_KERNEL_1, before the SPIM and PWM drivers claim the
// pins at POST_KERNEL
extern "C" {
size_t board_panel_id(uint8_t* out, size_t capacity);
int board_buzzer_pin_held_low(void);
}

namespace skyblip::platform::zephyr {

static_assert(IS_ENABLED(CONFIG_SKYBLIP_ACTIVATION),
              "the T-Echo Plus provisions the unit key, which only an activated product trusts");
static_assert(IS_ENABLED(CONFIG_SKYBLIP_IMU_IMAGE),
              "the T-Echo Plus boots its BHI260AP hub from an image the build pins");

class TEchoPlus {
   public:
    using Rf = zephyr::Rf;
    using Link = zephyr::Link;

    Status begin() {
        system_power_.begin();
        if (!device_is_ready(gpio0_) || !device_is_ready(gpio1_)) return Status::Down;
        if (!device_is_ready(radio_spi_device_)) return Status::Down;
        if (gnss_uart_.begin() != Status::Ok) return Status::Down;
        // INFO: fc 20nov26 board.c raised the rails for MCUboot too, so only the SX1262 TCXO's
        // settling is owed here
        k_msleep(50);
        (void)key_value_.begin();
        // INFO: fc 20nov26 a log partition that fails to open still flies, since the flight log
        // reads the region's readiness
        (void)log_flash_.begin();
        (void)imu_flash_.begin();
        annunciator_.begin();
        indicator_.begin();
        // INFO: fc 18oct26 the key goes into UICR before Bluetooth starts
        (void)identity_.begin();
        (void)link_.begin();
        (void)bme280_.begin();
        gpio_pin_configure_dt(&button_, GPIO_INPUT);
        gpio_pin_configure_dt(&pad_, GPIO_INPUT);
        (void)wake_on_edges(button_, button_edges_);
        (void)wake_on_edges(pad_, pad_edges_);
        (void)pps_.begin();
        return Status::Ok;
    }

    void wire(const io::PinMap&) {}

    io::Spi& spi(io::BusId bus) {
        if (bus == io::BusId::Epd) return epd_spi_;
        return radio_spi_;
    }
    io::Uart& uart(io::BusId) { return gnss_uart_; }
    static_assert(IS_ENABLED(CONFIG_UART_USE_RUNTIME_CONFIGURE),
                  "uart_rate() needs uart_configure(), which Nordic's SoC defconfig turns off");
    io::UartRate& uart_rate(io::BusId) { return gnss_uart_; }
    io::Gpio& gpio() { return gpio_; }
    io::Delay& delay() { return delay_; }
    io::I2c& i2c(io::BusId) { return sensor_i2c_; }

    zephyr::Clock& clock() { return clock_; }
    zephyr::Link& link() { return link_; }
    zephyr::Central& central() { return central_; }
    zephyr::KeyValueStore& key_value() { return key_value_; }
    zephyr::FlashRegion& log_flash() { return log_flash_; }
    zephyr::FlashRegion& imu_flash() { return imu_flash_; }
    zephyr::Annunciator& annunciator() { return annunciator_; }
    zephyr::Indicator& indicator() { return indicator_; }
    zephyr::Dfu& dfu() { return dfu_; }
    zephyr::Pps& pps() { return pps_; }
    zephyr::Identity& identity() { return identity_; }
    zephyr::Random& random() { return random_; }
    zephyr::Battery& battery() { return battery_; }
    zephyr::DieTemperature& die_temperature() { return die_temperature_; }
    zephyr::Watchdog& watchdog() { return watchdog_; }
    zephyr::SystemPower& system_power() { return system_power_; }

    bool button_down() { return gpio_pin_get_dt(&button_) == 1; }

    // TODO: fc 12sep26 read P0.11 on a bench, the polarity is SoftRF's alone (fcatuhe/skyblip#60)
    bool pad_down() { return gpio_pin_get_dt(&pad_) == 1; }

    // INFO: fc 12sep26 the Plus mounts the glass a quarter turn off the scan (SoftRF EPD.cpp:84)
    parts::GlassRotation glass_rotation() const { return parts::GlassRotation::Degrees270; }

    bool read_panel_id(parts::PanelId& out) {
        uint8_t raw[parts::kPanelIdBytes] = {0};
        if (board_panel_id(raw, sizeof(raw)) != sizeof(raw)) return false;
        for (int i = 0; i < parts::kPanelIdBytes2d; i++) out.register_2d[i] = raw[i];
        for (int i = 0; i < parts::kPanelIdBytes2e; i++)
            out.register_2e[i] = raw[parts::kPanelIdBytes2d + i];
        out.valid = true;
        return true;
    }

    static ConstByteSpan imu_firmware() { return ConstByteSpan(kImuFirmware, kImuFirmwareBytes); }
    static const Sha256::Digest& imu_firmware_digest() { return kImuFirmwareDigest; }

    // INFO: fc 18sep26 our Plus reads this pin low and our plain T-Echo reads it high
    bool buzzer_pin_held_low() const { return board_buzzer_pin_held_low() != 0; }
    bool trigger_barometer() { return bme280_.trigger(); }
    bool read_barometer(BarometerReading& out) {
        parts::Bme280::Reading reading{};
        if (!bme280_.read(reading)) return false;
        out.pressure_mpa = reading.pressure_mpa;
        out.temperature_dc = reading.temperature_dc;
        out.temperature_valid = true;
        return true;
    }
    bool read_divider_mv(uint16_t& out_mv) { return battery_.read_mv(out_mv); }
    bool external_power() { return zephyr::Battery::external_power(); }

    // INFO: fc 11nov26 asks no part: a global's constructor calls it before the watchdog
    ports::Capabilities capabilities() const {
        ports::Capabilities found =
            ports::Capability::Storage | ports::Capability::Dfu | ports::Capability::Link;
        if (gpio_is_ready_dt(&button_) && gpio_is_ready_dt(&pad_))
            found |= ports::Capability::Contacts;
        if (device_is_ready(epd_spi_device_)) found |= ports::Capability::Display;
        if (device_is_ready(gnss_uart_device_)) found |= ports::Capability::Gnss;
        if (device_is_ready(battery_device_)) found |= ports::Capability::Battery;
        // INFO: fc 20nov26 only when the driver answered, so a build without the sensor reports no
        // die temperature rather than zero
        if (die_temperature_.ready()) found |= ports::Capability::DieTemperature;
        // INFO: fc 20nov26 all three lamps or none, because a table showing two of three colours
        // cannot be read
        if (indicator_.ready()) found |= ports::Capability::Indicator;
        return found;
    }

    bool barometer_answers() const { return bme280_.answers(); }

   private:
    static constexpr struct spi_config kSpiConfig = {
        .frequency = 8000000U,
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_CONTROLLER,
        .slave = 0,
        .cs = {},
    };

    const struct device* gpio0_{DEVICE_DT_GET(DT_NODELABEL(gpio0))};
    const struct device* gpio1_{DEVICE_DT_GET(DT_NODELABEL(gpio1))};
    const struct device* radio_spi_device_{DEVICE_DT_GET(DT_ALIAS(radio_spi))};
    const struct device* epd_spi_device_{DEVICE_DT_GET(DT_ALIAS(epd_spi))};
    const struct device* gnss_uart_device_{DEVICE_DT_GET(DT_ALIAS(gnss_uart))};
    const struct device* battery_device_{DEVICE_DT_GET(DT_NODELABEL(vbatt))};
    const struct device* sensor_i2c_device_{DEVICE_DT_GET(DT_ALIAS(sensor_i2c))};
    // INFO: fc 20nov26 assigned, because the DT_SPEC macros are brace lists and member{MACRO} would
    // nest them one level too deep
    struct pwm_dt_spec buzzer_ = PWM_DT_SPEC_GET(DT_ALIAS(buzzer));
    struct gpio_dt_spec haptic_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(haptic_enable), gpios);
    struct gpio_dt_spec button_ = GPIO_DT_SPEC_GET(DT_ALIAS(button), gpios);
    struct gpio_dt_spec pad_ = GPIO_DT_SPEC_GET(DT_ALIAS(pad), gpios);
    struct gpio_callback button_edges_{};
    struct gpio_callback pad_edges_{};
    // INFO: fc 20nov26 _OR so a board without an LED node builds, and the indicator reports not
    // ready
    struct gpio_dt_spec led_green_ = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_green), gpios, {0});
    struct gpio_dt_spec led_red_ = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_red), gpios, {0});
    struct gpio_dt_spec led_blue_ = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_blue), gpios, {0});
    struct gpio_dt_spec radio_cs_ = GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(radio_spi), cs_gpios, 0);
    struct gpio_dt_spec epd_cs_ = GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(epd_spi), cs_gpios, 0);

    zephyr::Clock clock_{};
    Gpio gpio_{gpio0_, gpio1_};
    Delay delay_{};
    Spi radio_spi_{radio_spi_device_, kSpiConfig, radio_cs_};
    Spi epd_spi_{epd_spi_device_, kSpiConfig, epd_cs_};
    Uart gnss_uart_{gnss_uart_device_};
    I2c sensor_i2c_{sensor_i2c_device_};
    parts::Bme280 bme280_{sensor_i2c_};
    PinMotor motor_{haptic_enable_};
    Annunciator annunciator_{buzzer_, motor_};
    Indicator indicator_{led_green_, led_red_, led_blue_};
    KeyValueStore key_value_{};
    FlashRegion log_flash_{PARTITION_ID(log_partition)};
    FlashRegion imu_flash_{PARTITION_ID(imu_image_partition)};
    Dfu dfu_{};
    zephyr::Link& link_{zephyr::link()};
    zephyr::Central& central_{zephyr::central()};
    zephyr::Random random_{};
    zephyr::Identity identity_{random_};
    zephyr::Battery battery_{battery_device_};
    zephyr::DieTemperature die_temperature_{};
    zephyr::Pps pps_{};
    zephyr::Watchdog watchdog_{};
    zephyr::SystemPower system_power_{button_};
};

static_assert(fills_the_platform_contract<TEchoPlus>());

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
