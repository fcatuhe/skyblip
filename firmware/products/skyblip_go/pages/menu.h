#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_MENU_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_MENU_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/aircraft.h"
#include "products/skyblip_go/pages/frame.h"
#include "products/skyblip_go/pages/page.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

enum class MenuRow : uint8_t {
    AircraftType,
    Callsign,
    OwnIcao,
    Units,
    Range,
    Plot,
    Alarm,
    Volume,
    Status,
    Satellites,
    RadioLog,
    Raw,
    Capture,
    SelfTest,
    Light,
    Bravo,
    Aircraft,
    Detected,
    kCount
};

struct Menu {
    const MenuRow* rows{nullptr};
    int row_count{0};
};

Menu menu_for(Page page);
// INFO: fc 13nov26 the rows AIRCRAFT opens: what Bravo heard, then the three it sets
Menu aircraft_menu();

struct MenuValues {
    go::Settings settings{};
    bool light{false};
    Detected detected{};
};

struct MenuSnapshot {
    Page page{Page::Radar};
    MenuValues values{};
    MenuRow focus{MenuRow::Light};
    bool aircraft{false};
};

// Nine characters, which is what the settings blob has always held, and the ring
// a pad rolls through them: blank, dash, A to Z, 0 to 9, blank again.
constexpr int kCallsignCharacters = static_cast<int>(kCallsignBytes) - 1;
constexpr char kCallsignBlank = ' ';
char next_callsign_character(char character);
// The stored form: a name is edited nine wide and stored as what a pilot typed.
int callsign_stored(char* out, const char* edited);

constexpr int kOwnIcaoCharacters = 6;
static_assert(kOwnIcaoCharacters <= kCallsignCharacters,
              "the field edits in the callsign's buffer");
char next_hex_character(char character);
// INFO: fc 11nov26 kOwnIcaoUnset for six digits that are not all typed, or that spell 000000
uint32_t own_icao_typed(const char* edited);

struct AircraftType {
    uint8_t code;
    const char* name;
};

// INFO: cf 02aug26 ADS-L 4 SRD-860 issue 2 G.1.3 wire values: the code stored is transmitted
constexpr AircraftType kAircraftTypes[] = {
    {0, "UNKNOWN"},     {1, "LIGHT"},       {2, "HEAVY"},        {3, "LIGHT HELI"}, {4, "GLIDER"},
    {5, "BALLOON"},     {6, "MICROLIGHT"},  {7, "PARAGLIDER"},   {8, "SKYDIVER"},   {9, "VTOL"},
    {10, "GYROCOPTER"}, {15, "HEAVY HELI"}, {16, "HANG GLIDER"}, {17, "PARAMOTOR"}};
constexpr int kNamedAircraftTypes =
    static_cast<int>(sizeof(kAircraftTypes) / sizeof(kAircraftTypes[0]));
bool named_aircraft_type(uint8_t code);

enum class MenuAction : uint8_t { None, Moved, Changed, Open, Leave, Light };

const char* menu_row_label(MenuRow row);
const char* aircraft_type_name(uint8_t code);
uint8_t next_aircraft_type(uint8_t code);

constexpr bool opens_a_page(MenuRow row) {
    return row == MenuRow::RadioLog || row == MenuRow::Satellites || row == MenuRow::Status ||
           row == MenuRow::Raw || row == MenuRow::Capture || row == MenuRow::SelfTest ||
           row == MenuRow::Bravo;
}

Page page_behind(MenuRow row);

constexpr int kMenuValueBytes = 16;
int menu_row_value(char* out, MenuRow row, const MenuValues& values);

constexpr int kMenuScale = kFrameScale;
constexpr int kMenuLeftX = kFrameLeftX;
constexpr int kMenuCellWidth = kFrameCellWidth;
constexpr int kMenuRightX = kGlassWidth - kFrameLeftX;
constexpr int kMenuRowsTop = kOpenedRuleY + kBodyBelowRule;
constexpr int kMenuRowHeight = 20;
constexpr int kMenuBarHeight = kMenuRowHeight - 2;
constexpr int kMenuBarRadius = 3;
constexpr int kMenuTextInset = (kMenuBarHeight - ui::kCharacterHeight * kMenuScale) / 2;
constexpr int kMenuLines = (kBodyBottom - kMenuRowsTop) / kMenuRowHeight;

constexpr int menu_line_top(int line) { return kMenuRowsTop + line * kMenuRowHeight; }
constexpr int menu_line_text_y(int line) { return menu_line_top(line) + kMenuTextInset; }

int menu_row_index(const Menu& menu, MenuRow row);
int menu_first_row(const Menu& menu, MenuRow focus);

Keys menu_keys(const MenuSnapshot& snapshot);
void draw_menu(ui::Canvas& canvas, const MenuSnapshot& snapshot);

// The field screen the callsign and the own ICAO share: the characters at a size a thumb
// can check, and a bar under the one the pad is rolling.
struct CallsignSnapshot {
    const char* title{"CALLSIGN"};
    const char* text{""};
    int cursor{0};
    bool clears{false};
};

bool callsign_press_clears(const char* text, int cursor);

constexpr int kCallsignScale = 3;
constexpr int kCallsignCellWidth = ui::kCellWidth * kCallsignScale;
constexpr int field_text_x(int characters) {
    return (kGlassWidth - characters * kCallsignCellWidth) / 2;
}
constexpr int kCallsignTextX = field_text_x(kCallsignCharacters);
constexpr int kCallsignTextY = 80;
constexpr int kCallsignCursorY = kCallsignTextY + 7 * kCallsignScale + 4;
constexpr int kCallsignCursorHeight = 3;

constexpr const char* kFieldChangeVerb = "CHANGE";
constexpr const char* kFieldNextVerb = "NEXT";
constexpr const char* kFieldSaveVerb = "SAVE";
constexpr const char* kFieldEraseVerb = "ERASE";
constexpr const char* kFieldKeepVerb = "KEEP";

const char* field_button_verb(const CallsignSnapshot& snapshot);
void draw_callsign(ui::Canvas& canvas, const CallsignSnapshot& snapshot);

constexpr const char* kOwnIcaoClearLines[] = {"YOUR TRANSPONDER", "SHOWS AS", "TRAFFIC"};
constexpr int kOwnIcaoClearLineCount = 3;

struct ClearSnapshot {
    const char* title{""};
    const char* text{""};
};

void draw_clear(ui::Canvas& canvas, const ClearSnapshot& snapshot);

class MenuEditor {
   public:
    // INFO: cf 02aug26 a menu left open is the traffic picture taken away, and nobody dismissed it
    static constexpr uint32_t kIdleReturnHoldMs = 60000;

    void enter(Page page, uint32_t now_ms);
    void leave();

    bool active() const { return active_; }
    Page page() const { return page_; }
    bool aircraft() const { return aircraft_; }
    Menu menu() const;
    MenuRow focus() const;
    Page opening() const { return opening_; }

    // The two gestures, named after them: in the rows the pad moves the focus
    // and the button acts, in the callsign field the pad rolls a character and
    // the button steps to the next one.
    void button(uint32_t now_ms);
    void pad(uint32_t now_ms);

    bool editing() const { return editing_; }
    bool clearing() const { return clearing_; }
    MenuRow field() const { return field_; }
    const char* text() const { return text_; }
    int cursor() const { return cursor_; }

    MenuAction tick(uint32_t now_ms, const MenuValues& current, MenuValues& next);

   private:
    MenuAction act(const MenuValues& current, MenuValues& next);
    MenuAction advance();
    MenuAction use_detected(const MenuValues& current, MenuValues& next);
    void back_to_settings();
    void edit(MenuRow field, const MenuValues& current);
    int characters() const;
    MenuAction roll();
    MenuAction step(const MenuValues& current, MenuValues& next);
    MenuAction store(const MenuValues& current, MenuValues& next);
    MenuAction clear(const MenuValues& current, MenuValues& next);
    MenuAction confirm_clear(const MenuValues& current, MenuValues& next);
    MenuAction keep();
    void close_field();

    enum class Pending : uint8_t { None, Act, Advance };

    Page page_{Page::Radar};
    Page opening_{Page::Radar};
    int focus_{0};
    uint32_t idle_since_ms_{0};
    Pending pending_{Pending::None};
    bool active_{false};
    bool aircraft_{false};
    bool editing_{false};
    bool clearing_{false};
    MenuRow field_{MenuRow::Callsign};
    int cursor_{0};
    char text_[kCallsignBytes]{0};
};

}  // namespace skyblip::go

#endif
