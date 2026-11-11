#include <zephyr/kernel.h>

#include "hardware/parts/ag3335/ag3335.h"
#include "hardware/platform/zephyr/ag3335.h"

using namespace skyblip;

namespace {

platform::zephyr::Ag3335Wiring g_wiring;

}  // namespace

int main(void) {
    if (g_wiring.begin() != Status::Ok) return -1;
    parts::Ag3335 gnss(g_wiring.uart());
    for (;;) {
        const uint32_t now_ms = k_uptime_get_32();
        gnss.service(now_ms);
        gnss.poll();
        k_msleep(10);
    }
    return 0;
}
