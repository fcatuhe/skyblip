// The authorisation prompt, on the glass. Nothing sensitive the companion link
// asks for happens without this page having been read: it names the operation,
// says in a sentence what confirming it will do, and states the gesture that
// allows it next to the gesture that refuses it. A pilot who cannot tell from
// the panel what they are about to authorise has not authorised anything.
#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_CONFIRM_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_CONFIRM_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/frame.h"

namespace skyblip::go {

struct ConfirmSnapshot {
    const char* title{""};
    const char* detail{""};
    // INFO: fc 11nov26 one partial refresh a second, the alarm wedge's rate, shows the time left
    uint32_t seconds_left{0};
};

// The grid, exported so a test reads a line back off the glass instead of
// counting ink: "the page says POWER OFF" is the claim that matters.
constexpr int kConfirmDetailY = kInterruptRuleY + kBodyBelowRule;
constexpr int kConfirmDetailColumns = kFrameColumns;
constexpr int kConfirmDetailRows = 3;
constexpr int kConfirmCancelRows = 3;

constexpr int confirm_detail_y(int row) { return kConfirmDetailY + row * line_pitch(kFrameScale); }
constexpr int kConfirmCancelY = confirm_detail_y(kConfirmDetailRows) + kParagraphGap;
constexpr int confirm_cancel_y(int row) { return kConfirmCancelY + row * line_pitch(kFrameScale); }

static_assert(confirm_cancel_y(kConfirmCancelRows) - 4 <= kBodyBottom,
              "the cancel lines stand clear of the keys");

constexpr const char* kConfirmAllowVerb = "ACCEPT";
constexpr const char* kConfirmCancelLine = "TO CANCEL, PUSH";

void draw_confirm(ui::Canvas& canvas, const ConfirmSnapshot& snapshot);

}  // namespace skyblip::go

#endif
