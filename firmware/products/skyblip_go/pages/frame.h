#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_FRAME_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_FRAME_H

#include <cstdint>

#include "products/skyblip_go/glass.h"

namespace skyblip::go {

enum class Heading : uint8_t { Opened, Interrupt };
enum class Press : uint8_t { Once, Twice };

struct ButtonKey {
    Press press{Press::Once};
    const char* verb{""};
};

struct Keys {
    ButtonKey button{};
    const char* pad{""};
};

constexpr int kFrameScale = 2;
constexpr int kFrameCellWidth = ui::kCellWidth * kFrameScale;
constexpr int kFrameColumns = kGlassWidth / kFrameCellWidth;
constexpr int kFrameLeftX = 6;
constexpr int kParagraphGap = 10;

constexpr int kOpenedTitleY = 4;
constexpr int kOpenedRuleY = 22;
constexpr int kInterruptTitleY = 14;
constexpr int kInterruptRuleY = 38;
constexpr int kBodyBelowRule = 6;

constexpr int kKeyTop = 174;
constexpr int kKeyHeight = 24;
constexpr int kKeyWidth = 97;
constexpr int kButtonKeyX = 1;
constexpr int kPadKeyX = kGlassWidth - kButtonKeyX - kKeyWidth;
constexpr int kBodyBottom = kKeyTop - 4;

constexpr int text_cells(const char* text) {
    int count = 0;
    while (text[count]) count++;
    return count;
}

constexpr int text_width(const char* text, int scale) {
    return text_cells(text) * ui::kCellWidth * scale - scale;
}

constexpr int centred_x(const char* text, int scale) {
    return (kGlassWidth - text_cells(text) * ui::kCellWidth * scale) / 2;
}

constexpr int line_pitch(int scale) { return ui::kCharacterHeight * scale + 2 * scale; }

int heading_title_x(const char* title, Heading heading);
constexpr int heading_title_y(Heading heading) {
    return heading == Heading::Interrupt ? kInterruptTitleY : kOpenedTitleY;
}
int draw_heading(ui::Canvas& canvas, const char* title, Heading heading);
void draw_keys(ui::Canvas& canvas, const Keys& keys);

int draw_centred_lines(ui::Canvas& canvas, int y, const char* const* lines, int count,
                       int scale = kFrameScale);
int draw_centred_line(ui::Canvas& canvas, int y, const char* line, int scale = kFrameScale);

int take_line(const char* text, int from, char* out, int columns);

}  // namespace skyblip::go

#endif
