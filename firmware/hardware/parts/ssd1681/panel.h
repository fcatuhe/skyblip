#ifndef SKYBLIP_HARDWARE_PARTS_SSD1681_PANEL_H
#define SKYBLIP_HARDWARE_PARTS_SSD1681_PANEL_H

#include <cstdint>

namespace skyblip::parts {

constexpr uint8_t kPanelIdRegister2d = 0x2D;
constexpr uint8_t kPanelIdRegister2e = 0x2E;
constexpr int kPanelIdBytes2d = 11;
constexpr int kPanelIdBytes2e = 10;
constexpr int kPanelIdBytes = kPanelIdBytes2d + kPanelIdBytes2e;

struct PanelId {
    uint8_t register_2d[kPanelIdBytes2d]{};
    uint8_t register_2e[kPanelIdBytes2e]{};
    // INFO: fc 20nov26 false when the ID could not be taken, since only the board port can reverse
    // the data line
    bool valid{false};
};

enum class Panel : uint8_t {
    Unknown,
    Unlisted,
    Gdeh0154D67Syx1942,
    Gdeh0154D67Syx2118,
    Gdeh0154D67Syx2129,
    Depg0150Bn,
    Gdep015Oc1,
    ElecrowM1,
};

namespace panels {

// INFO: fc 20nov26 an OC1 read never changes the driver, because an unwired read line also reads
// all 0xFF: README.md
constexpr PanelId kGdep015Oc1{{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
                              {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                              true};

constexpr PanelId kGdeh0154D67Syx1942{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x40, 0x00, 0x00, 0x00, 0x01},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    true};

// INFO: fc 20nov26 2118 and 2129 differ only in 0x2E, so the ident needs both registers
constexpr PanelId kGdeh0154D67Syx2118{
    {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00},
    {0x00, 0x05, 0x00, 0x9A, 0x00, 0x55, 0x35, 0x37, 0x14, 0x0C},
    true};

constexpr PanelId kGdeh0154D67Syx2129{
    {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    true};

constexpr PanelId kDepg0150Bn{
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, true};

// INFO: fc 20nov26 SoftRF's "TBD (M1)", seen in an Elecrow ThinkNode M1 and never named
constexpr PanelId kElecrowM1{{0x00, 0x00, 0x40, 0x20, 0x10, 0x00, 0x40, 0x03, 0x00, 0x00, 0x00},
                             {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                             true};

}  // namespace panels

inline bool panel_id_equal(const PanelId& left, const PanelId& right) {
    for (int i = 0; i < kPanelIdBytes2d; i++)
        if (left.register_2d[i] != right.register_2d[i]) return false;
    for (int i = 0; i < kPanelIdBytes2e; i++)
        if (left.register_2e[i] != right.register_2e[i]) return false;
    return true;
}

inline Panel identify_panel(const PanelId& id) {
    if (!id.valid) return Panel::Unknown;
    if (panel_id_equal(id, panels::kGdeh0154D67Syx1942)) return Panel::Gdeh0154D67Syx1942;
    if (panel_id_equal(id, panels::kGdeh0154D67Syx2118)) return Panel::Gdeh0154D67Syx2118;
    if (panel_id_equal(id, panels::kGdeh0154D67Syx2129)) return Panel::Gdeh0154D67Syx2129;
    if (panel_id_equal(id, panels::kDepg0150Bn)) return Panel::Depg0150Bn;
    if (panel_id_equal(id, panels::kGdep015Oc1)) return Panel::Gdep015Oc1;
    if (panel_id_equal(id, panels::kElecrowM1)) return Panel::ElecrowM1;
    return Panel::Unlisted;
}

// INFO: fc 20nov26 short enough for the 33-character self-test row, which also carries a name and a
// verdict
constexpr const char* panel_name(Panel panel) {
    switch (panel) {
        case Panel::Gdeh0154D67Syx1942: return "D67/1942";
        case Panel::Gdeh0154D67Syx2118: return "D67/2118";
        case Panel::Gdeh0154D67Syx2129: return "D67/2129";
        case Panel::Depg0150Bn: return "DEPG0150";
        case Panel::Gdep015Oc1: return "OC1";
        case Panel::ElecrowM1: return "M1";
        case Panel::Unlisted: return "UNLISTED";
        case Panel::Unknown: break;
    }
    return "NO ID";
}

}  // namespace skyblip::parts

#endif
