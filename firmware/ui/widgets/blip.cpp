#include "ui/widgets/blip.h"

#include <algorithm>

namespace skyblip::ui {

namespace {

struct Sprite {
    const uint32_t* rows;
    int width;
    int height;
    int origin_dx;
    int origin_dy;
};

constexpr uint32_t kNearPointRows[] = {
    0x00000100,  // ........#........
    0x00000380,  // .......###.......
    0x000007c0,  // ......#####......
    0x00000ee0,  // .....###.###.....
    0x00001c70,  // ....###...###....
    0x00003838,  // ...###.....###...
    0x0000701c,  // ..###.......###..
    0x0000fffe,  // .###############.
    0x0001ffff,  // #################
};

constexpr uint32_t kDiamondRows[] = {
    0x00000080,  // .......#.......
    0x000001c0,  // ......###......
    0x000003e0,  // .....#####.....
    0x00000770,  // ....###.###....
    0x00000e38,  // ...###...###...
    0x00001c1c,  // ..###.....###..
    0x0000380e,  // .###.......###.
    0x00007007,  // ###.........###
    0x0000380e,  // .###.......###.
    0x00001c1c,  // ..###.....###..
    0x00000e38,  // ...###...###...
    0x00000770,  // ....###.###....
    0x000003e0,  // .....#####.....
    0x000001c0,  // ......###......
    0x00000080,  // .......#.......
};

constexpr uint32_t kNearCaretRows[] = {
    0x00000080,  // .......#.......
    0x000001c0,  // ......###......
    0x00000360,  // .....##.##.....
    0x00000630,  // ....##...##....
    0x00000c18,  // ...##.....##...
    0x0000180c,  // ..##.......##..
    0x00003006,  // .##.........##.
    0x00006003,  // ##...........##
};

constexpr uint32_t kFarPointRows[] = {
    0x00000040,  // ......#......
    0x000000e0,  // .....###.....
    0x000001b0,  // ....##.##....
    0x00000318,  // ...##...##...
    0x0000060c,  // ..##.....##..
    0x00000c06,  // .##.......##.
    0x00001fff,  // #############
};

constexpr uint32_t kFarCaretRows[] = {
    0x00000020,  // .....#.....
    0x00000070,  // ....###....
    0x000000d8,  // ...##.##...
    0x0000018c,  // ..##...##..
    0x00000306,  // .##.....##.
    0x00000603,  // ##.......##
};

constexpr Sprite kNearPoint{kNearPointRows, 17, 9, -8, -7};
constexpr Sprite kDiamond{kDiamondRows, 15, 15, -7, -7};
constexpr Sprite kNearCaret{kNearCaretRows, 15, 8, -7, -11};
constexpr Sprite kFarPoint{kFarPointRows, 13, 7, -6, -5};
constexpr Sprite kFarCaret{kFarCaretRows, 11, 6, -5, -9};

bool beyond_the_window(Vertical vertical) {
    return vertical == Vertical::FarAbove || vertical == Vertical::FarBelow;
}

bool points_down(Vertical vertical) {
    return vertical == Vertical::Below || vertical == Vertical::FarBelow;
}

const Sprite& body_of(Vertical vertical) {
    if (vertical == Vertical::Level) return kDiamond;
    return beyond_the_window(vertical) ? kFarPoint : kNearPoint;
}

const Sprite& caret_of(Vertical vertical) {
    return beyond_the_window(vertical) ? kFarCaret : kNearCaret;
}

int top_row(const Sprite& sprite, bool flip) {
    return flip ? -(sprite.origin_dy + sprite.height - 1) : sprite.origin_dy;
}

int bottom_row(const Sprite& sprite, bool flip) {
    return top_row(sprite, flip) + sprite.height - 1;
}

void blit(Canvas& canvas, int x, int y, const Sprite& sprite, bool flip, bool fill) {
    for (int row = 0; row < sprite.height; row++) {
        const uint32_t bits = sprite.rows[flip ? sprite.height - 1 - row : row];
        if (bits == 0) continue;
        const int row_y = y + top_row(sprite, flip) + row;
        int first = 0, last = sprite.width - 1;
        while (first < sprite.width && !(bits & (1u << first))) first++;
        while (last > first && !(bits & (1u << last))) last--;
        for (int column = first; column <= last; column++)
            if (fill || (bits & (1u << column)))
                canvas.set_pixel(x + sprite.origin_dx + column, row_y, true);
    }
}

bool caret_above(Trend trend) { return trend == Trend::Climbing; }

}  // namespace

void draw_blip(Canvas& canvas, int x, int y, Vertical vertical, Trend trend, bool alarm) {
    const bool flip = points_down(vertical);
    blit(canvas, x, y, body_of(vertical), flip, alarm);
    if (trend == Trend::Steady) return;
    blit(canvas, x, y, caret_of(vertical), !caret_above(trend), false);
}

int blip_above(Vertical vertical, Trend trend) {
    const bool flip = points_down(vertical);
    int above = -top_row(body_of(vertical), flip);
    if (caret_above(trend)) above = std::max(above, -top_row(caret_of(vertical), false));
    return above;
}

int blip_below(Vertical vertical, Trend trend) {
    const bool flip = points_down(vertical);
    int below = bottom_row(body_of(vertical), flip);
    if (trend == Trend::Sinking) below = std::max(below, bottom_row(caret_of(vertical), true));
    return below;
}

int blip_beside(Vertical vertical) { return body_of(vertical).width / 2 + 1; }

}  // namespace skyblip::ui
