// The mark from the logo, drawn alone: its ink lands where it is asked and the logo stays upright.
#include <initializer_list>

#include "doctest/doctest.h"
#include "ui/canvas.h"
#include "ui/widgets/mark.h"

using namespace skyblip::ui;

namespace {
using TestPanel = Panel<200, 200>;

struct Ink {
    int left{200}, top{200}, right{-1}, bottom{-1};
};

Ink ink_of(const TestPanel& canvas) {
    Ink ink;
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 200; x++)
            if (canvas.get_pixel(x, y)) {
                if (x < ink.left) ink.left = x;
                if (x > ink.right) ink.right = x;
                if (y < ink.top) ink.top = y;
                if (y > ink.bottom) ink.bottom = y;
            }
    return ink;
}
}  // namespace

TEST_CASE("mark: its ink starts at the corner it is given and is as large as its extent says") {
    for (const MarkSize size : {MarkSize::Small, MarkSize::Large}) {
        TestPanel canvas;
        canvas.clear(true);
        draw_mark(canvas, 40, 60, size, true);
        const Ink ink = ink_of(canvas);
        const MarkExtent extent = mark_extent(size);
        CHECK(ink.left == 40);
        CHECK(ink.top == 60);
        CHECK(ink.right - ink.left + 1 == extent.width);
        CHECK(ink.bottom - ink.top + 1 == extent.height);
    }
}

TEST_CASE("mark: the large mark is larger, and both read as the logo, arcs over the dot") {
    const MarkExtent small = mark_extent(MarkSize::Small);
    const MarkExtent large = mark_extent(MarkSize::Large);
    CHECK(large.width > small.width);
    CHECK(large.height > small.height);

    TestPanel canvas;
    canvas.clear(true);
    draw_mark(canvas, 0, 0, MarkSize::Large, true);
    // The dot sits at the bottom centre and the outer arc's ends rise clear of it on both sides.
    const int middle = large.width / 2;
    CHECK(canvas.get_pixel(middle, large.height - 2));
    CHECK(canvas.get_pixel(middle, 0));
    CHECK_FALSE(canvas.get_pixel(0, large.height - 1));
    CHECK_FALSE(canvas.get_pixel(large.width - 1, large.height - 1));
}

TEST_CASE("mark: it is symmetric about its vertical, as the logo is") {
    for (const MarkSize size : {MarkSize::Small, MarkSize::Large}) {
        TestPanel canvas;
        canvas.clear(true);
        draw_mark(canvas, 0, 0, size, true);
        const MarkExtent extent = mark_extent(size);
        for (int y = 0; y < extent.height; y++)
            for (int x = 0; x < extent.width; x++)
                CHECK(canvas.get_pixel(x, y) == canvas.get_pixel(extent.width - 1 - x, y));
    }
}

TEST_CASE("canvas: a rounded rectangle keeps its corners clear and its sides whole") {
    TestPanel outline;
    outline.clear(true);
    outline.rounded_rectangle(10, 10, 40, 20, 6, true, /*fill=*/false, 2);
    CHECK_FALSE(outline.get_pixel(10, 10));
    CHECK_FALSE(outline.get_pixel(49, 29));
    CHECK(outline.get_pixel(30, 10));
    CHECK(outline.get_pixel(30, 11));
    CHECK_FALSE(outline.get_pixel(30, 12));
    CHECK(outline.get_pixel(10, 20));
    CHECK_FALSE(outline.get_pixel(30, 20));

    TestPanel filled;
    filled.clear(true);
    filled.rounded_rectangle(10, 10, 40, 20, 6, true, /*fill=*/true);
    CHECK(filled.get_pixel(30, 20));
    CHECK_FALSE(filled.get_pixel(10, 10));
}
