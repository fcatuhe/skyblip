#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/retention/retention.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/printk.h>

#include <cstdint>

#include "core/indication/lamp.h"
#include "core/power/shutdown.h"
#include "hardware/parts/drv2605/drv2605.h"
#include "hardware/platform/zephyr/meshtracker_x1_lines.h"
#include "runtime/tasks.h"

using namespace skyblip;

namespace {

constexpr uint32_t kSmokeLampMs = 400;
constexpr uint32_t kSmokeOutputMs = 200;
constexpr uint16_t kSmokeTonePitchHz = 2700;
constexpr uint32_t kSmokeReportPeriodMs = 1000;

constexpr int kHapticEnablePin = DT_PROP(DT_GPIO_CTLR(DT_ALIAS(haptic_enable), gpios), port) * 32 +
                                 DT_GPIO_PIN(DT_ALIAS(haptic_enable), gpios);

struct BusPart {
    uint8_t address;
    const char* name;
};

constexpr BusPart kSensorBusParts[] = {
    {0x14, "BMM350"}, {0x5A, "DRV2605"}, {0x6A, "LSM6DSO"}, {0x77, "SPA06"}};
constexpr BusPart kRtcBusParts[] = {{0x32, "YSN8900"}};

platform::zephyr::MeshtrackerX1Lines g_platform;
parts::Drv2605 g_haptic{g_platform.i2c(io::BusId::Sensor), g_platform.gpio(), kHapticEnablePin};
platform::zephyr::I2c g_rtc_bus{DEVICE_DT_GET(DT_ALIAS(rtc_i2c))};
const struct gpio_dt_spec g_button = GPIO_DT_SPEC_GET(DT_ALIAS(button), gpios);
const struct gpio_dt_spec g_sensor_rail = GPIO_DT_SPEC_GET(DT_ALIAS(sensor_rail), gpios);
const struct gpio_dt_spec g_rtc_enable = GPIO_DT_SPEC_GET(DT_ALIAS(rtc_enable), gpios);
const struct device* const g_console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
bool g_haptic_ready = false;

template <size_t N>
void scan(const char* bus_name, io::I2c& bus, const BusPart (&expected)[N]) {
    for (uint8_t address = 0x08; address <= 0x77; address++) {
        if (!bus.write(address, nullptr, 0)) continue;
        const char* name = "?";
        for (const BusPart& part : expected)
            if (part.address == address) name = part.name;
        printk("smoke: %s 0x%02x answered (%s)\n", bus_name, address, name);
    }
}

void hold(uint32_t ms) {
    g_platform.watchdog().feed();
    k_msleep(ms);
}

void show_outputs() {
    for (indication::Lamp lamp :
         {indication::Lamp::Red, indication::Lamp::Green, indication::Lamp::Blue}) {
        g_platform.indicator().show(lamp);
        hold(kSmokeLampMs);
    }
    g_platform.indicator().show(indication::Lamp::None);
    g_platform.annunciator().tone(kSmokeTonePitchHz, ports::Loudness::Loud);
    hold(kSmokeOutputMs);
    g_platform.annunciator().silence();
    g_platform.annunciator().vibrate();
    hold(kSmokeOutputMs);
    g_platform.annunciator().still();
}

void report() {
    uint16_t battery_mv = 0;
    const bool battery_plausible = g_platform.read_battery_mv(battery_mv);
    printk("smoke: battery=%umV%s vbus=%d charging=%d charged=%d button=%d\n", battery_mv,
           battery_plausible ? "" : " (implausible)", g_platform.external_power(),
           g_platform.charging(), g_platform.charged(), g_platform.button_down());
}

// INFO: fc 11nov26 Zephyr's CDC sends before a host opens the port, so the results wait for DTR
bool host_listening() {
    uint32_t dtr = 0;
    return uart_line_ctrl_get(g_console, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr != 0;
}

void announce() {
    printk("smoke: meshtracker_x1 capabilities=%x tag=%04x storage=%d\n",
           static_cast<unsigned>(g_platform.capabilities()), g_platform.bluetooth_tag(),
           g_platform.storage_mounted());
    if (g_haptic_ready)
        printk("smoke: DRV2605 device id %u\n", g_haptic.device_id());
    else
        printk("smoke: no DRV2605 answered\n");
    scan("sensor bus", g_platform.i2c(io::BusId::Sensor), kSensorBusParts);
    scan("rtc bus", g_rtc_bus, kRtcBusParts);
    show_outputs();
}

// INFO: fc 11nov26 radio and NOR stay powered, GNSS enable undriven: Bravo parks them instead
void switch_off() {
    printk("smoke: switching off, release the button\n");
    while (g_platform.button_down()) hold(runtime::kServicePassCeilingMs);
    hold(power::kReleaseSettleMs);
    uint8_t magic = power::kSkipBootloaderMagic;
    const int retained =
        retention_write(DEVICE_DT_GET(DT_NODELABEL(uf2_boot_retention)), 0, &magic, sizeof(magic));
    if (retained != 0) {
        printk("smoke: GPREGRET not written (%d), staying on\n", retained);
        return;
    }
    int armed = gpio_pin_configure_dt(&g_button, GPIO_INPUT);
    if (armed == 0) armed = gpio_pin_interrupt_configure_dt(&g_button, GPIO_INT_LEVEL_ACTIVE);
    if (armed != 0) {
        printk("smoke: button wake not armed (%d), staying on\n", armed);
        return;
    }
    g_platform.indicator().park();
    g_platform.annunciator().silence();
    g_platform.annunciator().still();
    g_haptic.park();
    const int rail = gpio_pin_configure_dt(&g_sensor_rail, GPIO_OUTPUT_INACTIVE);
    if (rail != 0) printk("smoke: sensor rail not lowered (%d)\n", rail);
    sys_poweroff();
}

}  // namespace

// The bench's first image: outputs at boot and per connection, then the inputs, until a long press.
int main(void) {
    if (g_platform.watchdog().arm(runtime::kHardwareWatchdogCeilingMs) != Status::Ok) return -1;
    if (g_platform.begin() != Status::Ok) return -1;
    g_haptic.power_up();
    g_haptic_ready = g_haptic.begin() == Status::Ok;
    if (g_haptic_ready) g_platform.annunciator().attach_haptic(g_haptic);
    const int rtc_powered = gpio_pin_configure_dt(&g_rtc_enable, GPIO_OUTPUT_ACTIVE);
    if (rtc_powered != 0) printk("smoke: RTC enable not raised (%d)\n", rtc_powered);
    show_outputs();

    bool listening = false;
    uint32_t held_ms = 0;
    uint32_t since_report_ms = 0;
    for (;;) {
        hold(runtime::kServicePassCeilingMs);
        const bool was_listening = listening;
        listening = host_listening();
        if (listening && !was_listening) announce();
        held_ms = g_platform.button_down() ? held_ms + runtime::kServicePassCeilingMs : 0;
        if (held_ms >= power::kLongPressHoldMs) switch_off();
        since_report_ms += runtime::kServicePassCeilingMs;
        if (listening && since_report_ms >= kSmokeReportPeriodMs) {
            since_report_ms = 0;
            report();
        }
    }
    return 0;
}
