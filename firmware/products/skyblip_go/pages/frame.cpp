#include "products/skyblip_go/pages/frame.h"

#include <cstring>

#include "ui/widgets/mark.h"

namespace skyblip::go {

namespace {

constexpr int kOpenedMarkGap = 6;
constexpr int kInterruptMarkGap = 8;
constexpr int kShortestRule = 140;
constexpr int kRuleOverhang = 8;
constexpr int kKeyRadius = 6;
constexpr int kKeyStroke = 2;
constexpr int kIconGap = 3;
constexpr int kPadIconWidth = 15;
constexpr int kPadIconHeight = 9;
constexpr int kPadIconRadius = 3;
constexpr int kButtonIconRadius = 4;
constexpr int kButtonIconWidth = 2 * kButtonIconRadius + 1;

enum class Icon : uint8_t { Pad, Button };

int icon_width(Icon icon) { return icon == Icon::Pad ? kPadIconWidth : kButtonIconWidth; }

void draw_icon(ui::Canvas& canvas, Icon icon, int x, int centre_y, bool ink) {
    if (icon == Icon::Pad) {
        canvas.rounded_rectangle(x, centre_y - kPadIconHeight / 2, kPadIconWidth, kPadIconHeight,
                                 kPadIconRadius, ink, /*fill=*/false, kKeyStroke);
        return;
    }
    canvas.circle(x + kButtonIconRadius, centre_y, kButtonIconRadius, ink, /*fill=*/true);
}

// INFO: fc 14nov26 the button's key is filled and the pad's is not, so the contact reads first
void draw_key(ui::Canvas& canvas, int x, Icon icon, const char* verb) {
    const bool button = icon != Icon::Pad;
    canvas.rounded_rectangle(x, kKeyTop, kKeyWidth, kKeyHeight, kKeyRadius, true, button,
                             kKeyStroke);
    const int width = icon_width(icon) + kIconGap + text_width(verb, kFrameScale);
    const int left = x + (kKeyWidth - width) / 2;
    const int centre_y = kKeyTop + kKeyHeight / 2;
    draw_icon(canvas, icon, left, centre_y, !button);
    canvas.draw_text(left + icon_width(icon) + kIconGap,
                     centre_y - ui::kCharacterHeight * kFrameScale / 2, verb, !button, kFrameScale);
}

int break_within(const char* text, int from, int to) {
    for (int i = from; i < to; i++)
        if (text[i] == '\n') return i;
    return -1;
}

ui::MarkSize mark_size(Heading heading) {
    return heading == Heading::Interrupt ? ui::MarkSize::Large : ui::MarkSize::Small;
}

int mark_gap(Heading heading) {
    return heading == Heading::Interrupt ? kInterruptMarkGap : kOpenedMarkGap;
}

int heading_group(const char* title, Heading heading) {
    return ui::mark_extent(mark_size(heading)).width + mark_gap(heading) +
           text_width(title, kFrameScale);
}

}  // namespace

int heading_title_x(const char* title, Heading heading) {
    return (kGlassWidth - heading_group(title, heading)) / 2 +
           ui::mark_extent(mark_size(heading)).width + mark_gap(heading);
}

// INFO: fc 14nov26 the mark's ink box is centred on the capitals' height, the usual rule for an
// icon
int draw_heading(ui::Canvas& canvas, const char* title, Heading heading) {
    const ui::MarkExtent mark = ui::mark_extent(mark_size(heading));
    const int group = heading_group(title, heading);
    const int title_y = heading_title_y(heading);
    const int rule_y = heading == Heading::Interrupt ? kInterruptRuleY : kOpenedRuleY;
    const int cap_height = ui::kCharacterHeight * kFrameScale;

    canvas.clear(true);
    ui::draw_mark(canvas, (kGlassWidth - group) / 2, title_y + (cap_height - mark.height) / 2,
                  mark_size(heading), true);
    canvas.draw_text(heading_title_x(title, heading), title_y, title, true, kFrameScale);
    const int rule =
        group + 2 * kRuleOverhang > kShortestRule ? group + 2 * kRuleOverhang : kShortestRule;
    canvas.horizontal_line((kGlassWidth - rule) / 2, rule_y, rule, true);
    return rule_y + kBodyBelowRule;
}

void draw_keys(ui::Canvas& canvas, const Keys& keys) {
    if (keys.button[0] != 0) draw_key(canvas, kButtonKeyX, Icon::Button, keys.button);
    if (keys.pad[0] != 0) draw_key(canvas, kPadKeyX, Icon::Pad, keys.pad);
}

// INFO: fc 14nov26 the body's ink is centred between the heading's rule and the keys, or the foot
int body_height(const BodyLine* lines, int count) {
    int height = 0;
    int trailing = 0;
    for (int i = 0; i < count; i++) {
        if (lines[i].text[0] == 0) {
            if (height > 0) trailing += kParagraphGap;
            continue;
        }
        height += trailing + (height > 0 ? 2 * lines[i].scale : 0);
        trailing = 0;
        height += ui::kCharacterHeight * lines[i].scale;
    }
    return height;
}

int body_top(Heading heading, bool keys, const BodyLine* lines, int count) {
    return body_top_above(heading, keys ? kKeyTop : kGlassHeight, lines, count);
}

int body_top_above(Heading heading, int bottom, const BodyLine* lines, int count) {
    const int rule_y = heading == Heading::Interrupt ? kInterruptRuleY : kOpenedRuleY;
    const int top = rule_y + 1;
    return top + (bottom - top - body_height(lines, count)) / 2;
}

void draw_body(ui::Canvas& canvas, Heading heading, bool keys, const BodyLine* lines, int count) {
    draw_body_above(canvas, heading, keys ? kKeyTop : kGlassHeight, lines, count);
}

void draw_body_above(ui::Canvas& canvas, Heading heading, int bottom, const BodyLine* lines,
                     int count) {
    int y = body_top_above(heading, bottom, lines, count);
    bool started = false;
    for (int i = 0; i < count; i++) {
        if (lines[i].text[0] == 0) {
            if (started) y += kParagraphGap;
            continue;
        }
        if (started) y += 2 * lines[i].scale;
        canvas.draw_text(centred_x(lines[i].text, lines[i].scale), y, lines[i].text, true,
                         lines[i].scale);
        y += ui::kCharacterHeight * lines[i].scale;
        started = true;
    }
}

void draw_foot_line(ui::Canvas& canvas, const char* text) {
    canvas.draw_text(centred_x(text, kFrameScale), kFootLineY, text, true, kFrameScale);
}

int take_line(const char* text, int from, char* out, int columns) {
    const int length = static_cast<int>(std::strlen(text));
    if (from >= length) {
        out[0] = 0;
        return from;
    }
    int end = from + columns;
    const int newline = break_within(text, from, end < length ? end + 1 : length);
    if (newline >= 0) {
        end = newline;
    } else if (end >= length) {
        end = length;
    } else {
        int space = end;
        while (space > from && text[space] != ' ') space--;
        if (space > from) end = space;
    }
    int written = 0;
    for (int i = from; i < end; i++) out[written++] = text[i];
    out[written] = 0;
    return text[end] == ' ' || text[end] == '\n' ? end + 1 : end;
}

}  // namespace skyblip::go
