// What one push costs the bus, and the turned glass pinned against the per-pixel rotation.
#include <random>
#include <vector>

#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "test/support/ssd1681_rig.h"

using namespace skyblip;

namespace {

constexpr int kStride = parts::Ssd1681::kGlassStride;

parts::Ssd1681 make_turned(models::Ssd1681& f) {
    return parts::Ssd1681(f, f, f, f.dc, f.rst, f.busy, -1, parts::GlassRotation::Deg270);
}

std::vector<uint8_t> per_pixel_bank(const parts::Ssd1681Glass& fb) {
    const uint8_t* fb_bytes = fb.data();
    std::vector<uint8_t> bank(parts::Ssd1681::kGlassBytes);
    for (int gate = 0; gate < parts::Ssd1681::kGlassH; gate++)
        for (int column = 0; column < kStride; column++) {
            const int x = parts::Ssd1681::kGlassW - 1 - gate;
            uint8_t bits = 0;
            for (int source = 0; source < 8; source++) {
                const int y = column * 8 + source;
                if (fb_bytes[y * kStride + (x >> 3)] & (0x80 >> (x & 7)))
                    bits |= static_cast<uint8_t>(0x80 >> source);
            }
            bank[size_t(gate) * kStride + size_t(column)] = static_cast<uint8_t>(~bits);
        }
    return bank;
}

std::vector<parts::Ssd1681Glass> patterns() {
    std::vector<parts::Ssd1681Glass> out;
    parts::Ssd1681Glass fb;

    fb.clear(/*white=*/false);
    out.push_back(fb);

    fb.clear(true);
    for (int y = 0; y < parts::Ssd1681::kGlassH; y++)
        for (int x = 0; x < parts::Ssd1681::kGlassW; x++) fb.set_pixel(x, y, (x + y) & 1);
    out.push_back(fb);

    std::mt19937 rng(874);
    for (size_t i = 0; i < parts::Ssd1681::kGlassBytes; i++)
        fb.data()[i] = static_cast<uint8_t>(rng());
    out.push_back(fb);

    const int corners[4][2] = {{0, 0}, {199, 0}, {0, 199}, {199, 199}};
    for (const auto& corner : corners) {
        fb.clear(true);
        fb.set_pixel(corner[0], corner[1], true);
        out.push_back(fb);
    }
    return out;
}

}  // namespace

TEST_CASE("epd: a turned glass sends the bytes the per-pixel rotation sent, on every pattern") {
    for (const parts::Ssd1681Glass& fb : patterns()) {
        models::Ssd1681 f;
        parts::Ssd1681 d = make_turned(f);
        d.begin();

        d.present(fb, ports::Refresh::Full, 0);
        CHECK(f.ram == per_pixel_bank(fb));
        CHECK(f.ram_previous == f.ram);
    }
}

TEST_CASE("epd: a turned partial sends the frame before it to 0x26 as the per-pixel rotation did") {
    const std::vector<parts::Ssd1681Glass> frames = patterns();
    models::Ssd1681 f;
    parts::Ssd1681 d = make_turned(f);
    d.begin();
    d.present(frames[0], ports::Refresh::Full, 0);
    settle(d, 0);

    for (size_t i = 1; i < frames.size(); i++) {
        const uint32_t now_ms = uint32_t(i) * 5000;
        d.present(frames[i], ports::Refresh::Partial, now_ms);
        CHECK_FALSE(f.last_full);
        CHECK(f.ram_previous == per_pixel_bank(frames[i - 1]));
        CHECK(f.ram == per_pixel_bank(frames[i]));
        CHECK(d.ready(now_ms + parts::Ssd1681::kReadyAfterFullMs));
    }
}

TEST_CASE("epd: an unturned glass sends the framebuffer inverted, byte for byte") {
    const parts::Ssd1681Glass fb = patterns()[2];
    models::Ssd1681 f;
    parts::Ssd1681 d = make(f);
    d.begin();

    d.present(fb, ports::Refresh::Full, 0);
    REQUIRE(f.ram.size() == parts::Ssd1681::kGlassBytes);
    bool inverted = true;
    for (size_t i = 0; i < parts::Ssd1681::kGlassBytes; i++)
        if (f.ram[i] != static_cast<uint8_t>(~fb.data()[i])) inverted = false;
    CHECK(inverted);
}

// One blocking SPI call per gate line was 425 calls a push and held the main loop 100-170 ms.
TEST_CASE("epd: a push sends each RAM bank as one SPI write, not one per gate line") {
    models::Ssd1681 f;
    parts::Ssd1681 d = make_turned(f);
    d.begin();
    parts::Ssd1681Glass fb;
    fb.clear(true);

    // 8 window + 2 border + 2 banks x (5 cursor + 1 command + 1 bank) + 3 activation
    const int kPushTransfers = 27;
    f.transfers = 0;
    d.present(fb, ports::Refresh::Full, 0);
    CHECK(f.transfers == kPushTransfers);
    settle(d, 0);

    f.transfers = 0;
    d.present(fb, ports::Refresh::Partial, 5000);
    CHECK(f.transfers == kPushTransfers);
    CHECK(d.ready(5000 + parts::Ssd1681::kReadyAfterPartialMs));

    f.transfers = 0;
    d.paint_black(10000);
    CHECK(f.transfers == kPushTransfers);
}
