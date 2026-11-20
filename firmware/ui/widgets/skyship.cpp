#include "ui/widgets/skyship.h"

namespace skyblip::ui {

namespace {
// INFO: fc 20nov26 bin/rails ownship:compile writes this table: edit server/content/ownships/
constexpr int kGlyphWidth = 24;
constexpr int kGlyphOriginRow = 5;
constexpr uint32_t kOwnshipRows[] = {
    0x00001800,  // ...........##...........
    0x0001ff80,  // .......##########.......
    0x00003c00,  // ..........####..........
    0x00003c00,  // ..........####..........
    0x003ffffc,  // ..####################..
    0x00ffffff,  // ########################
    0x00ffffff,  // ########################
    0x003ffffc,  // ..####################..
    0x0007ffe0,  // .....##############.....
    0x00003c00,  // ..........####..........
    0x00001800,  // ...........##...........
    0x00001800,  // ...........##...........
    0x0000ff00,  // ........########........
    0x0001ff80,  // .......##########.......
    0x0000ff00,  // ........########........
    0x00001800,  // ...........##...........
};
constexpr int kGlyphHeight = static_cast<int>(sizeof(kOwnshipRows) / sizeof(kOwnshipRows[0]));

struct Sprite {
    int width;
    int height;
    int origin_row;
    const uint32_t* rows;
};
constexpr Sprite kDefaultSprite{kGlyphWidth, kGlyphHeight, kGlyphOriginRow, kOwnshipRows};

}  // namespace

void draw_skyship(Canvas& canvas, int centre_x, int centre_y) {
    const Sprite& sprite = kDefaultSprite;
    static_assert(kGlyphWidth % 2 == 0, "even width: the centre is a pixel PAIR");
    static_assert(kGlyphOriginRow == kSkyshipRowsToNose,
                  "the nose a screen clears is this sprite's");
    static_assert(kGlyphWidth == kSkyshipSpan,
                  "the span a screen measures against is this sprite's");
    static_assert(kGlyphHeight == kSkyshipRows,
                  "the rows a screen measures against are this sprite's");
    const int x0 = centre_x - sprite.width / 2, y0 = centre_y - sprite.origin_row;
    for (int row = 0; row < sprite.height; row++) {
        uint32_t bits = sprite.rows[row];
        for (int column = 0; bits; column++, bits >>= 1) {
            if (bits & 1) canvas.set_pixel(x0 + column, y0 + row, true);
        }
    }
}

}  // namespace skyblip::ui
