#include "ui/canvas.h"

namespace skyblip::ui {

namespace {
// INFO: fc 09nov26 the glyphs are the HD44780U A00 ROM, as Hitachi's datasheet Table 4 draws them
struct Glyph {
    char character;
    char rows[kCharacterHeight][kCharacterWidth + 1];
};
const Glyph kFont[] = {
    {' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "#...#", "....#", "...#.", "..#..", "..#..", "..#.."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {'A', {".###.", "#...#", "#...#", "#...#", "#####", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {':', {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}},
    {'/', {".....", "....#", "...#.", "..#..", ".#...", "#....", "....."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {'%', {"##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##"}},
};
const Glyph& find_glyph(char character) {
    if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
    for (const auto& glyph : kFont)
        if (glyph.character == character) return glyph;
    return kFont[0];
}
}

void Canvas::clear(bool white) {
    uint8_t fill = white ? 0x00 : 0xFF;
    for (size_t i = 0; i < bytes(); i++) data_[i] = fill;
}

void Canvas::set_pixel(int x, int y, bool black) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    uint8_t& byte = data_[y * stride_ + (x >> 3)];
    uint8_t mask = static_cast<uint8_t>(0x80 >> (x & 7));
    if (black)
        byte |= mask;
    else
        byte &= static_cast<uint8_t>(~mask);
}

bool Canvas::get_pixel(int x, int y) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
    return data_[y * stride_ + (x >> 3)] & (0x80 >> (x & 7));
}

void Canvas::horizontal_line(int x, int y, int width, bool black) {
    for (int i = 0; i < width; i++) set_pixel(x + i, y, black);
}
void Canvas::vertical_line(int x, int y, int height, bool black) {
    for (int i = 0; i < height; i++) set_pixel(x, y + i, black);
}

void Canvas::line(int x0, int y0, int x1, int y1, bool black) {
    int delta_x = x1 > x0 ? x1 - x0 : x0 - x1;
    int delta_y = y1 > y0 ? y1 - y0 : y0 - y1;
    int step_x = x0 < x1 ? 1 : -1;
    int step_y = y0 < y1 ? 1 : -1;
    int error = delta_x - delta_y;
    for (;;) {
        set_pixel(x0, y0, black);
        if (x0 == x1 && y0 == y1) break;
        int doubled_error = 2 * error;
        if (doubled_error > -delta_y) {
            error -= delta_y;
            x0 += step_x;
        }
        if (doubled_error < delta_x) {
            error += delta_x;
            y0 += step_y;
        }
    }
}

void Canvas::rectangle(int x, int y, int width, int height, bool black, bool fill) {
    if (fill) {
        for (int j = 0; j < height; j++) horizontal_line(x, y + j, width, black);
    } else {
        horizontal_line(x, y, width, black);
        horizontal_line(x, y + height - 1, width, black);
        vertical_line(x, y, height, black);
        vertical_line(x + width - 1, y, height, black);
    }
}

void Canvas::circle(int centre_x, int centre_y, int radius, bool black, bool fill) {
    int x = radius, y = 0, error = 1 - radius;
    while (x >= y) {
        if (fill) {
            horizontal_line(centre_x - x, centre_y + y, 2 * x + 1, black);
            horizontal_line(centre_x - x, centre_y - y, 2 * x + 1, black);
            horizontal_line(centre_x - y, centre_y + x, 2 * y + 1, black);
            horizontal_line(centre_x - y, centre_y - x, 2 * y + 1, black);
        } else {
            set_pixel(centre_x + x, centre_y + y, black);
            set_pixel(centre_x - x, centre_y + y, black);
            set_pixel(centre_x + x, centre_y - y, black);
            set_pixel(centre_x - x, centre_y - y, black);
            set_pixel(centre_x + y, centre_y + x, black);
            set_pixel(centre_x - y, centre_y + x, black);
            set_pixel(centre_x + y, centre_y - x, black);
            set_pixel(centre_x - y, centre_y - x, black);
        }
        y++;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            x--;
            error += 2 * (y - x) + 1;
        }
    }
}

int Canvas::draw_character(int x, int y, char character, bool black, int scale) {
    const Glyph& glyph = find_glyph(character);
    for (int column = 0; column < kCharacterWidth; column++) {
        for (int row = 0; row < kCharacterHeight; row++) {
            if (glyph.rows[row][column] == '#') {
                for (int block_x = 0; block_x < scale; block_x++)
                    for (int block_y = 0; block_y < scale; block_y++)
                        set_pixel(x + column * scale + block_x, y + row * scale + block_y, black);
            }
        }
    }
    return x + kCellWidth * scale;
}

int Canvas::draw_text(int x, int y, const char* text, bool black, int scale) {
    while (*text) {
        x = draw_character(x, y, *text++, black, scale);
    }
    return x;
}

int Canvas::count_black() const {
    int count = 0;
    for (size_t i = 0; i < bytes(); i++) count += __builtin_popcount(data_[i]);
    return count;
}

}
