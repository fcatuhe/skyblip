#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_MENU_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_MENU_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/page.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

enum class MenuRow : uint8_t {
    AircraftType,
    Callsign,
    Units,
    Range,
    Plot,
    Alarm,
    Volume,
    Status,
    Sats,
    RadioLog,
    Raw,
    Capture,
    SelfTest,
    kCount
};

struct Menu {
    const MenuRow* rows{nullptr};
    int n{0};
};

Menu menu_for(Page page);

struct MenuValues {
    go::Settings settings{};
    int range_step{kDefaultRangeStep};
    RadarPlot plot{RadarPlot::ToScale};
};

struct MenuSnapshot {
    Page page{Page::Radar};
    MenuValues values{};
    MenuRow focus{MenuRow::AircraftType};
};

constexpr uint8_t kMaxAlarmVolume = 5;

// Nine characters, which is what the settings blob has always held, and the ring
// a pad rolls through them: blank, dash, A to Z, 0 to 9, blank again.
constexpr int kCallsignChars = static_cast<int>(kCallsignCap) - 1;
constexpr char kCallsignBlank = ' ';
char next_callsign_char(char c);
// The stored form: a name is edited nine wide and stored as what a pilot typed.
int callsign_stored(char* out, const char* edited);

// INFO: fc 12sep26 ADS-L G.1.3 codes 11 up are UAV and reserved, nothing a pilot sits in
constexpr uint8_t kNamedAircraftTypes = 11;

enum class MenuAction : uint8_t { None, Moved, Changed, Open, Leave };

const char* menu_row_label(MenuRow row);
const char* aircraft_type_name(uint8_t code);
uint8_t next_aircraft_type(uint8_t code);

constexpr bool opens_a_page(MenuRow row) {
    return row == MenuRow::RadioLog || row == MenuRow::Sats || row == MenuRow::Status ||
           row == MenuRow::Raw || row == MenuRow::Capture || row == MenuRow::SelfTest;
}

Page page_behind(MenuRow row);

constexpr int kMenuValueCap = 16;
int menu_row_value(char* out, MenuRow row, const MenuValues& values);

constexpr int kMenuScale = 2;
constexpr int kMenuLeftX = 6;
constexpr int kSmallCellW = 6;
constexpr int kMenuCellW = kSmallCellW * kMenuScale;
constexpr int kMenuRightX = 194;
constexpr int kMenuRowsTop = 23;
constexpr int kMenuRowHeight = 23;
constexpr int kMenuBarH = kMenuRowHeight - 1;
constexpr int kMenuTextInset = (kMenuBarH - 7 * kMenuScale) / 2;
constexpr int kMenuHintY = 188;
constexpr const char* kMenuHintText = "PAD MOVES    BUTTON CHANGES";

constexpr int text_cells(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

constexpr int kMenuHintX = (kGlassW - text_cells(kMenuHintText) * kSmallCellW) / 2;

constexpr int menu_line_top(int line) { return kMenuRowsTop + line * kMenuRowHeight; }
constexpr int menu_line_text_y(int line) { return menu_line_top(line) + kMenuTextInset; }

int menu_row_index(const Menu& menu, MenuRow row);

void draw_menu(ui::Canvas& fb, const MenuSnapshot& snapshot);

// The one field on this device, so it is a screen and not a row: the characters
// at a size a thumb can check, and a bar under the one the pad is rolling.
struct CallsignSnapshot {
    const char* text{""};
    int cursor{0};
    bool clears{false};
};

bool callsign_press_clears(const char* text, int cursor);

constexpr int kCallsignScale = 3;
constexpr int kCallsignCellW = kSmallCellW * kCallsignScale;
constexpr int kCallsignTextX = (kGlassW - kCallsignChars * kCallsignCellW) / 2;
constexpr int kCallsignTextY = 80;
constexpr int kCallsignCursorY = kCallsignTextY + 7 * kCallsignScale + 4;
constexpr int kCallsignCursorH = 3;
constexpr int kCallsignHintScale = 2;
constexpr const char* kCallsignPadHintText = "PAD ROLLS";
constexpr const char* kCallsignNextHintText = "BUTTON NEXT";
constexpr const char* kCallsignClearHintText = "BUTTON CLEARS";
constexpr const char* kCallsignClearHelpText = "BLANK THE FIRST TO CLEAR";

constexpr int centred_x(const char* text, int scale) {
    return (kGlassW - text_cells(text) * kSmallCellW * scale) / 2;
}

constexpr int kCallsignPadHintY = 138;
constexpr int kCallsignButtonHintY = kCallsignPadHintY + 7 * kCallsignHintScale + 6;
constexpr int kCallsignHelpY = kMenuHintY;

void draw_callsign(ui::Canvas& fb, const CallsignSnapshot& snapshot);

class MenuEditor {
   public:
    // INFO: cf 02aug26 a menu left open is the traffic picture taken away, and nobody dismissed it
    static constexpr uint32_t kIdleReturnMs = 60000;

    void enter(Page page, uint32_t now_ms);
    void leave();

    bool active() const { return active_; }
    Page page() const { return page_; }
    MenuRow focus() const;
    Page opening() const { return opening_; }

    // The two gestures, named after them: in the rows the pad moves the focus
    // and the button acts, in the callsign field the pad rolls a character and
    // the button steps to the next one.
    void button(uint32_t now_ms);
    void pad(uint32_t now_ms);

    bool editing() const { return editing_; }
    const char* text() const { return text_; }
    int cursor() const { return cursor_; }

    MenuAction tick(uint32_t now_ms, const MenuValues& current, MenuValues& next);

   private:
    MenuAction act(const MenuValues& current, MenuValues& next);
    MenuAction advance();
    void edit(const MenuValues& current);
    MenuAction roll();
    MenuAction step(const MenuValues& current, MenuValues& next);
    MenuAction clear(const MenuValues& current, MenuValues& next);

    enum class Pending : uint8_t { None, Act, Advance };

    Page page_{Page::Radar};
    Page opening_{Page::Radar};
    int focus_{0};
    uint32_t idle_since_ms_{0};
    Pending pending_{Pending::None};
    bool active_{false};
    bool editing_{false};
    int cursor_{0};
    char text_[kCallsignCap]{0};
};

}  // namespace skyblip::go

#endif
