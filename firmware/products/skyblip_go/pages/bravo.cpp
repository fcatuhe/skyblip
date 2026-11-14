#include "products/skyblip_go/pages/bravo.h"

#include "core/comms/link_sessions.h"
#include "core/comms/peer.h"
#include "core/util/format.h"
#include "products/skyblip_go/pages/menu.h"

namespace skyblip::go {

namespace {

constexpr int kHeaderY = 3;
constexpr int kHeaderRuleY = 21;
constexpr int kTagDigits = 4;
constexpr int kPairedTagY = 46;
constexpr int kPairedStateY = 92;
constexpr int kReasonY = 52;
constexpr int kReasonLineGap = 22;
constexpr int kDetailY = 104;
constexpr int kNoticeDetailGap = 14;
constexpr int kEmptyListY = 76;
constexpr int kEmptyListDetailY = 104;
constexpr int kDetailBytes = kGlassWidth / ui::kCellWidth + 1;

void header(ui::Canvas& canvas, const char* title) {
    canvas.clear(true);
    canvas.draw_text(kMenuLeftX - 2, kHeaderY, title, true, kMenuScale);
    canvas.horizontal_line(kMenuLeftX - 2, kHeaderRuleY, kGlassWidth - 2 * (kMenuLeftX - 2), true);
}

void centred(ui::Canvas& canvas, int y, const char* text, int scale) {
    canvas.draw_text(centred_x(text, scale), y, text, true, scale);
}

void tag(ui::Canvas& canvas, int y, uint16_t value) {
    char text[kTagDigits + 1];
    text[format_hex(text, value, kTagDigits)] = 0;
    centred(canvas, y, text, kCallsignScale);
}

void hints(ui::Canvas& canvas, const char* pad, const char* button) {
    centred(canvas, kCallsignPadHintY, pad, kCallsignHintScale);
    centred(canvas, kCallsignButtonHintY, button, kCallsignHintScale);
}

void row(ui::Canvas& canvas, int line, uint16_t value, bool focused) {
    const int top = menu_line_top(line);
    const int y = menu_line_text_y(line);
    if (focused) canvas.rectangle(2, top, kGlassWidth - 4, kMenuBarHeight, true, /*fill=*/true);
    char text[kTagDigits + 1];
    text[format_hex(text, value, kTagDigits)] = 0;
    canvas.draw_text(kMenuLeftX, y, "BRAVO", !focused, kMenuScale);
    canvas.draw_text(kMenuRightX - kTagDigits * kMenuCellWidth, y, text, !focused, kMenuScale);
}

const char* link_state(BravoLink link) {
    switch (link) {
        case BravoLink::Up: return "CONNECTED";
        case BravoLink::Narrow: return "LINK TOO SMALL";
        case BravoLink::Unsupported: return "UPDATE NECESSARY";
        case BravoLink::Down: break;
    }
    return "NOT CONNECTED";
}

void draw_paired(ui::Canvas& canvas, const BravoSnapshot& snapshot) {
    if (snapshot.unpairing) {
        header(canvas, "UNPAIR BRAVO");
        tag(canvas, kCallsignTextY, snapshot.view.paired_tag);
        hints(canvas, "PAD KEEPS", "BUTTON UNPAIRS");
        return;
    }
    header(canvas, "BRAVO");
    tag(canvas, kPairedTagY, snapshot.view.paired_tag);
    centred(canvas, kPairedStateY, link_state(snapshot.view.link), kMenuScale);
    hints(canvas, "PAD BACK", "BUTTON UNPAIRS");
}

void draw_list(ui::Canvas& canvas, const BravoSnapshot& snapshot) {
    if (snapshot.confirming) {
        header(canvas, "PAIR BRAVO");
        tag(canvas, kCallsignTextY, snapshot.view.found_tags[snapshot.focus]);
        hints(canvas, "PAD CANCELS", "BUTTON PAIRS");
        return;
    }
    header(canvas, "BRAVO");
    if (snapshot.view.found_count == 0) {
        centred(canvas, kEmptyListY, "NO BRAVO FOUND", kMenuScale);
        centred(canvas, kEmptyListDetailY, "START A BRAVO NEAR THIS GO.", 1);
    }
    for (int i = 0; i < snapshot.view.found_count && i < kMenuLines; i++)
        row(canvas, i, snapshot.view.found_tags[i], i == snapshot.focus);
    centred(canvas, kMenuHintY, "PAD MOVES    BUTTON PAIRS", 1);
}

void draw_pairing(ui::Canvas& canvas, const BravoSnapshot& snapshot) {
    header(canvas, "PAIRING");
    tag(canvas, kCallsignTextY, snapshot.view.pairing_tag);
    if (snapshot.view.step == BravoStep::Connecting) {
        centred(canvas, kCallsignPadHintY, "WAIT", kCallsignHintScale);
    } else {
        centred(canvas, kCallsignPadHintY, "PUSH THE", kCallsignHintScale);
        centred(canvas, kCallsignButtonHintY, "BUTTON ON BRAVO.", kCallsignHintScale);
    }
    centred(canvas, kMenuHintY, "PAD CANCELS", 1);
}

struct Reason {
    const char* first;
    const char* second;
};

Reason reason_of(BravoRefusal refusal) {
    switch (refusal) {
        case BravoRefusal::PhonesConnected: return {"PHONES", "CONNECTED"};
        case BravoRefusal::NarrowLink: return {"LINK TOO", "SMALL"};
        case BravoRefusal::NoAnswer: return {"NO DATA", "FROM BRAVO"};
        case BravoRefusal::UnknownProtocol: return {"UNKNOWN", "PROTOCOL"};
        case BravoRefusal::LinkLost:
        case BravoRefusal::None: break;
    }
    return {"NO LINK", ""};
}

int reason_detail(char* out, BravoRefusal refusal) {
    switch (refusal) {
        case BravoRefusal::PhonesConnected: return format_string(out, "DISCONNECT ONE TO PAIR.");
        case BravoRefusal::NarrowLink: {
            int written = format_string(out, "LINK IS LESS THAN ");
            written += format_unsigned(out + written, comms::kPeerPayloadFloorBytes);
            return written + format_string(out + written, " BYTES.");
        }
        case BravoRefusal::NoAnswer: return format_string(out, "PUSH THE BUTTON ON BRAVO.");
        case BravoRefusal::UnknownProtocol: return format_string(out, "UPDATE THE GO OR BRAVO.");
        case BravoRefusal::LinkLost:
        case BravoRefusal::None: break;
    }
    return format_string(out, "BRAVO IS OFF OR OUT OF RANGE.");
}

void draw_refused(ui::Canvas& canvas, const BravoSnapshot& snapshot) {
    header(canvas, "NOT PAIRED");
    const BravoRefusal refusal = snapshot.view.refusal;
    const Reason reason = reason_of(refusal);
    char first[kMenuValueBytes];
    int written = 0;
    if (refusal == BravoRefusal::PhonesConnected) {
        written = format_unsigned(first, static_cast<uint32_t>(comms::LinkSessions::kMaxSessions));
        written += format_string(first + written, " ");
    }
    written += format_string(first + written, reason.first);
    first[written] = 0;
    centred(canvas, kReasonY, first, kMenuScale);
    centred(canvas, kReasonY + kReasonLineGap, reason.second, kMenuScale);
    char detail[kDetailBytes];
    detail[reason_detail(detail, refusal)] = 0;
    centred(canvas, kDetailY, detail, 1);
    centred(
        canvas, kMenuHintY,
        refusal == BravoRefusal::PhonesConnected ? "PAD BACK" : "PAD BACK    BUTTON TRIES AGAIN",
        1);
}

}  // namespace

void draw_bravo(ui::Canvas& canvas, const BravoSnapshot& snapshot) {
    if (snapshot.view.paired) {
        draw_paired(canvas, snapshot);
        return;
    }
    switch (snapshot.view.step) {
        case BravoStep::Connecting:
        case BravoStep::Waiting: draw_pairing(canvas, snapshot); return;
        case BravoStep::Refused: draw_refused(canvas, snapshot); return;
        case BravoStep::Scanning:
        case BravoStep::Idle: break;
    }
    draw_list(canvas, snapshot);
}

void draw_own_icao_notice(ui::Canvas& canvas) {
    header(canvas, "OWN ICAO");
    centred(canvas, kReasonY, "NOT SET ON", kMenuScale);
    centred(canvas, kReasonY + kReasonLineGap, "GO OR BRAVO", kMenuScale);
    centred(canvas, kDetailY, "OWN TRANSPONDER SHOWS AS TRAFFIC", 1);
    centred(canvas, kDetailY + kNoticeDetailGap, "SET IT IN SETTINGS, AIRCRAFT.", 1);
    centred(canvas, kMenuHintY, "PAD OR BUTTON CLOSES", 1);
}

// INFO: fc 11nov26 Bravo holds no pin for this Go, a pairing cut short or a Bravo paired elsewhere
void draw_bravo_forgot_notice(ui::Canvas& canvas) {
    header(canvas, "BRAVO");
    centred(canvas, kReasonY, "NOT PAIRED", kMenuScale);
    centred(canvas, kReasonY + kReasonLineGap, "ON BRAVO", kMenuScale);
    centred(canvas, kDetailY, "THE GO REMOVED IT TOO.", 1);
    centred(canvas, kDetailY + kNoticeDetailGap, "PAIR AGAIN IN SETTINGS.", 1);
    centred(canvas, kMenuHintY, "PAD OR BUTTON CLOSES", 1);
}

void BravoPage::open(uint32_t now_ms) {
    idle_since_ms_ = now_ms;
    focus_ = 0;
    confirming_ = false;
    unpairing_ = false;
}

BravoAction BravoPage::pad(const BravoView& view, uint32_t now_ms) {
    idle_since_ms_ = now_ms;
    if (view.paired) return pad_paired();
    switch (view.step) {
        case BravoStep::Connecting:
        case BravoStep::Waiting: return BravoAction::Cancel;
        case BravoStep::Scanning: return pad_scanning(view);
        case BravoStep::Refused:
        case BravoStep::Idle: break;
    }
    return BravoAction::Leave;
}

BravoAction BravoPage::button(const BravoView& view, uint32_t now_ms) {
    idle_since_ms_ = now_ms;
    if (view.paired) return button_paired();
    switch (view.step) {
        case BravoStep::Scanning: return button_scanning(view);
        case BravoStep::Refused:
            return view.refusal == BravoRefusal::PhonesConnected ? BravoAction::None
                                                                 : BravoAction::Retry;
        case BravoStep::Connecting:
        case BravoStep::Waiting:
        case BravoStep::Idle: break;
    }
    return BravoAction::None;
}

// INFO: fc 11nov26 a pairing under way has its own ceilings, so only a page a pilot reads idles out
BravoAction BravoPage::idle(const BravoView& view, uint32_t now_ms) const {
    const bool waiting =
        !view.paired && (view.step == BravoStep::Connecting || view.step == BravoStep::Waiting);
    if (waiting || now_ms - idle_since_ms_ < MenuEditor::kIdleReturnHoldMs)
        return BravoAction::None;
    return BravoAction::Leave;
}

BravoAction BravoPage::pad_paired() {
    if (!unpairing_) return BravoAction::Leave;
    unpairing_ = false;
    return BravoAction::Redraw;
}

BravoAction BravoPage::button_paired() {
    if (!unpairing_) {
        unpairing_ = true;
        return BravoAction::Redraw;
    }
    unpairing_ = false;
    return BravoAction::Unpair;
}

BravoAction BravoPage::pad_scanning(const BravoView& view) {
    if (confirming_) {
        confirming_ = false;
        return BravoAction::Redraw;
    }
    if (focus_ + 1 >= view.found_count) return BravoAction::Leave;
    focus_++;
    return BravoAction::Redraw;
}

BravoAction BravoPage::button_scanning(const BravoView& view) {
    if (focus_ >= view.found_count) return BravoAction::None;
    if (!confirming_) {
        confirming_ = true;
        return BravoAction::Redraw;
    }
    confirming_ = false;
    return BravoAction::Pair;
}

}  // namespace skyblip::go
