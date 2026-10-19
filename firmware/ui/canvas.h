#ifndef SKYBLIP_UI_CANVAS_H
#define SKYBLIP_UI_CANVAS_H

#include <cstddef>
#include <cstdint>

namespace skyblip::ui {

constexpr int kCharW = 5;
constexpr int kCharH = 7;
constexpr int kCellW = 6;

class Canvas {
   public:
    Canvas(uint8_t* data, int width, int height)
        : data_(data), width_(width), height_(height), stride_((width + 7) / 8) {}

    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return stride_; }
    size_t bytes() const { return static_cast<size_t>(stride_) * static_cast<size_t>(height_); }

    void clear(bool white = true);
    void set_pixel(int x, int y, bool black);
    bool get_pixel(int x, int y) const;

    void hline(int x, int y, int w, bool black);
    void vline(int x, int y, int h, bool black);
    void line(int x0, int y0, int x1, int y1, bool black);
    void rect(int x, int y, int w, int h, bool black, bool fill = false);
    void circle(int centre_x, int centre_y, int r, bool black, bool fill = false);

    int draw_char(int x, int y, char c, bool black, int scale = 1);
    int draw_text(int x, int y, const char* text, bool black, int scale = 1);

    const uint8_t* data() const { return data_; }
    uint8_t* data() { return data_; }
    int count_black() const;

   private:
    uint8_t* data_;
    int width_;
    int height_;
    int stride_;
};

template <int W, int H>
class Panel : public Canvas {
   public:
    static constexpr int kW = W;
    static constexpr int kH = H;
    static constexpr int kStride = (W + 7) / 8;
    static constexpr size_t kBytes = static_cast<size_t>(kStride) * static_cast<size_t>(H);

    Panel() : Canvas(store_, W, H) {}
    Panel(const Panel& other) : Canvas(store_, W, H) { assign(other); }
    Panel& operator=(const Panel& other) {
        if (this != &other) assign(other);
        return *this;
    }

   private:
    void assign(const Panel& other) {
        for (size_t i = 0; i < kBytes; i++) store_[i] = other.store_[i];
    }

    uint8_t store_[kBytes]{};
};

}  // namespace skyblip::ui

#endif
