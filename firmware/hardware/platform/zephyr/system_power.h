#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SYSTEM_POWER_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SYSTEM_POWER_H
#if defined(__ZEPHYR__)

#include <hal/nrf_gpio.h>
#include <hal/nrf_spim.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#include "boards/lilygo/t_echo_plus/pins.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "hardware/platform/zephyr/power_down.h"
#include "hardware/platform/zephyr/rf.h"

namespace skyblip::platform::zephyr {

// INFO: fc 04sep26 entered only on the way to SYSTEM OFF, and ext_flash `has-dpd` sends RDPD at
// init
constexpr uint8_t kFlashDeepPowerDown = 0xB9;

// TODO: fc 03aug26 a generous guess, since no datasheet gives tDP, until one lands and the
// devicetree can carry it
constexpr uint32_t kFlashDeepPowerDownSettleUs = 200;

// INFO: fc 08nov26 MeshCore bounds the same BUSY wait at 10 ms (NRF52Board.cpp:338-359)
constexpr uint32_t kRadioBusyTimeoutUs = 10000;
constexpr uint32_t kRadioBusyPollUs = 10;
// INFO: fc 08nov26 STOP ends the byte on the wire, and one byte at the SPIM's slowest 125 kbps is
// 64 us
constexpr uint32_t kRadioSpimStopCeilingUs = 100;

// INFO: fc 08nov26 uart0 is never suspended without CONFIG_PM_DEVICE, so its pinctrl leaves TX
// driven high into SYSTEM OFF
BUILD_ASSERT(function_of(SKYBLIP_PSEL(DT_ALIAS(gnss_uart), group1, 0)) == NRF_FUN_UART_TX);
BUILD_ASSERT(function_of(SKYBLIP_PSEL(DT_ALIAS(gnss_uart), group2, 0)) == NRF_FUN_UART_RX);
BUILD_ASSERT(function_of(SKYBLIP_PSEL(DT_ALIAS(sensor_i2c), group1, 0)) == NRF_FUN_TWIM_SDA);
BUILD_ASSERT(function_of(SKYBLIP_PSEL(DT_ALIAS(sensor_i2c), group1, 1)) == NRF_FUN_TWIM_SCL);

constexpr uint32_t kGnssUartTxPin = pin_of(SKYBLIP_PSEL(DT_ALIAS(gnss_uart), group1, 0));
constexpr uint32_t kGnssUartRxPin = pin_of(SKYBLIP_PSEL(DT_ALIAS(gnss_uart), group2, 0));
constexpr uint32_t kGnssResetPin = DT_PROP(DT_GPIO_CTLR(DT_ALIAS(gnss_reset), gpios), port) * 32 +
                                   DT_GPIO_PIN(DT_ALIAS(gnss_reset), gpios);
constexpr uint32_t kSensorI2cSdaPin = pin_of(SKYBLIP_PSEL(DT_ALIAS(sensor_i2c), group1, 0));
constexpr uint32_t kSensorI2cSclPin = pin_of(SKYBLIP_PSEL(DT_ALIAS(sensor_i2c), group1, 1));

class SystemPower : public PowerDown {
   public:
    explicit SystemPower(const struct gpio_dt_spec& wake) : wake_(wake) {}

    void begin() {
        read_causes();
        arm_supply_monitor();
    }

    void system_off() override { power_off(boards::t_echo_plus::kFactoryDfuButton); }

    power::CellOnGlass cell_on_glass() const override {
        if (retained(kFlatOnGlassBit)) return power::CellOnGlass::Flat;
        if (retained(kLowOnGlassBit)) return power::CellOnGlass::Low;
        return power::CellOnGlass::None;
    }
    void set_cell_on_glass(power::CellOnGlass word) override {
        retain(kFlatOnGlassBit, word == power::CellOnGlass::Flat);
        retain(kLowOnGlassBit, word == power::CellOnGlass::Low);
    }
    bool went_dark_flat() const override { return retained(kWentDarkFlatBit); }
    void set_went_dark_flat(bool flat) override { retain(kWentDarkFlatBit, flat); }

   private:
    // INFO: fc 21sep26 a power-on clears GPREGRET2, and a magic makes anything else read as no
    static constexpr uint8_t kGlassMagic = 0xa8;
    static constexpr uint8_t kGlassMagicMask = 0xf8;
    static constexpr uint8_t kFlatOnGlassBit = 0x01;
    static constexpr uint8_t kWentDarkFlatBit = 0x02;
    static constexpr uint8_t kLowOnGlassBit = 0x04;

    static uint8_t retained_bits() {
        const uint8_t byte = read_glass_byte();
        if ((byte & kGlassMagicMask) != kGlassMagic) return 0;
        return static_cast<uint8_t>(byte & ~kGlassMagicMask);
    }

    static bool retained(uint8_t bit) { return (retained_bits() & bit) != 0; }

    static void retain(uint8_t bit, bool on) {
        const uint8_t was = retained_bits();
        const uint8_t bits = static_cast<uint8_t>(on ? was | bit : was & ~bit);
        write_glass_byte(bits == 0 ? 0 : static_cast<uint8_t>(kGlassMagic | bits));
    }

    static uint8_t read_glass_byte() {
#if DT_NODE_EXISTS(DT_NODELABEL(glass_retention))
        const struct device* area = DEVICE_DT_GET(DT_NODELABEL(glass_retention));
        uint8_t byte = 0;
        if (!device_is_ready(area)) return 0;
        if (retention_read(area, 0, &byte, sizeof(byte)) != 0) return 0;
        return byte;
#else
        return 0;
#endif
    }

    static void write_glass_byte(uint8_t byte) {
#if DT_NODE_EXISTS(DT_NODELABEL(glass_retention))
        const struct device* area = DEVICE_DT_GET(DT_NODELABEL(glass_retention));
        if (!device_is_ready(area)) return;
        (void)retention_write(area, 0, &byte, sizeof(byte));
#else
        (void)byte;
#endif
    }

    void perform(power::PowerDownStep step) override {
        switch (step) {
            case power::PowerDownStep::RadioSleep: radio_sleep(); return;
            case power::PowerDownStep::ExternalFlashDeepPowerDown: flash_deep_power_down(); return;
            case power::PowerDownStep::ExternalFlashLinesReleased:
                // INFO: fc 20nov26 CS goes last, because it latched the command above
                release(flash_write_protect_);
                release(flash_hold_);
                release(flash_cs_);
                return;
            case power::PowerDownStep::PanelLinesReleased: release_panel_lines(); return;
            case power::PowerDownStep::SensorBusReleased: release_sensor_bus(); return;
            case power::PowerDownStep::GnssStandby: lower(gnss_wake_); return;
            case power::PowerDownStep::GnssLinesReleased: release_gnss_lines(); return;
            case power::PowerDownStep::PeripheralRailOff:
                lower(peripheral_rail_);
                k_msleep(power::kRailSettleMs);
                return;
            case power::PowerDownStep::WakePinArmed: arm_wake(wake_); return;
        }
    }

    // INFO: fc 20nov26 the parked executor leaves this the bus's only writer, and the reset puts
    // the part in STDBY for SetSleep
    void radio_sleep() {
        gpio_pin_configure_dt(&radio_cs_, GPIO_OUTPUT_INACTIVE);
        // INFO: fc 08nov26 an executor abandoned at the ceiling may hold the bus lock, so it gets
        // the reset and no SPI
        if (Rf::abandoned()) quiesce_radio_bus();
        gpio_pin_configure_dt(&radio_reset_, GPIO_OUTPUT_ACTIVE);
        k_busy_wait(parts::sx1262::kResetLowUs);
        gpio_pin_set_dt(&radio_reset_, 0);
        if (!radio_ready() || Rf::abandoned()) return;
        uint8_t frame[2] = {parts::sx1262::kSetSleep, parts::sx1262::kSleepColdStartNoRtc};
        write_spi(radio_bus_, radio_cs_, frame, sizeof(frame));
        k_busy_wait(parts::sx1262::kSleepSettleUs);
    }

    // INFO: fc 08nov26 register writes only, so it needs none of the lock an aborted transfer kept
    static void quiesce_radio_bus() {
        NRF_SPIM_Type* spim = reinterpret_cast<NRF_SPIM_Type*>(DT_REG_ADDR(DT_ALIAS(radio_spi)));
        irq_disable(DT_IRQN(DT_ALIAS(radio_spi)));
        nrf_spim_int_disable(spim, NRF_SPIM_ALL_INTS_MASK);
        nrf_spim_task_trigger(spim, NRF_SPIM_TASK_STOP);
        for (uint32_t waited_us = 0; waited_us < kRadioSpimStopCeilingUs &&
                                     !nrf_spim_event_check(spim, NRF_SPIM_EVENT_STOPPED);
             waited_us++)
            k_busy_wait(1);
        nrf_spim_disable(spim);
    }

    bool radio_ready() {
        if (gpio_pin_configure_dt(&radio_busy_, GPIO_INPUT) != 0) return false;
        for (uint32_t waited_us = 0; waited_us < kRadioBusyTimeoutUs;
             waited_us += kRadioBusyPollUs) {
            const int busy = gpio_pin_get_dt(&radio_busy_);
            if (busy < 0) return false;
            if (busy == 0) return true;
            k_busy_wait(kRadioBusyPollUs);
        }
        return false;
    }

    // INFO: fc 20nov26 sent while the rail is still up, which is why it comes early in the order
    void flash_deep_power_down() {
        uint8_t frame[1] = {kFlashDeepPowerDown};
        write_spi(flash_bus_, flash_cs_, frame, sizeof(frame));
        k_busy_wait(kFlashDeepPowerDownSettleUs);
    }

    static void write_spi(const struct device* bus, const struct gpio_dt_spec& cs,
                          const uint8_t* data, size_t bytes) {
        if (!device_is_ready(bus)) return;
        gpio_pin_configure_dt(&cs, GPIO_OUTPUT_ACTIVE);
        struct spi_buf buffer{const_cast<uint8_t*>(data), bytes};
        struct spi_buf_set tx{&buffer, 1};
        spi_write(bus, &kSpiConfig, &tx);
        gpio_pin_set_dt(&cs, 0);
    }

    // INFO: fc 13sep26 SPIM2 drives SCK and MOSI until the pads stop being outputs
    void release_panel_lines() {
        release(epd_sck_);
        release(epd_mosi_);
        release(epd_dc_);
        release(epd_reset_);
        release(epd_busy_);
        release(epd_backlight_);
        release(epd_cs_);
    }

    static void release_sensor_bus() {
        disable_twim(DT_REG_ADDR(DT_ALIAS(sensor_i2c)), DT_IRQN(DT_ALIAS(sensor_i2c)));
        nrf_gpio_cfg_default(kSensorI2cSdaPin);
        nrf_gpio_cfg_default(kSensorI2cSclPin);
    }

    // INFO: fc 08nov26 the L76K pulls RESET_N up itself (Hardware Design 4.2.2), so a released line
    // never resets it
    void release_gnss_lines() {
        stop_uarte(DT_REG_ADDR(DT_ALIAS(gnss_uart)), DT_IRQN(DT_ALIAS(gnss_uart)));
        nrf_gpio_cfg_default(kGnssUartTxPin);
        nrf_gpio_cfg_default(kGnssUartRxPin);
        nrf_gpio_cfg_default(kGnssResetPin);
    }

    static constexpr struct spi_config kSpiConfig = {
        .frequency = 8000000U,
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_CONTROLLER,
        .slave = 0,
        .cs = {},
    };

    struct gpio_dt_spec wake_;

    const struct device* radio_bus_{DEVICE_DT_GET(DT_ALIAS(radio_spi))};
    const struct device* flash_bus_{DEVICE_DT_GET(DT_BUS(DT_NODELABEL(ext_flash)))};
    struct gpio_dt_spec radio_cs_ = GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(radio_spi), cs_gpios, 0);
    struct gpio_dt_spec radio_busy_ = GPIO_DT_SPEC_GET(DT_NODELABEL(radio_busy_gpio), gpios);
    struct gpio_dt_spec radio_reset_ = GPIO_DT_SPEC_GET(DT_ALIAS(radio_reset), gpios);
    struct gpio_dt_spec flash_cs_ =
        GPIO_DT_SPEC_GET_BY_IDX(DT_BUS(DT_NODELABEL(ext_flash)), cs_gpios, 0);
    struct gpio_dt_spec peripheral_rail_ = GPIO_DT_SPEC_GET(DT_ALIAS(peripheral_rail), gpios);
    struct gpio_dt_spec flash_write_protect_ =
        GPIO_DT_SPEC_GET(DT_ALIAS(flash_write_protect), gpios);
    struct gpio_dt_spec flash_hold_ = GPIO_DT_SPEC_GET(DT_ALIAS(flash_hold), gpios);
    struct gpio_dt_spec gnss_wake_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_wake), gpios);
    struct gpio_dt_spec epd_cs_ = GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(epd_spi), cs_gpios, 0);
    struct gpio_dt_spec epd_dc_ = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_dc_gpio), gpios);
    struct gpio_dt_spec epd_reset_ = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_reset_gpio), gpios);
    struct gpio_dt_spec epd_busy_ = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_busy_gpio), gpios);
    struct gpio_dt_spec epd_sck_ = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_sck_gpio), gpios);
    struct gpio_dt_spec epd_mosi_ = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_mosi_gpio), gpios);
    struct gpio_dt_spec epd_backlight_ = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_backlight_gpio), gpios);
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
