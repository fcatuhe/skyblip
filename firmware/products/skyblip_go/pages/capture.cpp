#include "products/skyblip_go/pages/capture.h"

#include "core/util/format.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go {

namespace {

constexpr int kLeft = 4;
constexpr int kCellW = 6;
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

void row(ui::Canvas& canvas, int line, char* buf, int n) {
    buf[n] = 0;
    canvas.draw_text(kLeft, line_y(line), buf, true, 1);
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

const char* capture_subjects(diag::Profile profile) {
    return diag::paced(profile) ? "POWER AND DUTY" : "EVERY SUBJECT";
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

void draw_title(ui::Canvas& canvas, const CaptureSnapshot& s) {
    canvas.draw_text(kLeft, kTitleY, page_title(Page::Capture), true, 1);
    char buf[16];
    int n = format_string(buf, "T+");
    n += format_uint(buf + n, s.uptime_s % kUptimeClockWrapS);
    buf[n] = 0;
    canvas.draw_text(kRight - n * kCellW, kTitleY, buf, true, 1);
    canvas.hline(kLeft, kTitleRuleY, kRight - kLeft, true);
}

void draw_state(ui::Canvas& canvas, const CaptureSnapshot& s) {
    char buf[40];
    int n = format_word(buf, "STATE");
    if (s.capture.armed) {
        n += format_word(buf + n, "ARMED");
        n += format_string(buf + n, capture_name(s.running));
    } else {
        n += format_string(buf + n, stopped_word(s.capture.stopped));
    }
    row(canvas, kStateLine, buf, n);
}

void draw_offered_capture(ui::Canvas& canvas, int line, diag::Profile profile, bool focused) {
    const int y = line_y(line);
    if (focused)
        canvas.rect(2, y - kFocusBarRise, kGlassWidth - 4, kLineH - 1, true, /*fill=*/true);
    const bool ink = !focused;
    const char* subjects = capture_subjects(profile);
    canvas.draw_text(kLeft, y, capture_name(profile), ink, 1);
    canvas.draw_text(kRight - text_cells(subjects) * kCellW, y, subjects, ink, 1);
}

void draw_price(ui::Canvas& canvas, const CaptureSnapshot& s) {
    char buf[40];
    int n = format_word(buf, "TAKES");
    n += format_uint(buf + n, s.capture.price_sectors);
    n += format_string(buf + n, " OF ");
    n += format_uint(buf + n, s.capture.pool_sectors);
    n += format_string(buf + n, " SECTORS");
    row(canvas, kPriceLine, buf, n);

    n = format_word(buf, "EVICTS");
    n += format_uint(buf + n, s.capture.price_flights);
    n += format_string(buf + n, " FLIGHTS");
    row(canvas, kPriceLine + 1, buf, n);

    n = format_word(buf, "KEEPS");
    n += format_span(buf + n, s.focus_keeps_s);
    n += format_string(buf + n, " ROLLING");
    row(canvas, kPriceLine + 2, buf, n);
}

void draw_wrote(ui::Canvas& canvas, const bus::CaptureState& c) {
    if (c.records == 0) return;
    char buf[40];
    int n = format_word(buf, "WROTE");
    n += format_uint(buf + n, c.records);
    n += format_string(buf + n, " DROP ");
    n += format_uint(buf + n, c.dropped);
    row(canvas, kWroteLine, buf, n);
}

void draw_offer(ui::Canvas& canvas, const CaptureSnapshot& s) {
    if (s.capture.available) {
        int line = kOfferLine;
        diag::Profile profile = kFirstCapture;
        do {
            draw_offered_capture(canvas, line++, profile, s.focus == profile);
            profile = next_capture(profile);
        } while (profile != kFirstCapture);
    }
    draw_price(canvas, s);
    draw_wrote(canvas, s.capture);
}

void draw_progress(ui::Canvas& canvas, const bus::CaptureState& c) {
    char buf[40];
    int n = format_word(buf, "SESSION");
    n += format_uint(buf + n, c.session_id);
    row(canvas, kProgressLine, buf, n);

    n = format_word(buf, "WROTE");
    n += format_uint(buf + n, c.records);
    n += format_string(buf + n, " DROP ");
    n += format_uint(buf + n, c.dropped);
    row(canvas, kProgressLine + 1, buf, n);

    n = format_word(buf, "SECTORS");
    n += format_uint(buf + n, c.sectors);
    n += format_string(buf + n, " KEEPS ");
    n += format_span(buf + n, c.keeps_s);
    row(canvas, kProgressLine + 2, buf, n);
}

void draw_faults(ui::Canvas& canvas, const bus::CaptureState& c, int line) {
    if (c.faults == 0 && c.unreadable_sectors == 0) return;
    char buf[40];
    int n = format_word(buf, "FLASH FAULTS");
    n += format_uint(buf + n, c.faults);
    n += format_string(buf + n, " UNREAD ");
    n += format_uint(buf + n, c.unreadable_sectors);
    row(canvas, line, buf, n);
}

void draw_hint(ui::Canvas& canvas, const CaptureSnapshot& s) {
    const char* hint = !s.capture.available ? "NO FLASH FITTED"
                       : s.capture.armed    ? "PRESS TWICE TO STOP"
                       : s.arming           ? "PRESS AGAIN TO ARM"
                                            : "PAD PICKS  PRESS TWICE TO ARM";
    canvas.hline(kLeft, kHintRuleY, kRight - kLeft, true);
    canvas.draw_text(kLeft, kHintY, hint, true, 1);
}

}  // namespace

const char* capture_name(diag::Profile profile) {
    switch (profile) {
        case diag::Profile::PowerRun: return "POWER RUN";
        case diag::Profile::FlightRun: return "FLIGHT RUN";
        case diag::Profile::Full: break;
    }
    return "FULL";
}

void draw_capture(ui::Canvas& canvas, const CaptureSnapshot& s) {
    canvas.clear(true);
    draw_title(canvas, s);
    draw_state(canvas, s);

    if (s.capture.armed)
        draw_progress(canvas, s.capture);
    else
        draw_offer(canvas, s);

    draw_faults(canvas, s.capture, s.capture.armed ? kArmedFaultsLine : kOfferedFaultsLine);
    draw_hint(canvas, s);
}

}  // namespace skyblip::go
