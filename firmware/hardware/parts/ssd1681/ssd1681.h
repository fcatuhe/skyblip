#ifndef SKYBLIP_HARDWARE_PARTS_SSD1681_H
#define SKYBLIP_HARDWARE_PARTS_SSD1681_H

#include "hardware/io/io.h"
#include "hardware/parts/ssd1681/panel.h"
#include "ports/display.h"
#include "ui/canvas.h"

namespace skyblip::parts {

// INFO: fc 12sep26 the SSD1681 mirrors (0x01, 0x11) but cannot transpose, so the driver turns
enum class GlassRotation : uint8_t { Deg0, Deg270 };

class Ssd1681 : public ports::Display {
   public:
    Ssd1681(io::Spi& spi, io::Gpio& gpio, io::Delay& delay, int dc, int rst, int busy,
            int backlight = -1, GlassRotation rotation = GlassRotation::Deg0)
        : spi_(spi),
          gpio_(gpio),
          delay_(delay),
          dc_(dc),
          rst_(rst),
          busy_(busy),
          backlight_(backlight),
          rotation_(rotation) {}

    void begin();

    // Which panel the board found glued to it. The fingerprint read is the
    // board's, not the driver's: on silicon it needs the panel's one data line
    // reversed, which is only possible before a bus driver owns the pins
    // (hardware/parts/ssd1681/panel.h). Called before begin(); not calling it at
    // all leaves the identity Unknown, which is the safe policy.
    void adopt(const PanelSignature& signature) { panel_ = identify_panel(signature); }
    Panel panel() const { return panel_; }
    const char* panel_name() const { return parts::panel_name(panel_); }

    // INFO: fc 19sep26 SSD1681 drives 200 source x 200 gate lines, datasheet 6.1
    static constexpr int kGlassW = 200;
    static constexpr int kGlassH = 200;
    static constexpr int kGlassStride = (kGlassW + 7) / 8;
    static constexpr size_t kGlassBytes = static_cast<size_t>(kGlassStride) * kGlassH;

    static bool drives(const ui::Canvas& fb) {
        return fb.width() == kGlassW && fb.height() == kGlassH;
    }

    void present(const ui::Canvas& fb, ports::Refresh mode, uint32_t now_ms) override;
    void paint_black(uint32_t now_ms) override;
    bool ready(uint32_t now_ms) override;
    void power_off() override;
    void power_on() override { begin(); }
    void set_backlight(bool on) override;

    bool refreshing() const { return refreshing_; }
    ports::Refresh refresh_mode() const {
        return partial_refresh_ ? ports::Refresh::Partial : ports::Refresh::Full;
    }

    // INFO: fc 01aug25 D67 settles 460 ms partial / 2.5 s full, GxEPD2 | 12sep26 +140 ms power-down
    static constexpr uint32_t kReadyAfterPartialMs = 300;
    static constexpr uint32_t kReadyAfterFullMs = 1500;
    // INFO: fc 13sep26 GxEPD2 gives the D67 10 s: the cold waveform is slow, and this bounds a hang
    static constexpr uint32_t kBusyTimeoutMs = 10000;

   private:
    void init_panel();
    void ensure_awake();
    void activate(uint8_t sequence, bool full, uint32_t now_ms);
    void abort_refresh();
    void finish_refresh();
    void enter_sleep();
    const uint8_t* previous_bank(const ui::Canvas& fb, bool full) const;
    void cmd(uint8_t c);
    void data(uint8_t d);
    void write_bank(uint8_t command, const uint8_t* fb_bytes);
    void fill_bank(uint8_t command, uint8_t ram_value);
    uint8_t ram_byte(const uint8_t* fb_bytes, int gate, int column) const;
    void set_window(int x0, int y0, int x1, int y1);
    void set_cursor(int x, int y);
    bool wait_busy(uint32_t max_spins = 200000);

    io::Spi& spi_;
    io::Gpio& gpio_;
    io::Delay& delay_;
    int dc_, rst_, busy_, backlight_;
    GlassRotation rotation_;
    // INFO: fc 01aug25 the glass image, into bank 0x26 each present, so a partial diffs on truth
    uint8_t shadow_[kGlassBytes]{};
    bool glass_known_{false};
    bool asleep_{false};
    bool refreshing_{false};
    bool partial_refresh_{false};
    Panel panel_{Panel::Unknown};
    uint32_t ready_at_ms_{0};
    uint32_t timeout_at_ms_{0};
};

using Ssd1681Glass = ui::Panel<Ssd1681::kGlassW, Ssd1681::kGlassH>;

namespace epd {
// INFO: fc 04sep26 GxEPD2, Good Display, SoftRF hold RES# 10 ms; only this reset ends deep sleep
constexpr uint32_t kResetHoldUs = 10000;
}  // namespace epd

}

#endif
