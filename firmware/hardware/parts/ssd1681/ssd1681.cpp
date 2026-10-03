// GDEH0154D67 panel on the SSD1681 controller, over io::Spi / io::Gpio only.
#include "hardware/parts/ssd1681/ssd1681.h"

#include <cstring>

namespace skyblip::parts {

namespace {
constexpr uint8_t kDriverOutputCtrl = 0x01;
constexpr uint8_t kDataEntryMode = 0x11;
constexpr uint8_t kSwReset = 0x12;
constexpr uint8_t kTempSensorCtrl = 0x18;
constexpr uint8_t kMasterActivation = 0x20;
constexpr uint8_t kDisplayUpdateCtrl2 = 0x22;
constexpr uint8_t kWriteRam = 0x24;
constexpr uint8_t kWriteRamPrevious = 0x26;
constexpr uint8_t kBorderWaveform = 0x3C;
constexpr uint8_t kSetRamXAddr = 0x44;
constexpr uint8_t kSetRamYAddr = 0x45;
constexpr uint8_t kSetRamXCounter = 0x4E;
constexpr uint8_t kSetRamYCounter = 0x4F;
constexpr uint8_t kDeepSleep = 0x10;
constexpr uint8_t kDeepSleepRetainRam = 0x01;

constexpr uint8_t kSequenceFull = 0xF7;
// INFO: fc 12sep26 0xFF ends a partial with the rails down, as Waveshare's own 0xCF does
constexpr uint8_t kSequencePartial = 0xFF;
// INFO: fc 09mar26 0xFF less its two power-down bits: the frame behind a wipe reuses the rails
constexpr uint8_t kSequenceWipe = 0xFC;

// INFO: fc 09mar26 VBD follows LUT1 at 0x05 and greys over a run of partials; 0x80 holds it at VCOM
constexpr uint8_t kBorderFollowLut1 = 0x05;
constexpr uint8_t kBorderVcom = 0x80;

constexpr int kW = Ssd1681::kGlassW;
constexpr int kH = Ssd1681::kGlassH;
constexpr int kStride = Ssd1681::kGlassStride;

// INFO: fc 01aug25 panel RAM is 1=white, the framebuffer 1=black
constexpr uint8_t kRamWhite = 0xFF;
constexpr uint8_t kRamBlack = 0x00;

// INFO: fc 03oct26 Hacker's Delight transpose8: byte k out is bit k of each byte in, MSB first
void turn_block(const uint8_t* rows, uint8_t* gates) {
    uint32_t x = 0;
    uint32_t y = 0;
    for (size_t i = 0; i < 4; i++) {
        x = (x << 8) | rows[i * kStride];
        y = (y << 8) | rows[(i + 4) * kStride];
    }
    uint32_t t = (x ^ (x >> 7)) & 0x00AA00AAu;
    x ^= t ^ (t << 7);
    t = (y ^ (y >> 7)) & 0x00AA00AAu;
    y ^= t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000CCCCu;
    x ^= t ^ (t << 14);
    t = (y ^ (y >> 14)) & 0x0000CCCCu;
    y ^= t ^ (t << 14);
    t = (x & 0xF0F0F0F0u) | ((y >> 4) & 0x0F0F0F0Fu);
    y = ((x << 4) & 0xF0F0F0F0u) | (y & 0x0F0F0F0Fu);
    x = t;
    for (size_t k = 0; k < 4; k++) {
        gates[(7 - k) * kStride] = static_cast<uint8_t>(~(x >> (24 - 8 * k)));
        gates[(3 - k) * kStride] = static_cast<uint8_t>(~(y >> (24 - 8 * k)));
    }
}
}  // namespace

void Ssd1681::begin() {
    gpio_.mode_output(dc_);
    gpio_.mode_output(rst_);
    gpio_.mode_input(busy_, false);
    init_panel();
    glass_known_ = false;
    refreshing_ = false;
    asleep_ = false;
}

void Ssd1681::present(const ui::Canvas& fb, ports::Refresh mode, uint32_t now_ms) {
    if (!drives(fb)) return;
    ensure_awake();

    const bool full = mode == ports::Refresh::Full || !glass_known_;

    set_window(0, 0, kW - 1, kH - 1);
    cmd(kBorderWaveform);
    data(full ? kBorderFollowLut1 : kBorderVcom);
    if (full) {
        // INFO: fc 09mar26 GxEPD2 writes the new frame into both banks before a full refresh
        stage(fb.data());
        write_bank(kWriteRamPrevious);
    } else {
        write_bank(kWriteRamPrevious);
        stage(fb.data());
    }
    write_bank(kWriteRam);

    activate(full ? kSequenceFull : kSequencePartial, full, now_ms);
}

// INFO: fc 13sep26 the white previous is a drive, not a claim: every pixel lands black
void Ssd1681::paint_black(uint32_t now_ms) {
    ensure_awake();

    set_window(0, 0, kW - 1, kH - 1);
    cmd(kBorderWaveform);
    data(kBorderVcom);
    std::memset(shadow_, kRamWhite, sizeof(shadow_));
    write_bank(kWriteRamPrevious);
    std::memset(shadow_, kRamBlack, sizeof(shadow_));
    write_bank(kWriteRam);

    activate(kSequenceWipe, /*full=*/false, now_ms);
}

void Ssd1681::ensure_awake() {
    if (refreshing_) abort_refresh();
    if (!asleep_) return;
    init_panel();
    asleep_ = false;
}

void Ssd1681::activate(uint8_t sequence, bool full, uint32_t now_ms) {
    cmd(kDisplayUpdateCtrl2);
    data(sequence);
    cmd(kMasterActivation);

    glass_known_ = true;
    refreshing_ = true;
    partial_refresh_ = !full;
    ready_at_ms_ = now_ms + (full ? kReadyAfterFullMs : kReadyAfterPartialMs);
    timeout_at_ms_ = now_ms + kBusyTimeoutMs;
}

bool Ssd1681::ready(uint32_t now_ms) {
    if (!refreshing_) return true;
    if (static_cast<int32_t>(now_ms - ready_at_ms_) < 0) return false;
    if (gpio_.get(busy_)) {
        if (static_cast<int32_t>(now_ms - timeout_at_ms_) < 0) return false;
        init_panel();
        glass_known_ = false;
        refreshing_ = false;
        asleep_ = false;
        return true;
    }
    finish_refresh();
    return true;
}

void Ssd1681::power_off() {
    if (refreshing_) abort_refresh();
    if (!wait_busy()) glass_known_ = false;
    if (!asleep_) enter_sleep();
}

// INFO: fc 13sep26 RES# is the only abort the glass has, and only a full waveform undoes the ink
void Ssd1681::abort_refresh() {
    refreshing_ = false;
    if (wait_busy()) return;
    init_panel();
    asleep_ = false;
    glass_known_ = false;
}

void Ssd1681::set_backlight(bool on) {
    if (backlight_ < 0) return;
    gpio_.mode_output(backlight_);
    gpio_.set(backlight_, on);
}

void Ssd1681::init_panel() {
    gpio_.set(rst_, true);
    gpio_.set(rst_, false);
    delay_.wait_at_least_us(epd::kResetHoldUs);
    gpio_.set(rst_, true);
    delay_.wait_at_least_us(epd::kResetHoldUs);
    wait_busy();

    cmd(kSwReset);
    wait_busy();

    cmd(kDriverOutputCtrl);
    data(0xC7);  // 200 - 1 gates
    data(0x00);
    data(0x00);

    cmd(kDataEntryMode);
    data(0x03);  // X inc, Y inc

    set_window(0, 0, kW - 1, kH - 1);

    cmd(kBorderWaveform);
    data(kBorderFollowLut1);

    // INFO: fc 01aug25 internal sensor selects the temperature-compensated OTP LUT
    cmd(kTempSensorCtrl);
    data(0x80);

    set_cursor(0, 0);
    wait_busy();
}

void Ssd1681::finish_refresh() { refreshing_ = false; }

void Ssd1681::enter_sleep() {
    cmd(kDeepSleep);
    data(kDeepSleepRetainRam);
    asleep_ = true;
}

void Ssd1681::cmd(uint8_t c) {
    gpio_.set(dc_, false);
    spi_.select(true);
    spi_.transfer(&c, nullptr, 1);
    spi_.select(false);
}

void Ssd1681::data(uint8_t d) {
    gpio_.set(dc_, true);
    spi_.select(true);
    spi_.transfer(&d, nullptr, 1);
    spi_.select(false);
}

void Ssd1681::write_bank(uint8_t command) {
    set_cursor(0, 0);
    cmd(command);
    gpio_.set(dc_, true);
    spi_.select(true);
    spi_.transfer(shadow_, nullptr, kGlassBytes);
    spi_.select(false);
}

void Ssd1681::stage(const uint8_t* fb_bytes) {
    if (rotation_ == GlassRotation::Deg0) {
        for (size_t i = 0; i < kGlassBytes; i++) shadow_[i] = static_cast<uint8_t>(~fb_bytes[i]);
    } else {
        for (size_t band = 0; band < kStride; band++)
            for (size_t column = 0; column < kStride; column++)
                turn_block(fb_bytes + band * 8 * kStride + column,
                           shadow_ + (kH - 8 - column * 8) * kStride + band);
    }
}

void Ssd1681::set_window(int x0, int y0, int x1, int y1) {
    cmd(kSetRamXAddr);
    data(static_cast<uint8_t>(x0 / 8));
    data(static_cast<uint8_t>(x1 / 8));
    cmd(kSetRamYAddr);
    data(static_cast<uint8_t>(y0));
    data(static_cast<uint8_t>(y0 >> 8));
    data(static_cast<uint8_t>(y1));
    data(static_cast<uint8_t>(y1 >> 8));
}

void Ssd1681::set_cursor(int x, int y) {
    cmd(kSetRamXCounter);
    data(static_cast<uint8_t>(x / 8));
    cmd(kSetRamYCounter);
    data(static_cast<uint8_t>(y));
    data(static_cast<uint8_t>(y >> 8));
}

bool Ssd1681::wait_busy(uint32_t max_spins) {
    for (uint32_t i = 0; i < max_spins; i++) {
        if (!gpio_.get(busy_)) return true;
    }
    return false;
}

}  // namespace skyblip::parts
