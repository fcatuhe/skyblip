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
constexpr int kSecondPressOffset = 10;

enum class Icon : uint8_t { Pad, Once, Twice };

int icon_width(Icon icon) {
    switch (icon) {
        case Icon::Pad: return kPadIconWidth;
        case Icon::Twice: return kSecondPressOffset + kButtonIconWidth;
        case Icon::Once: break;
    }
    return kButtonIconWidth;
}

void draw_icon(ui::Canvas& canvas, Icon icon, int x, int centre_y, bool ink) {
    if (icon == Icon::Pad) {
        canvas.rounded_rectangle(x, centre_y - kPadIconHeight / 2, kPadIconWidth, kPadIconHeight,
                                 kPadIconRadius, ink, /*fill=*/false, kKeyStroke);
        return;
    }
    canvas.circle(x + kButtonIconRadius, centre_y, kButtonIconRadius, ink, /*fill=*/true);
    if (icon == Icon::Twice)
        canvas.circle(x + kSecondPressOffset + kButtonIconRadius, centre_y, kButtonIconRadius, ink,
                      /*fill=*/true);
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
    if (keys.button.verb[0] != 0)
        draw_key(canvas, kButtonKeyX, keys.button.press == Press::Twice ? Icon::Twice : Icon::Once,
                 keys.button.verb);
    if (keys.pad[0] != 0) draw_key(canvas, kPadKeyX, Icon::Pad, keys.pad);
}

int draw_centred_line(ui::Canvas& canvas, int y, const char* line, int scale) {
    canvas.draw_text(centred_x(line, scale), y, line, true, scale);
    return y + line_pitch(scale);
}

// INFO: fc 14nov26 an empty line is a paragraph break, the gap between two sentences
int draw_centred_lines(ui::Canvas& canvas, int y, const char* const* lines, int count, int scale) {
    for (int i = 0; i < count; i++)
        y = lines[i][0] == 0 ? y + kParagraphGap : draw_centred_line(canvas, y, lines[i], scale);
    return y + kParagraphGap;
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
