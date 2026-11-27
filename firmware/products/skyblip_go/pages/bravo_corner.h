#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_BRAVO_CORNER_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_BRAVO_CORNER_H

#include <cstdint>
#include <optional>

#include "core/comms/peer.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/no_tx.h"

namespace skyblip::go {

struct BravoCorner {
    comms::BravoState state{comms::BravoState::Unpaired};
    bool cell_low{false};
    std::optional<uint8_t> cell_percent{};
};

struct CornerPatch {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

constexpr int kBravoCornerMargin = kNoTxMargin;
constexpr int kBravoCornerPad = kNoTxPad;
constexpr int kBravoCornerStackGap = kNoTxStackGap;
constexpr int kBravoCornerLines = 3;

int format_cell(char* out, std::optional<uint8_t> percent);
CornerPatch draw_bravo_corner(ui::Canvas& canvas, const BravoCorner& corner,
                              MarkLines marks = MarkLines::WordAndReason);
void reverse_bravo_corner(ui::Canvas& canvas, const CornerPatch& patch);
// INFO: fc 27nov26 top right, the mirror of NO TX: drawn over the page, reversed once Bravo is lost
void draw_bravo_corner_over(ui::Canvas& canvas, const BravoCorner& corner,
                            MarkLines marks = MarkLines::WordAndReason);

}  // namespace skyblip::go

#endif
