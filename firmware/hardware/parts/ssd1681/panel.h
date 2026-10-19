// Which e-paper is actually glued to this board.
//
// LilyGO fits more than one panel behind the same footprint and the same
// controller pinout, and the panels do not behave the same. SoftRF fingerprints
// them by clocking registers 0x2D (11 bytes) and 0x2E (10 bytes) out of the
// panel over a bit-banged half-duplex SPI inside a critical section, and carries
// the signatures it has observed on real T-Echos in a comment block
// (platform/nRF52.cpp:3775-3900, table at 3855-3878). That table is transcribed
// below, byte for byte, including the two entries whose meaning it does not
// establish.
#ifndef SKYBLIP_HARDWARE_PARTS_SSD1681_PANEL_H
#define SKYBLIP_HARDWARE_PARTS_SSD1681_PANEL_H

#include <cstdint>

namespace skyblip::parts {

// The two registers, and the byte counts SoftRF reads from each.
constexpr uint8_t kPanelIdRegister2d = 0x2D;
constexpr uint8_t kPanelIdRegister2e = 0x2E;
constexpr int kPanelIdBytes2d = 11;
constexpr int kPanelIdBytes2e = 10;
constexpr int kPanelSignatureBytes = kPanelIdBytes2d + kPanelIdBytes2e;

struct PanelSignature {
    uint8_t register_2d[kPanelIdBytes2d]{};
    uint8_t register_2e[kPanelIdBytes2e]{};
    // False when the signature could not be taken at all. On silicon the read
    // needs the panel's data line reversed, which only the board port can do,
    // and a board that cannot do it says so here rather than reporting a panel.
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

// 0x2D: FF x11. 0x2E: 00 x10.
// GDEP015OC1, the first-generation panel, on a different controller: our init
// sequence is wrong for it, so finding one is a procurement alarm. It is NOT
// allowed to change what the driver does, because all-0xFF is also what an
// unwired read line returns and this board's e-paper MISO is a placeholder - see
// the note below the table.
constexpr PanelSignature kGdep015Oc1{
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    true};

constexpr PanelSignature kGdeh0154D67Syx1942{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x40, 0x00, 0x00, 0x00, 0x01},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    true};

// 2118 and 2129 are the same in 0x2D and differ only in 0x2E, which is why the
// ident needs both registers and why a table keyed on 0x2D alone would be wrong.
constexpr PanelSignature kGdeh0154D67Syx2118{
    {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00},
    {0x00, 0x05, 0x00, 0x9A, 0x00, 0x55, 0x35, 0x37, 0x14, 0x0C},
    true};

constexpr PanelSignature kGdeh0154D67Syx2129{
    {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    true};

// 0x2D: 00 x11. The one identification SoftRF's own code actually acts on
// (nRF52.cpp:3888-3897: all-zero 0x2D means DEPG0150BN, everything else falls
// back to D67).
constexpr PanelSignature kDepg0150Bn{
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, true};

// SoftRF's table calls this one "TBD (M1)": observed in an Elecrow ThinkNode M1,
// not named. Carried so a bench read that lands on it is recognised as a panel
// somebody has seen rather than as noise.
constexpr PanelSignature kElecrowM1{
    {0x00, 0x00, 0x40, 0x20, 0x10, 0x00, 0x40, 0x03, 0x00, 0x00, 0x00},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    true};

}  // namespace panels

inline bool panel_signature_equal(const PanelSignature& left, const PanelSignature& right) {
    for (int i = 0; i < kPanelIdBytes2d; i++)
        if (left.register_2d[i] != right.register_2d[i]) return false;
    for (int i = 0; i < kPanelIdBytes2e; i++)
        if (left.register_2e[i] != right.register_2e[i]) return false;
    return true;
}

inline Panel identify_panel(const PanelSignature& signature) {
    if (!signature.valid) return Panel::Unknown;
    if (panel_signature_equal(signature, panels::kGdeh0154D67Syx1942))
        return Panel::Gdeh0154D67Syx1942;
    if (panel_signature_equal(signature, panels::kGdeh0154D67Syx2118))
        return Panel::Gdeh0154D67Syx2118;
    if (panel_signature_equal(signature, panels::kGdeh0154D67Syx2129))
        return Panel::Gdeh0154D67Syx2129;
    if (panel_signature_equal(signature, panels::kDepg0150Bn)) return Panel::Depg0150Bn;
    if (panel_signature_equal(signature, panels::kGdep015Oc1)) return Panel::Gdep015Oc1;
    if (panel_signature_equal(signature, panels::kElecrowM1)) return Panel::ElecrowM1;
    return Panel::Unlisted;
}

// Short enough for the self-test row it is printed on: 200 px of 5x7 glass is 33
// characters wide and the row already carries a name and a verdict.
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

// Deliberately absent: anything that would let an identification REFUSE to
// drive the glass. GDEP015OC1 is a different controller and our init sequence is
// wrong for it, but its signature is all-0xFF, which is also what an unwired
// read line returns - and this board's e-paper MISO is a placeholder. A rule
// that withdrew the display on an all-0xFF read would blank the one page that
// can explain a dead device. An OC1 verdict is a procurement alarm to be read on
// the self-test page, not a runtime decision.

}  // namespace skyblip::parts

#endif
