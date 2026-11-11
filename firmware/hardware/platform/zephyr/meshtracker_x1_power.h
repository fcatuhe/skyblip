#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_MESHTRACKER_X1_POWER_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_MESHTRACKER_X1_POWER_H
#if defined(__ZEPHYR__)

#include <hal/nrf_gpio.h>
#include <hal/nrf_qspi.h>
#include <hal/nrf_twim.h>
#include <hal/nrf_uarte.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/dt-bindings/pinctrl/nrf-pinctrl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>

#include "core/power/shutdown.h"
#include "hardware/platform/zephyr/dfu.h"
#include "hardware/platform/zephyr/reset_causes.h"
#include "hardware/platform/zephyr/supply_monitor.h"
#include "ports/system_power.h"

namespace skyblip::platform::zephyr {

constexpr uint32_t x1_pin_of(uint32_t psel) { return (psel >> NRF_PIN_POS) & NRF_PIN_MSK; }

#define SKYBLIP_X1_PSEL(node, group, index) \
    x1_pin_of(DT_PROP_BY_IDX(DT_CHILD(DT_PINCTRL_BY_NAME(node, default, 0), group), psels, index))

constexpr uint32_t kX1QspiPins[] = {
    SKYBLIP_X1_PSEL(DT_NODELABEL(qspi), group1, 0), SKYBLIP_X1_PSEL(DT_NODELABEL(qspi), group1, 1),
    SKYBLIP_X1_PSEL(DT_NODELABEL(qspi), group1, 2), SKYBLIP_X1_PSEL(DT_NODELABEL(qspi), group1, 3),
    SKYBLIP_X1_PSEL(DT_NODELABEL(qspi), group1, 4), SKYBLIP_X1_PSEL(DT_NODELABEL(qspi), group1, 5)};
constexpr uint32_t kX1I2cPins[] = {SKYBLIP_X1_PSEL(DT_ALIAS(sensor_i2c), group1, 0),
                                   SKYBLIP_X1_PSEL(DT_ALIAS(sensor_i2c), group1, 1),
                                   SKYBLIP_X1_PSEL(DT_ALIAS(rtc_i2c), group1, 0),
                                   SKYBLIP_X1_PSEL(DT_ALIAS(rtc_i2c), group1, 1)};
constexpr uint32_t kX1GnssUartPins[] = {SKYBLIP_X1_PSEL(DT_ALIAS(gnss_uart), group1, 0),
                                        SKYBLIP_X1_PSEL(DT_ALIAS(gnss_uart), group2, 0)};

#undef SKYBLIP_X1_PSEL

// INFO: fc 12nov26 core/power's order, walked on the X1's lines:
// boards/seeed/meshtracker_x1/README.md
class MeshtrackerX1Power : public ports::SystemPower, private power::PowerDownSink {
   public:
    // INFO: fc 12nov26 read once and cleared: a second read of RESETREAS reads as a power-on
    void read_causes() {
        if (causes_read_) return;
        causes_read_ = true;
        causes_ = read_reset_causes();
        hwinfo_clear_reset_cause();
    }

    void arm_supply_monitor() { supply_monitor_.arm(); }

    power::ResetCause reset_causes() const override { return causes_; }
    bool supply_monitor_armed() const override { return supply_monitor_.armed(); }
    bool take_supply_warning() override { return supply_monitor_.take_warning(); }

    void system_off() override {
        (void)write_boot_magic(Dfu::boot_magic_for_system_off());
        power::power_down(*this);
        clear_stale_latches();
        sys_poweroff();
    }

    void reboot() override { sys_reboot(SYS_REBOOT_WARM); }

   private:
    void perform(power::PowerDownStep step) override {
        switch (step) {
            // INFO: fc 12nov26 the receiver or a refused boot already parked the LR2021
            case power::PowerDownStep::RadioSleep: return;
            // TODO: fc 12nov26 the NOR loses its rail below instead of a DPD, until #169 measures
            // off current
            case power::PowerDownStep::ExternalFlashDeepPowerDown: return;
            case power::PowerDownStep::ExternalFlashLinesReleased: release_flash_lines(); return;
            case power::PowerDownStep::PanelLinesReleased: return;
            case power::PowerDownStep::SensorBusReleased: release_i2c_buses(); return;
            // TODO: fc 12nov26 SoftRF's GNSS enable, kept low through SYSTEM OFF, unverified until
            // #169
            case power::PowerDownStep::GnssStandby: lower(gnss_enable_); return;
            case power::PowerDownStep::GnssLinesReleased: release_gnss_lines(); return;
            // TODO: fc 11nov26 P1.07 and P0.09 as SoftRF and Seeed name them, unverified until #169
            case power::PowerDownStep::PeripheralRailOff:
                lower(sensor_rail_);
                lower(rtc_enable_);
                lower(flash_enable_);
                k_msleep(power::kRailSettleMs);
                return;
            case power::PowerDownStep::WakePinArmed: arm_wake(); return;
        }
    }

    static void lower(const struct gpio_dt_spec& pin) {
        (void)gpio_pin_configure_dt(&pin, GPIO_OUTPUT_INACTIVE);
    }

    static void release(const struct gpio_dt_spec& pin) {
        (void)gpio_pin_configure_dt(&pin, GPIO_INPUT);
        (void)gpio_pin_configure_dt(&pin, GPIO_DISCONNECTED);
    }

    // INFO: fc 12nov26 QSPI owns its six pads while enabled, so it goes before the pads are let go
    static void release_flash_lines() {
        irq_disable(DT_IRQN(DT_NODELABEL(qspi)));
        nrf_qspi_disable(NRF_QSPI);
        for (uint32_t pin : kX1QspiPins) nrf_gpio_cfg_default(pin);
    }

    // INFO: fc 12nov26 Zephyr leaves both TWIM enabled without device PM
    static void release_i2c_buses() {
        irq_disable(DT_IRQN(DT_ALIAS(sensor_i2c)));
        nrf_twim_disable(reinterpret_cast<NRF_TWIM_Type*>(DT_REG_ADDR(DT_ALIAS(sensor_i2c))));
        irq_disable(DT_IRQN(DT_ALIAS(rtc_i2c)));
        nrf_twim_disable(reinterpret_cast<NRF_TWIM_Type*>(DT_REG_ADDR(DT_ALIAS(rtc_i2c))));
        for (uint32_t pin : kX1I2cPins) nrf_gpio_cfg_default(pin);
    }

    void release_gnss_lines() {
        NRF_UARTE_Type* uarte = reinterpret_cast<NRF_UARTE_Type*>(DT_REG_ADDR(DT_ALIAS(gnss_uart)));
        irq_disable(DT_IRQN(DT_ALIAS(gnss_uart)));
        nrf_uarte_task_trigger(uarte, NRF_UARTE_TASK_STOPRX);
        nrf_uarte_task_trigger(uarte, NRF_UARTE_TASK_STOPTX);
        nrf_uarte_disable(uarte);
        for (uint32_t pin : kX1GnssUartPins) nrf_gpio_cfg_default(pin);
        release(gnss_reset_);
        release(gnss_sleep_);
        release(gnss_rtc_wake_);
    }

    // INFO: fc 12nov26 SENSE is a level, and the X1's button reads high when pressed
    void arm_wake() {
        (void)gpio_pin_configure_dt(&button_, GPIO_INPUT);
        (void)gpio_pin_interrupt_configure_dt(&button_, GPIO_INT_LEVEL_ACTIVE);
    }

    SupplyMonitor supply_monitor_{};
    power::ResetCause causes_{power::ResetCause::None};
    bool causes_read_{false};
    struct gpio_dt_spec button_ = GPIO_DT_SPEC_GET(DT_ALIAS(button), gpios);
    struct gpio_dt_spec sensor_rail_ = GPIO_DT_SPEC_GET(DT_ALIAS(sensor_rail), gpios);
    struct gpio_dt_spec flash_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(flash_enable), gpios);
    struct gpio_dt_spec rtc_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(rtc_enable), gpios);
    struct gpio_dt_spec gnss_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_enable), gpios);
    struct gpio_dt_spec gnss_reset_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_reset), gpios);
    struct gpio_dt_spec gnss_sleep_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_sleep), gpios);
    struct gpio_dt_spec gnss_rtc_wake_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_rtc_wake), gpios);
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
