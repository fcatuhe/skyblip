#include "products/skyblip_go/pages/capture.h"

#include "core/util/format.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go {

namespace {

constexpr int kLeft = 4;
using ui::kCellW;
constexpr int kRight = kGlassWidth - kLeft;
constexpr int kTitleY = 2;
constexpr int kTitleRuleY = 12;
constexpr int kTopY = 20;
constexpr int kLineH = 14;
constexpr int kFocusBarRise = 3;
constexpr int kHintRuleY = 176;
constexpr int kHintY = 182;

constexpr int kStateLine = 0;
constexpr int kProgressLine = 1;
constexpr int kOfferLine = 1;
constexpr int kPriceLine = 4;
constexpr int kWroteLine = 7;
constexpr int kArmedFaultsLine = 5;
constexpr int kOfferedFaultsLine = 8;

int line_y(int line) { return kTopY + line * kLineH; }

void row(ui::Canvas& canvas, int line, char* buffer, int n) {
    buffer[n] = 0;
    canvas.draw_text(kLeft, line_y(line), buffer, true, 1);
}

int format_word(char* out, const char* word) {
    int n = format_string(out, word);
    out[n++] = ' ';
    return n;
}

int format_span(char* out, uint32_t seconds) {
    int n = format_uint(out, seconds / 3600);
    out[n++] = 'H';
    n += format_uint(out + n, seconds % 3600 / 60, 2);
    return n;
}

const char* capture_subjects(diagnostics::Profile profile) {
    return diagnostics::paced(profile) ? "POWER AND DUTY" : "EVERY SUBJECT";
}

const char* stopped_word(bus::CaptureStop stopped) {
    switch (stopped) {
        case bus::CaptureStop::NoSectors: return "STOPPED NO SECTORS";
        case bus::CaptureStop::NoStorage: return "STOPPED NO FLASH";
        case bus::CaptureStop::Pilot: return "STOPPED";
        case bus::CaptureStop::None: break;
    }
    return "OFF";
}

void draw_title(ui::Canvas& canvas, const CaptureSnapshot& snapshot) {
    canvas.draw_text(kLeft, kTitleY, page_title(Page::Capture), true, 1);
    char text[16];
    int written = format_string(text, "T+");
    written += format_uint(text + written, snapshot.uptime_s % kUptimeClockWrapS);
    text[written] = 0;
    canvas.draw_text(kRight - written * kCellW, kTitleY, text, true, 1);
    canvas.hline(kLeft, kTitleRuleY, kRight - kLeft, true);
}

void draw_state(ui::Canvas& canvas, const CaptureSnapshot& snapshot) {
    char text[40];
    int written = format_word(text, "STATE");
    if (snapshot.capture.armed) {
        written += format_word(text + written, "ARMED");
        written += format_string(text + written, capture_name(snapshot.running));
    } else {
        written += format_string(text + written, stopped_word(snapshot.capture.stopped));
    }
    row(canvas, kStateLine, text, written);
}

void draw_offered_capture(ui::Canvas& canvas, int line, diagnostics::Profile profile,
                          bool focused) {
    const int y = line_y(line);
    if (focused)
        canvas.rect(2, y - kFocusBarRise, kGlassWidth - 4, kLineH - 1, true, /*fill=*/true);
    const bool ink = !focused;
    const char* subjects = capture_subjects(profile);
    canvas.draw_text(kLeft, y, capture_name(profile), ink, 1);
    canvas.draw_text(kRight - text_cells(subjects) * kCellW, y, subjects, ink, 1);
}

void draw_price(ui::Canvas& canvas, const CaptureSnapshot& snapshot) {
    char text[40];
    int written = format_word(text, "TAKES");
    written += format_uint(text + written, snapshot.capture.price_sectors);
    written += format_string(text + written, " OF ");
    written += format_uint(text + written, snapshot.capture.pool_sectors);
    written += format_string(text + written, " SECTORS");
    row(canvas, kPriceLine, text, written);

    written = format_word(text, "EVICTS");
    written += format_uint(text + written, snapshot.capture.price_flights);
    written += format_string(text + written, " FLIGHTS");
    row(canvas, kPriceLine + 1, text, written);

    written = format_word(text, "KEEPS");
    written += format_span(text + written, snapshot.focus_keeps_s);
    written += format_string(text + written, " ROLLING");
    row(canvas, kPriceLine + 2, text, written);
}

void draw_wrote(ui::Canvas& canvas, const bus::CaptureState& c) {
    if (c.records == 0) return;
    char text[40];
    int written = format_word(text, "WROTE");
    written += format_uint(text + written, c.records);
    written += format_string(text + written, " DROP ");
    written += format_uint(text + written, c.dropped);
    row(canvas, kWroteLine, text, written);
}

void draw_offer(ui::Canvas& canvas, const CaptureSnapshot& snapshot) {
    if (snapshot.capture.available) {
        int line = kOfferLine;
        diagnostics::Profile profile = kFirstCapture;
        do {
            draw_offered_capture(canvas, line++, profile, snapshot.focus == profile);
            profile = next_capture(profile);
        } while (profile != kFirstCapture);
    }
    draw_price(canvas, snapshot);
    draw_wrote(canvas, snapshot.capture);
}

void draw_progress(ui::Canvas& canvas, const bus::CaptureState& c) {
    char text[40];
    int written = format_word(text, "SESSION");
    written += format_uint(text + written, c.session_id);
    row(canvas, kProgressLine, text, written);

    written = format_word(text, "WROTE");
    written += format_uint(text + written, c.records);
    written += format_string(text + written, " DROP ");
    written += format_uint(text + written, c.dropped);
    row(canvas, kProgressLine + 1, text, written);

    written = format_word(text, "SECTORS");
    written += format_uint(text + written, c.sectors);
    written += format_string(text + written, " KEEPS ");
    written += format_span(text + written, c.keeps_s);
    row(canvas, kProgressLine + 2, text, written);
}

void draw_faults(ui::Canvas& canvas, const bus::CaptureState& c, int line) {
    if (c.faults == 0 && c.unreadable_sectors == 0) return;
    char text[40];
    int written = format_word(text, "FLASH FAULTS");
    written += format_uint(text + written, c.faults);
    written += format_string(text + written, " UNREAD ");
    written += format_uint(text + written, c.unreadable_sectors);
    row(canvas, line, text, written);
}

void draw_hint(ui::Canvas& canvas, const CaptureSnapshot& snapshot) {
    const char* hint = !snapshot.capture.available ? "NO FLASH FITTED"
                       : snapshot.capture.armed    ? "PRESS TWICE TO STOP"
                       : snapshot.arming           ? "PRESS AGAIN TO ARM"
                                                   : "PAD PICKS  PRESS TWICE TO ARM";
    canvas.hline(kLeft, kHintRuleY, kRight - kLeft, true);
    canvas.draw_text(kLeft, kHintY, hint, true, 1);
}

}  // namespace

const char* capture_name(diagnostics::Profile profile) {
    switch (profile) {
        case diagnostics::Profile::PowerRun: return "POWER RUN";
        case diagnostics::Profile::FlightRun: return "FLIGHT RUN";
        case diagnostics::Profile::Full: break;
    }
    return "FULL";
}

void draw_capture(ui::Canvas& canvas, const CaptureSnapshot& snapshot) {
    canvas.clear(true);
    draw_title(canvas, snapshot);
    draw_state(canvas, snapshot);

    if (snapshot.capture.armed)
        draw_progress(canvas, snapshot.capture);
    else
        draw_offer(canvas, snapshot);

    draw_faults(canvas, snapshot.capture,
                snapshot.capture.armed ? kArmedFaultsLine : kOfferedFaultsLine);
    draw_hint(canvas, snapshot);
}

}  // namespace skyblip::go
