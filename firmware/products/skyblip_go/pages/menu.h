#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_MENU_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_MENU_H

#include <cstdint>
#include <optional>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/aircraft.h"
#include "products/skyblip_go/pages/frame.h"
#include "products/skyblip_go/pages/page.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

enum class MenuRow : uint8_t {
    AircraftType,
    Callsign,
    AircraftAddress,
    Units,
    Range,
    Plot,
    Alarm,
    Status,
    Satellites,
    RadioLog,
    SelfTest,
    Light,
    Bravo,
    Aircraft,
    Detected,
    BravoLog,
    Power,
    Flarm,
    FlarmHeard,
    FlarmId,
    kCount
};

struct Menu {
    const MenuRow* rows{nullptr};
    int row_count{0};
};

Menu menu_for(Page page);
// INFO: fc 13nov26 the rows AIRCRAFT opens: what Bravo heard, then the three it sets
Menu aircraft_menu();
// INFO: fc 27nov26 the rows FLARM opens: what the Go hears aboard, then the ID it sets
Menu flarm_menu();

constexpr bool hex_field(MenuRow row) {
    return row == MenuRow::AircraftAddress || row == MenuRow::FlarmId;
}

struct MenuValues {
    go::Settings settings{};
    bool light{false};
    Detected detected{};
    // INFO: fc 14nov26 bus::State::own_transponder, the one an aircraft address typed must match
    std::optional<uint32_t> own_transponder{};
    // INFO: fc 15nov26 the Go has a buzzer or a haptic, so its alarm can be soft or loud
    bool sounds{true};
    FlarmDetected flarm{};
    // INFO: fc 27nov26 the FLARM ID the Go hears aboard, the one a FLARM ID typed must match
    std::optional<uint32_t> own_flarm{};
};

struct MenuSnapshot {
    Page page{Page::Radar};
    MenuValues values{};
    MenuRow focus{MenuRow::Light};
    bool aircraft{false};
    bool flarm{false};
};

constexpr int kCallsignCharacters = static_cast<int>(kCallsignBytes) - 1;
constexpr char kCallsignBlank = ' ';
char next_callsign_character(char character);
// INFO: fc 20nov26 a name is edited nine wide and stored without its trailing blanks
int callsign_stored(char* out, const char* edited);

constexpr int kAircraftAddressCharacters = 6;
static_assert(kAircraftAddressCharacters <= kCallsignCharacters,
              "the field edits in the callsign's buffer");
char next_hex_character(char character);
// INFO: fc 11nov26 kAircraftAddressUnset for six digits that are not all typed, or that spell
// 000000
uint32_t hex_typed(const char* edited);

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
           row == MenuRow::SelfTest || row == MenuRow::Bravo || row == MenuRow::BravoLog;
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

struct CallsignSnapshot {
    const char* title{"CALLSIGN"};
    const char* text{""};
    int cursor{0};
    bool clears{false};
    // INFO: fc 27nov26 what a unit hears aboard, the one value the field accepts
    std::optional<uint32_t> heard{};
    const char* heard_text{""};
    bool refused{false};
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
constexpr int kOwnTransponderY = kCallsignCursorY + kCallsignCursorHeight + 8;
constexpr const char* kOwnTransponderText = "BRAVO HEARS";
constexpr const char* kOwnFlarmText = "GO HEARS";
constexpr const char* kOwnTransponderRefusedText = "NOT ACCEPTED";

constexpr const char* kFieldChangeVerb = "CHANGE";
constexpr const char* kFieldNextVerb = "NEXT";
constexpr const char* kFieldSaveVerb = "SAVE";
constexpr const char* kFieldEraseVerb = "ERASE";
constexpr const char* kFieldKeepVerb = "KEEP";

const char* field_button_verb(const CallsignSnapshot& snapshot);
void draw_callsign(ui::Canvas& canvas, const CallsignSnapshot& snapshot);

constexpr const char* kAircraftAddressClearLines[] = {"YOUR TRANSPONDER", "SHOWS AS", "TRAFFIC"};
constexpr int kAircraftAddressClearLineCount = 3;
constexpr const char* kFlarmClearOwner = "YOUR FLARM";

struct ClearSnapshot {
    const char* title{""};
    const char* text{""};
    const char* owner{kAircraftAddressClearLines[0]};
    uint32_t seconds_left{0};
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
    bool aircraft() const { return submenu_ == Submenu::Aircraft; }
    bool flarm() const { return submenu_ == Submenu::Flarm; }
    Menu menu() const;
    MenuRow focus() const;
    Page opening() const { return opening_; }

    void button(uint32_t now_ms);
    void pad(uint32_t now_ms);

    bool editing() const { return editing_; }
    bool clearing() const { return clearing_; }
    uint32_t clear_seconds_left(uint32_t now_ms) const;
    bool refused() const { return refused_; }
    MenuRow field() const { return field_; }
    const char* text() const { return text_; }
    int cursor() const { return cursor_; }

    MenuAction tick(uint32_t now_ms, const MenuValues& current, MenuValues& next);

   private:
    MenuAction act(const MenuValues& current, MenuValues& next);
    MenuAction advance();
    MenuAction use_detected(const MenuValues& current, MenuValues& next);
    MenuAction use_flarm(const MenuValues& current, MenuValues& next);
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
    enum class Submenu : uint8_t { None, Aircraft, Flarm };

    Page page_{Page::Radar};
    Page opening_{Page::Radar};
    int focus_{0};
    uint32_t idle_since_ms_{0};
    Pending pending_{Pending::None};
    bool active_{false};
    Submenu submenu_{Submenu::None};
    bool editing_{false};
    bool clearing_{false};
    bool refused_{false};
    MenuRow field_{MenuRow::Callsign};
    int cursor_{0};
    char text_[kCallsignBytes]{0};
};

}  // namespace skyblip::go

#endif
