// INFO: fc 20nov26 nothing sensitive the link asks for happens unless this page names it first
#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_CONFIRM_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_CONFIRM_H

#include <cstdint>

#include "products/common/services/config_service.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/frame.h"

namespace skyblip::go {

struct ConfirmSnapshot {
    const char* title{""};
    const char* detail{""};
    bool trusts_browser{false};
    // INFO: fc 11nov26 one partial refresh a second, the alarm wedge's rate, shows the time left
    uint32_t seconds_left{0};
};

constexpr int kConfirmDetailColumns = kFrameColumns;
constexpr int kConfirmDetailRows = 3;

constexpr const char* kConfirmAllowVerb = "ACCEPT";
constexpr const char* kConfirmCancelVerb = "CANCEL";
constexpr const char* kConfirmCountdownWords = "CANCELS IN ";
constexpr const char* kConfirmTrustsBrowser = "TRUSTS BROWSER";
constexpr int kConfirmCountdownBytes = 24;

// INFO: fc 14nov26 a question the Go asks of itself stands as long as a phone's prompt does
constexpr uint32_t kAskCeilingMs = common::kPromptCeilingMs;

int format_confirm_countdown(char* out, uint32_t seconds_left);
uint32_t seconds_left(uint32_t asked_ms, uint32_t now_ms, uint32_t ceiling_ms);

void draw_confirm(ui::Canvas& canvas, const ConfirmSnapshot& snapshot);
void draw_question_body(ui::Canvas& canvas, Heading heading, uint32_t seconds_left,
                        const BodyLine* lines, int count);

}  // namespace skyblip::go

#endif
