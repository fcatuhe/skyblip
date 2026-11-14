#include "products/skyblip_go/pages/bravo.h"

#include "core/comms/link_sessions.h"
#include "core/comms/peer.h"
#include "core/util/format.h"
#include "products/skyblip_go/pages/menu.h"

namespace skyblip::go {

namespace {

constexpr int kTagDigits = 4;
constexpr int kTagScale = 4;
constexpr int kTagY = 70;
constexpr int kPairedTagY = 56;
constexpr int kStepY = 104;
constexpr int kReasonY = 46;
constexpr int kEmptyListY = 70;
constexpr int kDetailRows = 3;
constexpr int kDetailBytes = kGlassWidth / ui::kCellWidth + 1;

constexpr const char* kOwnIcaoNoticeLines[] = {"YOUR TRANSPONDER", "SHOWS AS", "TRAFFIC."};
constexpr const char* kOwnIcaoNoticeFix[] = {"SET IT IN", "SETTINGS,", "AIRCRAFT."};
constexpr const char* kForgotNoticeLines[] = {"BRAVO IS NOT", "PAIRED. THE GO", "REMOVED IT TOO."};
constexpr const char* kForgotNoticeFix[] = {"PAIR AGAIN IN", "SETTINGS."};
constexpr const char* kEmptyListLines[] = {"START A BRAVO", "NEAR THIS GO."};
constexpr const char* kPushBravoLines[] = {"PUSH THE BUTTON", "ON BRAVO."};

void tag(ui::Canvas& canvas, int y, uint16_t value) {
    char text[kTagDigits + 1];
    text[format_hex(text, value, kTagDigits)] = 0;
    draw_centred_line(canvas, y, text, kTagScale);
}

void row(ui::Canvas& canvas, int line, uint16_t value, bool focused) {
    const int top = menu_line_top(line);
    const int y = menu_line_text_y(line);
    if (focused)
        canvas.rounded_rectangle(2, top, kGlassWidth - 4, kMenuBarHeight, kMenuBarRadius, true,
                                 /*fill=*/true);
    char text[kTagDigits + 1];
    text[format_hex(text, value, kTagDigits)] = 0;
    canvas.draw_text(kMenuLeftX, y, "BRAVO", !focused, kMenuScale);
    canvas.draw_text(kMenuRightX - kTagDigits * kMenuCellWidth, y, text, !focused, kMenuScale);
}

void wrapped(ui::Canvas& canvas, int y, const char* text) {
    char line[kFrameColumns + 1];
    int at = 0;
    for (int i = 0; i < kDetailRows; i++) {
        at = take_line(text, at, line, kFrameColumns);
        if (line[0] == 0) return;
        y = draw_centred_line(canvas, y, line);
    }
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
        draw_heading(canvas, "UNPAIR BRAVO", Heading::Opened);
        tag(canvas, kTagY, snapshot.view.paired_tag);
        draw_keys(canvas, {{Press::Once, "UNPAIR"}, "KEEP"});
        return;
    }
    draw_heading(canvas, "BRAVO", Heading::Opened);
    tag(canvas, kPairedTagY, snapshot.view.paired_tag);
    draw_centred_line(canvas, kStepY, link_state(snapshot.view.link));
    draw_keys(canvas, {{Press::Once, "UNPAIR"}, "BACK"});
}

void draw_list(ui::Canvas& canvas, const BravoSnapshot& snapshot) {
    if (snapshot.confirming) {
        draw_heading(canvas, "PAIR BRAVO", Heading::Opened);
        tag(canvas, kTagY, snapshot.view.found_tags[snapshot.focus]);
        draw_keys(canvas, {{Press::Once, "PAIR"}, "CANCEL"});
        return;
    }
    draw_heading(canvas, "BRAVO", Heading::Opened);
    const int found = snapshot.view.found_count;
    if (found == 0) {
        const int y = draw_centred_line(canvas, kEmptyListY, "NO BRAVO FOUND") + kParagraphGap;
        draw_centred_lines(canvas, y, kEmptyListLines, 2);
        draw_keys(canvas, {{}, "BACK"});
        return;
    }
    for (int i = 0; i < found && i < kMenuLines; i++)
        row(canvas, i, snapshot.view.found_tags[i], i == snapshot.focus);
    draw_keys(canvas, {{Press::Once, "PAIR"}, snapshot.focus + 1 >= found ? "BACK" : "NEXT"});
}

void draw_pairing(ui::Canvas& canvas, const BravoSnapshot& snapshot) {
    draw_heading(canvas, "PAIRING", Heading::Opened);
    tag(canvas, kPairedTagY, snapshot.view.pairing_tag);
    if (snapshot.view.step == BravoStep::Connecting)
        draw_centred_line(canvas, kStepY, "WAIT");
    else
        draw_centred_lines(canvas, kStepY, kPushBravoLines, 2);
    draw_keys(canvas, {{}, "CANCEL"});
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
    draw_heading(canvas, "NOT PAIRED", Heading::Opened);
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
    int y = draw_centred_line(canvas, kReasonY, first);
    if (reason.second[0] != 0) y = draw_centred_line(canvas, y, reason.second);
    char detail[kDetailBytes];
    detail[reason_detail(detail, refusal)] = 0;
    wrapped(canvas, y + kParagraphGap, detail);
    const bool retries = refusal != BravoRefusal::PhonesConnected;
    draw_keys(canvas, {{Press::Once, retries ? "PAIR" : ""}, "BACK"});
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
    const int top = draw_heading(canvas, kOwnIcaoNoticeTitle, Heading::Interrupt);
    const int y = draw_centred_lines(canvas, top, kOwnIcaoNoticeLines, 3);
    draw_centred_lines(canvas, y, kOwnIcaoNoticeFix, 3);
    draw_keys(canvas, {{Press::Once, "CLOSE"}, "CLOSE"});
}

// INFO: fc 11nov26 Bravo holds no pin for this Go, a pairing cut short or a Bravo paired elsewhere
void draw_bravo_forgot_notice(ui::Canvas& canvas) {
    const int top = draw_heading(canvas, "BRAVO", Heading::Interrupt);
    const int y = draw_centred_lines(canvas, top, kForgotNoticeLines, 3);
    draw_centred_lines(canvas, y, kForgotNoticeFix, 2);
    draw_keys(canvas, {{Press::Once, "CLOSE"}, "CLOSE"});
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
