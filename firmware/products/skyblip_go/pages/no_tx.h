#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_NO_TX_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_NO_TX_H

#include "core/timing/transmit_gate.h"
#include "products/skyblip_go/glass.h"

namespace skyblip::go {

constexpr const char* kNoTxWord = "NO TX";
constexpr int kNoTxMargin = 4;
constexpr int kNoTxPad = 2;
constexpr int kNoTxStackGap = 2;
constexpr int kNoTxReasonY = kNoTxMargin + ui::kCharacterHeight + kNoTxStackGap;

// INFO: fc 19nov26 top left and reversed, the mirror of the Bravo corner, on any page
void draw_no_tx(ui::Canvas& canvas, timing::NoTx reason);

}  // namespace skyblip::go

#endif
