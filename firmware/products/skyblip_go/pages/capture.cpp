#include "products/skyblip_go/pages/capture.h"

#include "core/util/format.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go {

namespace {

constexpr int kLeft = 4;
constexpr int kCellW = 6;
constexpr int kRight = kGlassW - kLeft;
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
constexpr int kPriceLine = 3;
constexpr int kWroteLine = 6;
constexpr int kArmedFaultsLine = 5;
constexpr int kOfferedFaultsLine = 7;

int line_y(int line) { return kTopY + line * kLineH; }

void row(ui::Canvas& fb, int line, char* buf, int n) {
    buf[n] = 0;
    fb.draw_text(kLeft, line_y(line), buf, true, 1);
}

int fmt_word(char* out, const char* word) {
    int n = fmt_string(out, word);
    out[n++] = ' ';
    return n;
}

int fmt_span(char* out, uint32_t seconds) {
    int n = fmt_uint(out, seconds / 3600);
    out[n++] = 'H';
    n += fmt_uint(out + n, seconds % 3600 / 60, 2);
    return n;
}

const char* capture_subjects(diag::Profile profile) {
    return profile == diag::Profile::PowerRun ? "POWER AND DUTY" : "EVERY SUBJECT";
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

void draw_title(ui::Canvas& fb, const CaptureSnapshot& s) {
    fb.draw_text(kLeft, kTitleY, page_title(Page::Capture), true, 1);
    char buf[16];
    int n = fmt_string(buf, "T+");
    n += fmt_uint(buf + n, s.uptime_s % kUptimeClockWrapS);
    buf[n] = 0;
    fb.draw_text(kRight - n * kCellW, kTitleY, buf, true, 1);
    fb.hline(kLeft, kTitleRuleY, kRight - kLeft, true);
}

void draw_state(ui::Canvas& fb, const CaptureSnapshot& s) {
    char buf[40];
    int n = fmt_word(buf, "STATE");
    if (s.capture.armed) {
        n += fmt_word(buf + n, "ARMED");
        n += fmt_string(buf + n, capture_name(s.running));
    } else {
        n += fmt_string(buf + n, stopped_word(s.capture.stopped));
    }
    row(fb, kStateLine, buf, n);
}

void draw_offered_capture(ui::Canvas& fb, int line, diag::Profile profile, bool focused) {
    const int y = line_y(line);
    if (focused) fb.rect(2, y - kFocusBarRise, kGlassW - 4, kLineH - 1, true, /*fill=*/true);
    const bool ink = !focused;
    const char* subjects = capture_subjects(profile);
    fb.draw_text(kLeft, y, capture_name(profile), ink, 1);
    fb.draw_text(kRight - text_cells(subjects) * kCellW, y, subjects, ink, 1);
}

void draw_price(ui::Canvas& fb, const CaptureSnapshot& s) {
    char buf[40];
    int n = fmt_word(buf, "TAKES");
    n += fmt_uint(buf + n, s.capture.price_sectors);
    n += fmt_string(buf + n, " OF ");
    n += fmt_uint(buf + n, s.capture.pool_sectors);
    n += fmt_string(buf + n, " SECTORS");
    row(fb, kPriceLine, buf, n);

    n = fmt_word(buf, "EVICTS");
    n += fmt_uint(buf + n, s.capture.price_flights);
    n += fmt_string(buf + n, " FLIGHTS");
    row(fb, kPriceLine + 1, buf, n);

    n = fmt_word(buf, "KEEPS");
    n += fmt_span(buf + n, s.focus_keeps_s);
    n += fmt_string(buf + n, " ROLLING");
    row(fb, kPriceLine + 2, buf, n);
}

void draw_wrote(ui::Canvas& fb, const bus::CaptureState& c) {
    if (c.records == 0) return;
    char buf[40];
    int n = fmt_word(buf, "WROTE");
    n += fmt_uint(buf + n, c.records);
    n += fmt_string(buf + n, " DROP ");
    n += fmt_uint(buf + n, c.dropped);
    row(fb, kWroteLine, buf, n);
}

void draw_offer(ui::Canvas& fb, const CaptureSnapshot& s) {
    if (s.capture.available) {
        draw_offered_capture(fb, kOfferLine, diag::Profile::Full, s.focus == diag::Profile::Full);
        draw_offered_capture(fb, kOfferLine + 1, diag::Profile::PowerRun,
                             s.focus == diag::Profile::PowerRun);
    }
    draw_price(fb, s);
    draw_wrote(fb, s.capture);
}

void draw_progress(ui::Canvas& fb, const bus::CaptureState& c) {
    char buf[40];
    int n = fmt_word(buf, "SESSION");
    n += fmt_uint(buf + n, c.session_id);
    row(fb, kProgressLine, buf, n);

    n = fmt_word(buf, "WROTE");
    n += fmt_uint(buf + n, c.records);
    n += fmt_string(buf + n, " DROP ");
    n += fmt_uint(buf + n, c.dropped);
    row(fb, kProgressLine + 1, buf, n);

    n = fmt_word(buf, "SECTORS");
    n += fmt_uint(buf + n, c.sectors);
    n += fmt_string(buf + n, " KEEPS ");
    n += fmt_span(buf + n, c.keeps_s);
    row(fb, kProgressLine + 2, buf, n);
}

void draw_faults(ui::Canvas& fb, const bus::CaptureState& c, int line) {
    if (c.faults == 0 && c.unreadable_sectors == 0) return;
    char buf[40];
    int n = fmt_word(buf, "FLASH FAULTS");
    n += fmt_uint(buf + n, c.faults);
    n += fmt_string(buf + n, " UNREAD ");
    n += fmt_uint(buf + n, c.unreadable_sectors);
    row(fb, line, buf, n);
}

void draw_hint(ui::Canvas& fb, const CaptureSnapshot& s) {
    const char* hint = !s.capture.available ? "NO FLASH FITTED"
                       : s.capture.armed    ? "PRESS TWICE TO STOP"
                       : s.arming           ? "PRESS AGAIN TO ARM"
                                            : "PAD PICKS  PRESS TWICE TO ARM";
    fb.hline(kLeft, kHintRuleY, kRight - kLeft, true);
    fb.draw_text(kLeft, kHintY, hint, true, 1);
}

}  // namespace

const char* capture_name(diag::Profile profile) {
    return profile == diag::Profile::PowerRun ? "POWER RUN" : "FULL";
}

void draw_capture(ui::Canvas& fb, const CaptureSnapshot& s) {
    fb.clear(true);
    draw_title(fb, s);
    draw_state(fb, s);

    if (s.capture.armed)
        draw_progress(fb, s.capture);
    else
        draw_offer(fb, s);

    draw_faults(fb, s.capture, s.capture.armed ? kArmedFaultsLine : kOfferedFaultsLine);
    draw_hint(fb, s);
}

}  // namespace skyblip::go
