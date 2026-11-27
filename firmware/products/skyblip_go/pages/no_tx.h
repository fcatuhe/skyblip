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
// INFO: fc 27nov26 the first row a mark in one line leaves untouched
constexpr int kOneLineMarkEndY = kNoTxMargin + ui::kCharacterHeight + kNoTxPad;

// INFO: fc 27nov26 the lines a top mark takes: its word and reason, or its word alone
enum class MarkLines : uint8_t { WordAndReason, WordOnly };

// INFO: fc 19nov26 top left and reversed, the mirror of the Bravo corner, on any page
void draw_no_tx(ui::Canvas& canvas, timing::NoTx reason,
                MarkLines lines = MarkLines::WordAndReason);

}  // namespace skyblip::go

#endif
