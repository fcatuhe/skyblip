// The LR2021 executor compiled against Zephyr and the X1's radio lines, over an SPI #176 wires.
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "core/bus/bus.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "hardware/platform/zephyr/adsb_receiver.h"
#include "hardware/platform/zephyr/clock.h"
#include "hardware/platform/zephyr/io.h"

using namespace skyblip;

namespace {

class UnwiredSpi : public io::Spi {
   public:
    void transfer(const uint8_t*, uint8_t* rx, size_t bytes) override {
        for (size_t i = 0; rx != nullptr && i < bytes; i++) rx[i] = 0;
    }
    void select(bool) override {}
};

constexpr int flat_pin(bool on_gpio1, int pin) { return (on_gpio1 ? 32 : 0) + pin; }

#define RADIO_PIN(label)                                                                  \
    flat_pin(DT_SAME_NODE(DT_GPIO_CTLR(DT_NODELABEL(label), gpios), DT_NODELABEL(gpio1)), \
             DT_GPIO_PIN(DT_NODELABEL(label), gpios))

constexpr int kBusyPin = RADIO_PIN(radio_busy_gpio);
constexpr int kResetPin = RADIO_PIN(radio_reset_gpio);
constexpr int kIrqPin = RADIO_PIN(radio_irq_gpio);
// TODO: fc 11nov26 DIO8 from SoftRF radiolib.cpp:2132, unverified until #169 benches a unit
constexpr uint8_t kIrqDio = 8;

platform::zephyr::Gpio g_gpio{DEVICE_DT_GET(DT_NODELABEL(gpio0)),
                              DEVICE_DT_GET(DT_NODELABEL(gpio1))};
platform::zephyr::Delay g_delay;
platform::zephyr::Clock g_clock;
UnwiredSpi g_spi;
bus::AdsbQueue g_frames;
parts::Lr2021 g_radio(g_spi, g_gpio, g_delay, kBusyPin, kResetPin, kIrqPin, kIrqDio);
const struct gpio_dt_spec g_irq = GPIO_DT_SPEC_GET(DT_NODELABEL(radio_irq_gpio), gpios);
platform::zephyr::AdsbReceiver g_receiver(g_radio, g_clock, g_irq, g_frames);

}  // namespace

int main(void) {
    if (g_receiver.begin() != Status::Ok) return -1;
    for (;;) {
        events::AdsbFrame frame{};
        while (g_frames.pop(frame)) {
        }
        (void)g_receiver.statistics();
        k_msleep(1000);
    }
    return 0;
}
