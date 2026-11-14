#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_BRAVO_LOG_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_BRAVO_LOG_H

#include <cstdint>

#include "core/comms/peer.h"
#include "products/skyblip_go/bravo_log.h"
#include "products/skyblip_go/glass.h"

namespace skyblip::go {

constexpr int kBravoLogRows = BravoLog::kMaxEntries;

struct BravoLogSnapshot {
    comms::BravoState state{comms::BravoState::Unpaired};
    uint16_t tag{0};
    const comms::PeerHealth* health{nullptr};
    uint32_t folded{0};
    uint32_t refused{0};
    uint32_t unplaced{0};
    int tracked{0};
    const BravoLog* log{nullptr};
};

void draw_bravo_log(ui::Canvas& canvas, const BravoLogSnapshot& snapshot);

}  // namespace skyblip::go

#endif
