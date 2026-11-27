#include "products/skyblip_go/pages/no_tx.h"

#include <algorithm>

#include "products/skyblip_go/pages/frame.h"

namespace skyblip::go {

void draw_no_tx(ui::Canvas& canvas, timing::NoTx reason, MarkLines lines) {
    if (reason == timing::NoTx::None) return;
    const char* word = timing::to_string(reason);
    const bool with_reason = lines == MarkLines::WordAndReason;
    const int width = with_reason ? std::max(text_width(kNoTxWord, 1), text_width(word, 1))
                                  : text_width(kNoTxWord, 1);
    const int height =
        with_reason ? kNoTxReasonY + ui::kCharacterHeight - kNoTxMargin : ui::kCharacterHeight;
    canvas.rectangle(kNoTxMargin - kNoTxPad, kNoTxMargin - kNoTxPad, width + 2 * kNoTxPad,
                     height + 2 * kNoTxPad, true, true);
    canvas.draw_text(kNoTxMargin, kNoTxMargin, kNoTxWord, false, 1);
    if (with_reason) canvas.draw_text(kNoTxMargin, kNoTxReasonY, word, false, 1);
}

}  // namespace skyblip::go
