#ifndef SKYBLIP_HARDWARE_MODEL_SSD1681_H
#define SKYBLIP_HARDWARE_MODEL_SSD1681_H

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "hardware/io/io.h"
#include "hardware/parts/ssd1681/panel.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "ports/clock.h"
#include "ui/canvas.h"

namespace skyblip::models {

class Ssd1681 : public io::Spi, public io::Gpio, public io::Delay {
   public:
    int dc_pin{0}, reset_pin{1}, busy_pin{2}, backlight_pin{3};

    // INFO: fc 13sep26 GxEPD2_154_D67.h: partial_refresh_time 500, full_refresh_time 2600
    static constexpr uint32_t kPartialBusyMs = 500;
    static constexpr uint32_t kFullBusyMs = 2600;
    // INFO: fc 09mar26 the tail GxEPD2 counts for the power-down, and a rails-up sequence skips it
    static constexpr uint32_t kPowerDownMs = 140;

    // INFO: fc 26sep26 the vendor 10 ms RES# hold as a literal: epd::kResetHoldUs is on trial
    static constexpr uint32_t kResetLowFloorUs = 10000;

    void attach_clock(const ports::Clock& clock) { clock_ = &clock; }

    bool refreshing() const {
        if (clock_ == nullptr || refresh_span_ms_ == 0) return false;
        return clock_->milliseconds() - refresh_since_ms_ < refresh_span_ms_;
    }

    void set(int pin, bool level) override {
        if (pin == dc_pin) dc_high_ = level;
        if (pin == backlight_pin) backlight = level;
        if (pin == reset_pin) {
            if (!level && reset_level_) reset_low_since_us_ = elapsed_us;
            if (level && !reset_level_) release_reset();
            reset_level_ = level;
        }
    }
    bool get(int pin) override {
        if (pin != busy_pin) return false;
        return busy_stuck || refreshing();
    }
    void mode_output(int) override {}
    void mode_input(int, bool) override {}
    void wait_at_least_us(uint32_t hold_us) override { elapsed_us += hold_us; }

    void select(bool) override {}
    void transfer(const uint8_t* tx, uint8_t* rx, size_t bytes) override {
        transfers++;
        for (size_t i = 0; i < bytes; i++) {
            uint8_t byte = tx ? tx[i] : 0;
            if (rx) rx[i] = 0;
            if (!dc_high_) {
                // INFO: fc 13sep26 a command over a live BUSY aborts the waveform mid-pixel
                if (refreshing()) commands_while_busy++;
                commands.push_back(byte);
                if (byte == kWriteRam) ram.clear();
                if (byte == kWriteRamPrevious) ram_previous.clear();
                if (byte == kDeepSleep) {
                    powered = false;
                    rails_on = false;
                    deep_sleeps++;
                }
                if (byte == kMasterActivation) {
                    rails_on =
                        powered && (sequence_ & kEnableAnalog) && !(sequence_ & kDisableAnalog);
                    if (sequence_ & kDisplay) {
                        present_count++;
                        if (clock_ != nullptr) {
                            refresh_since_ms_ = clock_->milliseconds();
                            refresh_span_ms_ = last_full ? kFullBusyMs : partial_busy_ms();
                        }
                        // In deep sleep the panel's charge pump is off: it latches
                        // nothing, and keeps the last image it did latch.
                        if (powered) rasterise();
                    }
                }
                pending_opcode_ = byte;
            } else {
                if (pending_opcode_ == kWriteRam) ram.push_back(byte);
                if (pending_opcode_ == kWriteRamPrevious) ram_previous.push_back(byte);
                if (pending_opcode_ == kDisplayUpdateControl2) {
                    sequence_ = byte;
                    if (byte & kDisplay) last_full = !(byte & kLoadLutMode2);
                }
                if (pending_opcode_ == kBorderWaveform) border = byte;
            }
        }
    }

    bool saw_command(uint8_t opcode) const {
        return std::any_of(commands.begin(), commands.end(),
                           [opcode](uint8_t seen) { return seen == opcode; });
    }

    // What the panel would be showing: RAM read back through the driver's own
    // inversion, so a polarity bug in the driver shows up as an inverted screen
    // in the simulator instead of passing unnoticed.
    const parts::Ssd1681Glass& framebuffer() const { return panel_; }

    bool save_pgm(const char* path) const {
        FILE* file = std::fopen(path, "wb");
        if (!file) return false;
        std::fprintf(file, "P5\n%d %d\n255\n", parts::Ssd1681::kGlassWidth,
                     parts::Ssd1681::kGlassHeight);
        for (int y = 0; y < parts::Ssd1681::kGlassHeight; y++)
            for (int x = 0; x < parts::Ssd1681::kGlassWidth; x++) {
                uint8_t shade = panel_.get_pixel(x, y) ? 0 : 255;
                std::fwrite(&shade, 1, 1, file);
            }
        std::fclose(file);
        return true;
    }

    // Which lot the virtual glass is from. Not served over the SPI seam, because
    // on real hardware it is not readable there either: the signature needs the
    // panel's single data line reversed, so the board port takes it before any
    // bus driver owns the pins and the platform hands it over. The default is a
    // signature that was never taken, which is what this board is expected to
    // report until a bench read exists.
    parts::PanelSignature signature{};

    std::vector<uint8_t> commands;
    std::vector<uint8_t> ram;
    std::vector<uint8_t> ram_previous;
    int reset_pulses{0};
    int short_resets{0};
    uint64_t reset_low_us{0};
    uint64_t elapsed_us{0};
    int present_count{0};
    int transfers{0};
    int deep_sleeps{0};
    int commands_while_busy{0};
    bool busy_stuck{false};
    bool powered{true};
    bool rails_on{false};
    uint8_t border{0};
    bool backlight{false};
    bool last_full{true};

   private:
    static constexpr uint8_t kWriteRam = 0x24;
    static constexpr uint8_t kWriteRamPrevious = 0x26;
    static constexpr uint8_t kMasterActivation = 0x20;
    static constexpr uint8_t kDisplayUpdateControl2 = 0x22;
    static constexpr uint8_t kBorderWaveform = 0x3C;
    static constexpr uint8_t kDeepSleep = 0x10;
    static constexpr uint8_t kEnableAnalog = 0x40;
    static constexpr uint8_t kLoadLutMode2 = 0x08;
    static constexpr uint8_t kDisplay = 0x04;
    static constexpr uint8_t kDisableAnalog = 0x02;

    void release_reset() {
        reset_low_us = elapsed_us - reset_low_since_us_;
        if (reset_low_us < kResetLowFloorUs) {
            short_resets++;
            return;
        }
        reset_pulses++;
        powered = true;
        rails_on = false;
        refresh_span_ms_ = 0;
    }

    uint32_t partial_busy_ms() const {
        return (sequence_ & kDisableAnalog) ? kPartialBusyMs : kPartialBusyMs - kPowerDownMs;
    }

    void rasterise() {
        if (ram.size() < parts::Ssd1681::kGlassBytes) return;
        uint8_t* out = panel_.data();
        for (size_t i = 0; i < parts::Ssd1681::kGlassBytes; i++)
            out[i] = static_cast<uint8_t>(~ram[i]);
    }

    const ports::Clock* clock_{nullptr};
    uint32_t refresh_since_ms_{0};
    uint32_t refresh_span_ms_{0};
    parts::Ssd1681Glass panel_{};
    uint8_t sequence_{0};
    bool dc_high_{false};
    bool reset_level_{true};
    uint64_t reset_low_since_us_{0};
    uint8_t pending_opcode_{0};
};

}  // namespace skyblip::models

#endif
