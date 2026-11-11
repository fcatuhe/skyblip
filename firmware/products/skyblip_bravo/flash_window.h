#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_FLASH_WINDOW_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_FLASH_WINDOW_H

#include "core/events/adsb.h"
#include "core/store/sector.h"
#include "runtime/service.h"

namespace skyblip::bravo {

// INFO: fc 12nov26 the ADS-B queue holds the design load for its drain window, so flash takes half
constexpr uint32_t kFlashPassCeilingMs = events::kAdsbQueueDrainWindowMs / 2;

// TODO: fc 12nov26 kSectorEraseCostMs is the T-Echo's MX25R figure, until #169 times the GD25Q64E
static_assert(store::kSectorEraseCostMs + 2 * store::kSlotWriteCostMs <= kFlashPassCeilingMs,
              "claiming a sector no longer fits inside one pass's flash share");

// INFO: fc 12nov26 Bravo keys no transmitter, so flash waits only for the queue the loop drains
inline bool book_flash_beside_adsb(runtime::Context& context, uint32_t pass_ms, uint32_t cost_ms) {
    const uint32_t now_ms = context.roles.clock.milliseconds();
    const bus::RfState& rf = context.state.rf;
    const uint32_t booked_ms =
        rf.flash_pass_seen && rf.flash_pass_ms == pass_ms ? rf.flash_pass_booked_ms : 0;
    const uint32_t elapsed_ms = now_ms - pass_ms;
    const uint32_t into_pass_ms = elapsed_ms > booked_ms ? elapsed_ms : booked_ms;
    if (into_pass_ms + cost_ms > kFlashPassCeilingMs) return false;
    return context.state.rf.book_flash_window(pass_ms, now_ms, cost_ms);
}

}  // namespace skyblip::bravo

#endif
