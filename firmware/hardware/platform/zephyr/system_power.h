#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SYSTEM_POWER_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SYSTEM_POWER_H
#if defined(__ZEPHYR__)

#include <hal/nrf_gpio.h>
#include <hal/nrf_spim.h>
#include <hal/nrf_twim.h>
#include <hal/nrf_uarte.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/dt-bindings/pinctrl/nrf-pinctrl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include "boards/lilygo/t_echo_plus/pins.h"
#include "core/power/shutdown.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "hardware/platform/zephyr/dfu.h"
#include "hardware/platform/zephyr/reset_causes.h"
#include "hardware/platform/zephyr/rf.h"
#include "hardware/platform/zephyr/supply_monitor.h"
#include "ports/system_power.h"

namespace skyblip::platform::zephyr {

// JESD216 / MX25R1635F 8.9 and ZD25WQ16B 8.20, the one opcode both parts spell
// the same: Deep Power-Down.
//
// INFO: fc 04sep26 entered only on the way to SYSTEM OFF; ext_flash `has-dpd` sends RDPD at init
constexpr uint8_t kFlashDeepPowerDown = 0xB9;

// INFO: fc 03aug26 tDP for neither candidate part is in project/reference/, and
// `t-enter-dpd` has no default in jedec,spi-nor-common.yaml, so reading it from
// the devicetree would not compile. Both parts specify it in microseconds; this
// is generous by any reading of that and costs nothing, because the next step
// takes the supply away. Move it to the devicetree the day a datasheet lands.
constexpr uint32_t kFlashDeepPowerDownSettleUs = 200;

// INFO: fc 08nov26 MeshCore bounds the same BUSY wait at 10 ms (NRF52Board.cpp:338-359)
constexpr uint32_t kRadioBusyTimeoutUs = 10000;
constexpr uint32_t kRadioBusyPollUs = 10;
// INFO: fc 08nov26 STOP ends the byte on the wire, and one byte at the SPIM's slowest 125 kbps is
// 64 us
constexpr uint32_t kRadioSpimStopCeilingUs = 100;

// INFO: fc 08nov26 uart0 is never suspended without CONFIG_PM_DEVICE, so its pinctrl leaves TX
// driven high into SYSTEM OFF
#define SKYBLIP_GNSS_UART_GROUP(group) \
    DT_CHILD(DT_PINCTRL_BY_NAME(DT_ALIAS(gnss_uart), default, 0), group)
#define SKYBLIP_GNSS_UART_PSEL(group) DT_PROP_BY_IDX(SKYBLIP_GNSS_UART_GROUP(group), psels, 0)

BUILD_ASSERT(DT_PROP_LEN(SKYBLIP_GNSS_UART_GROUP(group1), psels) == 1 &&
             ((SKYBLIP_GNSS_UART_PSEL(group1) >> NRF_FUN_POS) & NRF_FUN_MSK) == NRF_FUN_UART_TX);
BUILD_ASSERT(DT_PROP_LEN(SKYBLIP_GNSS_UART_GROUP(group2), psels) == 1 &&
             ((SKYBLIP_GNSS_UART_PSEL(group2) >> NRF_FUN_POS) & NRF_FUN_MSK) == NRF_FUN_UART_RX);

constexpr uint32_t kGnssUartTxPin = (SKYBLIP_GNSS_UART_PSEL(group1) >> NRF_PIN_POS) & NRF_PIN_MSK;
constexpr uint32_t kGnssUartRxPin = (SKYBLIP_GNSS_UART_PSEL(group2) >> NRF_PIN_POS) & NRF_PIN_MSK;
constexpr uint32_t kGnssResetPin = DT_PROP(DT_GPIO_CTLR(DT_ALIAS(gnss_reset), gpios), port) * 32 +
                                   DT_GPIO_PIN(DT_ALIAS(gnss_reset), gpios);

#undef SKYBLIP_GNSS_UART_PSEL
#undef SKYBLIP_GNSS_UART_GROUP

#define SKYBLIP_SENSOR_I2C_GROUP \
    DT_CHILD(DT_PINCTRL_BY_NAME(DT_ALIAS(sensor_i2c), default, 0), group1)
#define SKYBLIP_SENSOR_I2C_PSEL(idx) DT_PROP_BY_IDX(SKYBLIP_SENSOR_I2C_GROUP, psels, idx)

BUILD_ASSERT(DT_PROP_LEN(SKYBLIP_SENSOR_I2C_GROUP, psels) == 2 &&
             ((SKYBLIP_SENSOR_I2C_PSEL(0) >> NRF_FUN_POS) & NRF_FUN_MSK) == NRF_FUN_TWIM_SDA &&
             ((SKYBLIP_SENSOR_I2C_PSEL(1) >> NRF_FUN_POS) & NRF_FUN_MSK) == NRF_FUN_TWIM_SCL);

constexpr uint32_t kSensorI2cSdaPin = (SKYBLIP_SENSOR_I2C_PSEL(0) >> NRF_PIN_POS) & NRF_PIN_MSK;
constexpr uint32_t kSensorI2cSclPin = (SKYBLIP_SENSOR_I2C_PSEL(1) >> NRF_PIN_POS) & NRF_PIN_MSK;

#undef SKYBLIP_SENSOR_I2C_PSEL
#undef SKYBLIP_SENSOR_I2C_GROUP

// INFO: fc 08nov26 ten 8N1 characters at 4800 baud, the slowest rate the L76K driver walks
constexpr uint32_t kGnssUartStopTimeoutUs = 21000;
constexpr uint32_t kGnssUartStopPollUs = 10;

class SystemPower : public ports::SystemPower, private power::PowerDownSink {
   public:
    explicit SystemPower(const struct gpio_dt_spec& wake) : wake_(wake) {}

    // Once, at boot. The register latches, so leaving a cause in it makes every
    // later boot report the same reason - and the clear that prevents that is
    // also what makes a second read a lie: an empty register reads as a power-on
    // (see translate_reset_flags), which is exactly the cause the boot path must not confuse
    // a charger with. So the read happens once and is then only remembered.
    void begin() {
        if (!causes_read_) {
            causes_read_ = true;
            causes_ = read_reset_causes();
            hwinfo_clear_reset_cause();
        }
        supply_monitor_.arm();
    }

    power::ResetCause reset_causes() const override { return causes_; }

    bool supply_monitor_armed() const override { return supply_monitor_.armed(); }
    bool take_supply_warning() override { return supply_monitor_.take_warning(); }

    // The order is core/power's, not this file's: everything here is one step
    // of it, and which step happens when is decided and tested on the host.
    void system_off() override {
        // INFO: fc 05oct26 first, as it outlives the rails; no retention area, no recovery either
        (void)write_boot_magic(
            Dfu::boot_magic_for_system_off(boards::t_echo_plus::kFactoryDfuButton));
        power::power_down(*this);
        // AFTER the walk above, not before: every step of it reconfigures pins,
        // and the last one arms a level-sensed wake. A DETECT that was latched
        // before or during that is a device that comes straight back up and looks
        // like a flat battery instead of a switched-off one.
        //
        // INFO: fc 05aug26 SoftRF-moshe-braner does the same two writes
        // immediately before SYSTEMOFF, with the reason in the source: "Clear
        // LATCH before sleep to prevent stale DETECT assertion"
        // (src/platform/nRF52.cpp:421-446). Zephyr's own poweroff path does not:
        // sys_poweroff() reaches nrf_power_system_off() and touches neither
        // register.
        clear_stale_latches();

        // INFO: fc 05aug26 There is a second way to reach SYSTEM OFF and it is
        // DELIBERATELY NOT IMPLEMENTED. This is the line a reader meets if the
        // sleep current measured after the sequence above disappoints. MB does not enter
        // SYSTEMOFF from here at all: it writes a magic to GPREGRET2, reboots, and
        // enters SYSTEMOFF from the next boot's power-on peripheral state,
        // explicitly because that is the path that reproduced the low drain they
        // measured (src/platform/nRF52.cpp:2300-2332). It is not free - one
        // power-off becomes a reset plus a boot that has to know not to become a
        // device - so it is worth doing against a number and not before. If the
        // measured current with the sequence above is materially worse than the
        // datasheet's SYSTEM OFF figure, that is the next thing to try.
        // | 08nov26 not adopted: board.c raises every rail at boot, so that boot is no low-power
        // state
        sys_poweroff();
    }

    void reboot() override { sys_reboot(SYS_REBOOT_WARM); }

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
                // The same three SoftRF releases, in the same place in its own
                // teardown (nRF52.cpp:2914-2916). CS last: it is what latched
                // the command above.
                disconnect(flash_write_protect_);
                disconnect(flash_hold_);
                disconnect(flash_cs_);
                return;
            case power::PowerDownStep::PanelLinesReleased: release_panel_lines(); return;
            case power::PowerDownStep::SensorBusReleased: release_sensor_bus(); return;
            case power::PowerDownStep::GnssStandby:
                gpio_pin_configure_dt(&gnss_wake_, GPIO_OUTPUT_INACTIVE);
                return;
            case power::PowerDownStep::GnssLinesReleased: release_gnss_lines(); return;
            case power::PowerDownStep::PeripheralRailOff:
                gpio_pin_configure_dt(&peripheral_rail_, GPIO_OUTPUT_INACTIVE);
                k_msleep(power::kRailSettleMs);
                return;
            case power::PowerDownStep::WakePinArmed: arm_wake(); return;
        }
    }

    // DS 13.1.2 through the same SPI the driver uses, because the SX1262 is not
    // a devicetree device and this object cannot reach parts::Sx1262. The radio
    // executor has parked by now (ports::Rf::parked), so this is the only writer
    // left on the bus. MeshCore's order (NRF52Board.cpp:338-359): NRESET pulsed,
    // BUSY awaited, then SetSleep, which DS 13.1.1 accepts only from STDBY.
    void radio_sleep() {
        gpio_pin_configure_dt(&radio_cs_, GPIO_OUTPUT_INACTIVE);
        // INFO: fc 08nov26 an executor abandoned at the ceiling may hold the bus lock, so it gets
        // the reset and no SPI
        if (Rf::abandoned()) quiesce_radio_bus();
        gpio_pin_configure_dt(&radio_reset_, GPIO_OUTPUT_ACTIVE);
        k_busy_wait(parts::sx1262::kResetLowUs);
        gpio_pin_set_dt(&radio_reset_, 0);
        // INFO: fc 08nov26 printk, not LOG_ERR: main.cpp registers the log module after including
        // this
        if (!radio_ready()) {
            printk("power down: SX1262 BUSY did not fall after NRESET, SetSleep not sent\n");
            return;
        }
        if (Rf::abandoned()) {
            printk("power down: radio executor abandoned, SX1262 left in STDBY_RC, no SetSleep\n");
            return;
        }
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

    // While the rail is still up, which is the whole point of where this sits in
    // the order. SoftRF sends the same opcode at the head of its own teardown
    // (nRF52.cpp:2795).
    void flash_deep_power_down() {
        uint8_t frame[1] = {kFlashDeepPowerDown};
        write_spi(flash_bus_, flash_cs_, frame, sizeof(frame));
        k_busy_wait(kFlashDeepPowerDownSettleUs);
    }

    // Manual CS, matching the drivers: the transfer and the chip select are two
    // separate things on this board.
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
        disconnect(epd_sck_);
        disconnect(epd_mosi_);
        disconnect(epd_dc_);
        disconnect(epd_reset_);
        disconnect(epd_busy_);
        disconnect(epd_backlight_);
        disconnect(epd_cs_);
    }

    // INFO: fc 08nov26 GPIO_DISCONNECTED resets only a pin nrfx already holds (pin_uninit), so an
    // input comes first
    static void disconnect(const struct gpio_dt_spec& pin) {
        gpio_pin_configure_dt(&pin, GPIO_INPUT);
        gpio_pin_configure_dt(&pin, GPIO_DISCONNECTED);
    }

    // INFO: fc 08nov26 Zephyr leaves TWIM0 enabled without device PM
    // (i2c_nrfx_twim_common.c:94-100)
    static void release_sensor_bus() {
        irq_disable(DT_IRQN(DT_ALIAS(sensor_i2c)));
        nrf_twim_disable(reinterpret_cast<NRF_TWIM_Type*>(DT_REG_ADDR(DT_ALIAS(sensor_i2c))));
        nrf_gpio_cfg_default(kSensorI2cSdaPin);
        nrf_gpio_cfg_default(kSensorI2cSclPin);
    }

    // INFO: fc 08nov26 the L76K pulls RESET_N up itself (Hardware Design 4.2.2), so a released line
    // never resets it
    void release_gnss_lines() {
        stop_gnss_uart();
        nrf_gpio_cfg_default(kGnssUartTxPin);
        nrf_gpio_cfg_default(kGnssUartRxPin);
        nrf_gpio_cfg_default(kGnssResetPin);
    }

    // INFO: fc 08nov26 nRF52840 PS 6.34.7-8: stop RX and TX, wait for RXTO and TXSTOPPED, then
    // disable the UARTE
    static void stop_gnss_uart() {
        NRF_UARTE_Type* uarte = reinterpret_cast<NRF_UARTE_Type*>(DT_REG_ADDR(DT_ALIAS(gnss_uart)));
        irq_disable(DT_IRQN(DT_ALIAS(gnss_uart)));
        nrf_uarte_shorts_disable(uarte,
                                 NRF_UARTE_SHORT_ENDRX_STARTRX | NRF_UARTE_SHORT_ENDRX_STOPRX);
        nrf_uarte_event_clear(uarte, NRF_UARTE_EVENT_RXTO);
        nrf_uarte_event_clear(uarte, NRF_UARTE_EVENT_TXSTOPPED);
        nrf_uarte_task_trigger(uarte, NRF_UARTE_TASK_STOPRX);
        nrf_uarte_task_trigger(uarte, NRF_UARTE_TASK_STOPTX);
        for (uint32_t waited_us = 0; waited_us < kGnssUartStopTimeoutUs;
             waited_us += kGnssUartStopPollUs) {
            if (nrf_uarte_event_check(uarte, NRF_UARTE_EVENT_RXTO) &&
                nrf_uarte_event_check(uarte, NRF_UARTE_EVENT_TXSTOPPED))
                break;
            k_busy_wait(kGnssUartStopPollUs);
        }
        nrf_uarte_disable(uarte);
    }

    void arm_wake() {
        // INFO: hk 02aug26 the nRF52 wakes from SYSTEM OFF on GPIO SENSE, which
        // is a level detect and not an edge. Zephyr's nRF GPIO driver maps
        // GPIO_INT_LEVEL_ACTIVE onto it (samples/boards/nordic/system_off), so
        // this is the wake pin being armed. The caller must already have waited
        // for the button to be released; arming it under a held button wakes the
        // device the instant the rails drop.
        gpio_pin_configure_dt(&wake_, GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&wake_, GPIO_INT_LEVEL_ACTIVE);
    }

    static constexpr struct spi_config kSpiConfig = {
        .frequency = 8000000U,
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
        .slave = 0,
        .cs = {},
    };

    struct gpio_dt_spec wake_;
    SupplyMonitor supply_monitor_{};
    power::ResetCause causes_{power::ResetCause::None};
    bool causes_read_{false};

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
