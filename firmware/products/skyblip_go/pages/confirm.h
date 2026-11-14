// The authorisation prompt, on the glass. Nothing sensitive the companion link
// asks for happens without this page having been read: it names the operation,
// says in a sentence what confirming it will do, and states the gesture that
// allows it next to the gesture that refuses it. A pilot who cannot tell from
// the panel what they are about to authorise has not authorised anything.
#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_CONFIRM_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_CONFIRM_H

#include <cstdint>

#include "core/comms/config.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/frame.h"

namespace skyblip::go {

struct ConfirmSnapshot {
    const char* title{""};
    const char* detail{""};
    // INFO: fc 11nov26 one partial refresh a second, the alarm wedge's rate, shows the time left
    uint32_t seconds_left{0};
};

constexpr int kConfirmDetailColumns = kFrameColumns;
constexpr int kConfirmDetailRows = 3;

constexpr const char* kConfirmAllowVerb = "ACCEPT";
constexpr const char* kConfirmCancelVerb = "CANCEL";
constexpr const char* kConfirmCountdownWords = "CANCELS IN ";
constexpr int kConfirmCountdownBytes = 24;

// INFO: fc 14nov26 a question the Go asks of itself stands as long as a phone's prompt does
constexpr uint32_t kAskCeilingMs = comms::kPromptCeilingMs;

int format_confirm_countdown(char* out, uint32_t seconds_left);
uint32_t seconds_left(uint32_t asked_ms, uint32_t now_ms, uint32_t ceiling_ms);

void draw_confirm(ui::Canvas& canvas, const ConfirmSnapshot& snapshot);
void draw_question_body(ui::Canvas& canvas, Heading heading, uint32_t seconds_left,
                        const BodyLine* lines, int count);

}  // namespace skyblip::go

#endif
