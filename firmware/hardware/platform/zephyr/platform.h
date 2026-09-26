#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_PLATFORM_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_PLATFORM_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#include <cstddef>

#include "core/util/span.h"
#include "hardware/parts/ssd1681/panel.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "hardware/platform/contract.h"
#include "hardware/platform/zephyr/annunciator.h"
#include "hardware/platform/zephyr/baro.h"
#include "hardware/platform/zephyr/battery.h"
#include "hardware/platform/zephyr/clock.h"
#include "hardware/platform/zephyr/dfu.h"
#include "hardware/platform/zephyr/die_temperature.h"
#include "hardware/platform/zephyr/flash_region.h"
#include "hardware/platform/zephyr/imu_firmware.h"
#include "hardware/platform/zephyr/indicator.h"
#include "hardware/platform/zephyr/io.h"
#include "hardware/platform/zephyr/kvstore.h"
#include "hardware/platform/zephyr/link.h"
#include "hardware/platform/zephyr/pps.h"
#include "hardware/platform/zephyr/rf.h"
#include "hardware/platform/zephyr/system_power.h"
#include "hardware/platform/zephyr/watchdog.h"
#include "ports/capabilities.h"

// Two questions only the board port can answer, because on this SoC they have to
// be asked BEFORE the bus drivers claim the pins at POST_KERNEL: the e-paper's
// fingerprint (its one data line has to be reversed, and the SPIM owns MOSI and
// SCK from init onwards) and the buzzer pin read high-Z against a pull-up (the
// PWM peripheral drives it from init onwards). Both are taken once, at
// PRE_KERNEL_1, by boards/<board>/board.c, and read back here.
//
// This is the Zephyr board-hook idiom - the same shape as upstream's
// board_early_init_hook() - and it is why the acquisition is not in the panel
// driver: a driver that could only be called during board init would be a driver
// nothing could call.
extern "C" {
size_t board_panel_fingerprint(uint8_t* out, size_t capacity);
int board_buzzer_pin_held_low(void);
}

namespace skyblip::platform::zephyr {

class Platform {
   public:
    using Rf = zephyr::Rf;
    using Link = zephyr::Link;

    Status begin() {
        system_power_.begin();
        if (!device_is_ready(gpio0_) || !device_is_ready(gpio1_)) return Status::Down;
        if (!device_is_ready(radio_spi_dev_)) return Status::Down;
        if (gnss_uart_.begin() != Status::Ok) return Status::Down;
        // The gated rails are raised at board level so MCUboot sees them too;
        // what is still owed here is the SX1262 TCXO settling time.
        k_msleep(50);
        storage_mounted_ = kv_.begin() == Status::Ok;
        // A log partition that refuses to open is a device that flies and logs
        // nothing, not a device that refuses to fly: the region reports its own
        // readiness and the flight log service reads it.
        (void)log_flash_.begin();
        annunciator_.begin();
        indicator_.begin();
        link_up_ = link_.begin(device_addr()) == Status::Ok;
        baro_ = baro76_.ready() ? &baro76_ : (baro77_.ready() ? &baro77_ : nullptr);
        gpio_pin_configure_dt(&button_, GPIO_INPUT);
        gpio_pin_configure_dt(&pad_, GPIO_INPUT);
        pps_armed_ = pps_.begin() == Status::Ok;
        return Status::Ok;
    }

    void wire(const io::PinMap&) {}

    io::Spi& spi(io::BusId id) {
        if (id == io::BusId::Epd) return epd_spi_;
        return radio_spi_;
    }
    io::Uart& uart(io::BusId) { return gnss_uart_; }
    io::UartRate& uart_rate(io::BusId) { return gnss_uart_; }
    io::Gpio& gpio() { return gpio_; }
    io::I2c& i2c(io::BusId) { return sensor_i2c_; }

    zephyr::Clock& clock() { return clock_; }
    zephyr::Link& link() { return link_; }
    zephyr::KvStore& kv() { return kv_; }
    zephyr::FlashRegion& log_flash() { return log_flash_; }
    zephyr::Annunciator& annunciator() { return annunciator_; }
    zephyr::Indicator& indicator() { return indicator_; }
    zephyr::Dfu& dfu() { return dfu_; }
    zephyr::Pps& pps() { return pps_; }
    bool pps_armed() const { return pps_armed_; }
    bool storage_mounted() const { return storage_mounted_; }
    bool link_up() const { return link_up_; }
    zephyr::Baro* baro() { return baro_; }
    zephyr::Battery& battery() { return battery_; }
    zephyr::DieTemperature& die_temperature() { return die_temperature_; }
    zephyr::Watchdog& watchdog() { return watchdog_; }
    zephyr::SystemPower& system_power() { return system_power_; }

    bool button_down() { return gpio_pin_get_dt(&button_) == 1; }

    // TODO: fc 12sep26 read P0.11 on a bench, the polarity is SoftRF's alone (fcatuhe/skyblip#60)
    bool pad_down() { return gpio_pin_get_dt(&pad_) == 1; }

    // INFO: fc 12sep26 the Plus mounts the glass a quarter turn off the scan (SoftRF EPD.cpp:84)
    parts::GlassRotation glass_rotation() const { return parts::GlassRotation::Deg270; }

    // The 21 bytes the board port clocked out of the panel, or false if it could
    // not take them at all.
    bool read_panel_signature(parts::PanelSignature& out) {
        uint8_t raw[parts::kPanelSignatureBytes] = {0};
        if (board_panel_fingerprint(raw, sizeof(raw)) != sizeof(raw)) return false;
        for (int i = 0; i < parts::kPanelIdBytesA; i++) out.a[i] = raw[i];
        for (int i = 0; i < parts::kPanelIdBytesB; i++) out.b[i] = raw[parts::kPanelIdBytesA + i];
        out.read = true;
        return true;
    }

    static ConstByteSpan imu_firmware() { return ConstByteSpan(kImuFirmware, kImuFirmwareBytes); }

    // INFO: fc 18sep26 our Plus reads this pin low and our plain T-Echo reads it high
    bool buzzer_pin_held_low() const { return board_buzzer_pin_held_low() != 0; }
    bool read_baro(BaroReading& out) { return baro_ != nullptr && baro_->read_baro(out); }
    bool read_battery_mv(uint16_t& out_mv) { return battery_.read_mv(out_mv); }
    bool external_power() { return zephyr::Battery::external_power(); }

    // Probe only: what silicon answered, before anything is brought up.
    ports::Capabilities capabilities() const {
        ports::Capabilities c =
            ports::Capability::Storage | ports::Capability::Dfu | ports::Capability::Link;
        if (gpio_is_ready_dt(&button_) && gpio_is_ready_dt(&pad_)) c |= ports::Capability::Contacts;
        if (device_is_ready(epd_spi_dev_)) c |= ports::Capability::Display;
        if (device_is_ready(gnss_uart_dev_)) c |= ports::Capability::Gnss;
        if (device_is_ready(baro76_dev_) || device_is_ready(baro77_dev_))
            c |= ports::Capability::Baro;
        if (device_is_ready(battery_dev_)) c |= ports::Capability::Battery;
        // Only when the driver answered. A build with no CONFIG_TEMP_NRF5, or a
        // devicetree without the node, is a device with no die reading - and
        // saying so once here is what keeps a zero out of the status reply.
        if (die_temperature_.ready()) c |= ports::Capability::DieTemperature;
        // All three lamps or none. A partially populated LED node would be a
        // board file half-edited, and a table that can show two of its three
        // colours is a vocabulary a pilot cannot read.
        if (indicator_.ready()) c |= ports::Capability::Indicator;
        return c;
    }

    // INFO: fc 19sep26 hwinfo_nrf emits DEVICEID[1] then DEVICEID[0], both big-endian
    uint32_t device_addr() const {
        uint8_t id[8] = {0};
        const ssize_t n = hwinfo_get_device_id(id, sizeof(id));
        if (n < 8) return 0;
        uint32_t a = 0;
        for (size_t i = 5; i < 8; i++) a = (a << 8) | id[i];
        return a & 0x00FFFFFFu;
    }

   private:
    static constexpr struct spi_config kSpiCfg = {
        .frequency = 8000000U,
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
        .slave = 0,
        .cs = {},
    };

    const struct device* gpio0_{DEVICE_DT_GET(DT_NODELABEL(gpio0))};
    const struct device* gpio1_{DEVICE_DT_GET(DT_NODELABEL(gpio1))};
    const struct device* radio_spi_dev_{DEVICE_DT_GET(DT_ALIAS(radio_spi))};
    const struct device* epd_spi_dev_{DEVICE_DT_GET(DT_ALIAS(epd_spi))};
    const struct device* gnss_uart_dev_{DEVICE_DT_GET(DT_ALIAS(gnss_uart))};
    // Both candidate barometer addresses are declared. Whichever part is fitted
    // becomes ready, the other never does.
    const struct device* baro76_dev_{DEVICE_DT_GET(DT_NODELABEL(bme280_76))};
    const struct device* baro77_dev_{DEVICE_DT_GET(DT_NODELABEL(bme280_77))};
    const struct device* battery_dev_{DEVICE_DT_GET(DT_NODELABEL(vbatt))};
    const struct device* sensor_i2c_dev_{DEVICE_DT_GET(DT_ALIAS(sensor_i2c))};
    // Assigned, not brace-initialised: the DT_SPEC macros ARE brace lists, so
    // member{MACRO} nests them one level too deep and the first field eats the
    // whole struct.
    struct pwm_dt_spec buzzer_ = PWM_DT_SPEC_GET(DT_ALIAS(buzzer));
    struct gpio_dt_spec haptic_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(haptic_enable), gpios);
    struct gpio_dt_spec button_ = GPIO_DT_SPEC_GET(DT_ALIAS(button), gpios);
    struct gpio_dt_spec pad_ = GPIO_DT_SPEC_GET(DT_ALIAS(pad), gpios);
    // _OR, not _GET: a board file with no LED node has to build. That is the seam
    // - the specs come back null, ready() answers false, and the device runs the
    // same table and lights nothing.
    struct gpio_dt_spec led_green_ = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_green), gpios, {0});
    struct gpio_dt_spec led_red_ = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_red), gpios, {0});
    struct gpio_dt_spec led_blue_ = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_blue), gpios, {0});
    struct gpio_dt_spec radio_cs_ = GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(radio_spi), cs_gpios, 0);
    struct gpio_dt_spec epd_cs_ = GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(epd_spi), cs_gpios, 0);

    zephyr::Clock clock_{};
    Gpio gpio_{gpio0_, gpio1_};
    Spi radio_spi_{radio_spi_dev_, kSpiCfg, radio_cs_};
    Spi epd_spi_{epd_spi_dev_, kSpiCfg, epd_cs_};
    Uart gnss_uart_{gnss_uart_dev_};
    I2c sensor_i2c_{sensor_i2c_dev_};
    // The default haptic: the pin, driven directly. The board replaces it the
    // moment it finds a waveform driver on the bus, which on this board is the
    // only way a pulse is ever produced.
    PinMotor motor_{haptic_enable_};
    Annunciator annunciator_{buzzer_, motor_};
    Indicator indicator_{led_green_, led_red_, led_blue_};
    KvStore kv_{};
    FlashRegion log_flash_{};
    Dfu dfu_{};
    zephyr::Link& link_{zephyr::link()};
    zephyr::Baro baro76_{baro76_dev_};
    zephyr::Baro baro77_{baro77_dev_};
    zephyr::Baro* baro_{nullptr};
    zephyr::Battery battery_{battery_dev_};
    zephyr::DieTemperature die_temperature_{};
    zephyr::Pps pps_{};
    bool pps_armed_{false};
    bool storage_mounted_{false};
    bool link_up_{false};
    zephyr::Watchdog watchdog_{};
    zephyr::SystemPower system_power_{button_};
};

static_assert(fills_the_platform_contract<Platform>());

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
