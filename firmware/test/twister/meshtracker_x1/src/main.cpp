#include <zephyr/kernel.h>

#include "hardware/platform/zephyr/meshtracker_x1.h"
#include "runtime/tasks.h"

using namespace skyblip;

namespace {

platform::zephyr::MeshtrackerX1 g_platform;

}  // namespace

// The X1's composition brings up its own parts and none of the T-Echo's: no unit key, no hub image.
int main(void) {
    if (g_platform.watchdog().arm(runtime::kHardwareWatchdogCeilingMs) != Status::Ok) return -1;
    if (g_platform.begin() != Status::Ok) return -1;
    printk("meshtracker_x1: capabilities=%u tag=%04x\n",
           static_cast<unsigned>(g_platform.capabilities()), g_platform.bluetooth_tag());
    for (;;) {
        g_platform.watchdog().feed();
        k_msleep(runtime::kServicePassCeilingMs);
    }
    return 0;
}
