#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RESET_CAUSES_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RESET_CAUSES_H
#if defined(__ZEPHYR__)

#include <hal/nrf_gpio.h>
#include <helpers/nrfx_reset_reason.h>
#include <zephyr/drivers/hwinfo.h>

#include "core/power/reset_reason.h"

namespace skyblip::platform::zephyr {

inline power::ResetCause translate_reset_flags(uint32_t flags) {
    // INFO: hk 02aug26 the nRF52 leaves RESETREAS all-zero after a power-on
    // or brown-out reset: it latches only the other seven causes (nRF52840
    // PS v1.8 §5.3.3). An empty register is therefore the power-on case, not
    // an unread one.
    //
    // The other half of that: RESET_POR coming back SET is not a power-on at
    // all on this SoC - it is the VBUS bit, which hwinfo_nrf.c folds into
    // RESET_POR. read_reset_causes() has already reported that as UsbVbus from the
    // register itself, and core/power::classify ranks the specific cause
    // above PowerOn, so the extra bit costs nothing and is left as the driver
    // reported it rather than second-guessed here.
    if (flags == 0) return power::ResetCause::PowerOn;

    power::ResetCause causes = power::ResetCause::None;
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
    // Read straight from the register, because Zephyr cannot carry this one:
    // drivers/hwinfo/hwinfo_nrf.c maps NRFX_RESET_REASON_VBUS_MASK onto
    // RESET_POR, so through hwinfo alone a charger plugged into a sleeping
    // device is indistinguishable from a cell being connected for the first
    // time - and core/power/wake.h refuses a boot on the difference. Done
    // before hwinfo_clear_reset_cause(), which clears every bit.
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
