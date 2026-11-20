#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RESET_CAUSES_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RESET_CAUSES_H
#if defined(__ZEPHYR__)

#include <hal/nrf_gpio.h>
#include <helpers/nrfx_reset_reason.h>
#include <zephyr/drivers/hwinfo.h>

#include "core/power/reset_reason.h"

namespace skyblip::platform::zephyr {

inline power::ResetCause translate_reset_flags(uint32_t flags) {
    // INFO: hk 02aug26 RESETREAS stays all-zero after a power-on or brown-out (nRF52840 PS v1.8
    // §5.3.3), so empty means power-on
    if (flags == 0) return power::ResetCause::PowerOn;

    power::ResetCause causes = power::ResetCause::None;
    // INFO: fc 20nov26 a set RESET_POR is hwinfo's fold of the VBUS bit, which classify ranks below
    // UsbVbus anyway
    if (flags & RESET_POR) causes |= power::ResetCause::PowerOn;
    if (flags & RESET_PIN) causes |= power::ResetCause::Pin;
    if (flags & RESET_BROWNOUT) causes |= power::ResetCause::Brownout;
    if (flags & RESET_SOFTWARE) causes |= power::ResetCause::Software;
    if (flags & RESET_WATCHDOG) causes |= power::ResetCause::Watchdog;
    if (flags & RESET_CPU_LOCKUP) causes |= power::ResetCause::Lockup;
    if (flags & RESET_LOW_POWER_WAKE) causes |= power::ResetCause::LowPowerWake;
    if (flags & RESET_DEBUG) causes |= power::ResetCause::Debug;
    return causes;
}

inline power::ResetCause read_reset_causes() {
    power::ResetCause causes = power::ResetCause::None;
#if NRFX_RESET_REASON_HAS_VBUS
    // INFO: fc 20nov26 read from the register before hwinfo clears it, since hwinfo folds VBUS into
    // RESET_POR and wake.h needs it
    if (nrfx_reset_reason_get() & NRFX_RESET_REASON_VBUS_MASK) causes |= power::ResetCause::UsbVbus;
#endif
    uint32_t flags = 0;
    if (hwinfo_get_reset_cause(&flags) == 0) causes |= translate_reset_flags(flags);
    return causes;
}

inline void clear_stale_latches() {
#if defined(NRF_GPIO_LATCH_PRESENT)
    uint32_t latched[GPIO_COUNT] = {};
    nrf_gpio_latches_read_and_clear(0, GPIO_COUNT, latched);
#endif
    nrfx_reset_reason_clear(0xFFFFFFFFu);
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
