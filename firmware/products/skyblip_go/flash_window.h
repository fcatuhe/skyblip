#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_FLASH_WINDOW_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_FLASH_WINDOW_H

#include "core/store/sector.h"
#include "core/timing/slot.h"
#include "runtime/service.h"

namespace skyblip::go {

static_assert(store::kSectorEraseCostMs + 2 * store::kSlotWriteCostMs +
                      static_cast<uint32_t>(timing::kJitterGuardMs) <
                  static_cast<uint32_t>(timing::kUplinkRxEndMs - timing::kUplinkRxStartMs),
              "claiming a sector no longer fits inside the narrowest dwell the map offers");

// INFO: fc 10nov26 flash waits for a published receive dwell, never where own-ship may key the PA
inline bool book_flash_in_dwell(runtime::Context& context, uint32_t pass_ms, uint32_t cost_ms) {
    return context.state.rf.book_flash_window(pass_ms, context.roles.clock.milliseconds(), cost_ms);
}

}  // namespace skyblip::go

#endif
