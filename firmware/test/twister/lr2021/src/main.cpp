// The LR2021 executor compiled against Zephyr, over a bus nobody wired yet: #170 and #176 give it
// SPI.
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "core/bus/bus.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "hardware/platform/zephyr/adsb_receiver.h"
#include "hardware/platform/zephyr/meshtracker_x1.h"
#include "runtime/tasks.h"

using namespace skyblip;

namespace {

class UnwiredSpi : public io::Spi {
   public:
    void transfer(const uint8_t*, uint8_t* rx, size_t bytes) override {
        for (size_t i = 0; rx != nullptr && i < bytes; i++) rx[i] = 0;
    }
    void select(bool) override {}
};

// TODO: fc 11nov26 pins from SoftRF Seeed_X1.h, unverified until #169 benches a unit
constexpr int kBusyPin = 7;
constexpr int kResetPin = 32 + 10;
constexpr int kIrqPin = 32 + 1;
constexpr uint8_t kIrqDio = 8;

platform::zephyr::MeshtrackerX1 g_platform;
UnwiredSpi g_spi;
bus::AdsbQueue g_frames;
parts::Lr2021 g_radio(g_spi, g_platform.gpio(), g_platform.delay(), kBusyPin, kResetPin, kIrqPin,
                      kIrqDio);
const struct gpio_dt_spec g_irq = {DEVICE_DT_GET(DT_NODELABEL(gpio1)), 1, GPIO_ACTIVE_HIGH};
platform::zephyr::AdsbReceiver g_receiver(g_radio, g_platform.clock(), g_irq, g_frames);

}  // namespace

int main(void) {
    if (g_platform.begin() != Status::Ok) return -1;
    const Status status = g_receiver.begin();
    printk("lr2021: begin=%s\n", to_string(status));
    for (;;) {
        events::AdsbFrame frame{};
        while (g_frames.pop(frame)) printk("lr2021: frame rssi=%d\n", frame.rssi_dbm);
        const ports::AdsbReceiverStatistics statistics = g_receiver.statistics();
        printk("lr2021: frames=%u dropped=%u\n", static_cast<unsigned>(statistics.frames),
               static_cast<unsigned>(statistics.dropped));
        k_msleep(1000);
    }
    return 0;
}
