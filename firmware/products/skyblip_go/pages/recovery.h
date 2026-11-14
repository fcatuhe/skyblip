#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RECOVERY_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RECOVERY_H

#include "ports/dfu.h"
#include "products/skyblip_go/pages/installing.h"

namespace skyblip::go {

constexpr const char* kRecoveryHeader = "RECOVERY";
constexpr const char* kRecoveryTitle = "USB BOOTLOADER";
constexpr const char* kRecoveryRunning = "THE BOOTLOADER OPERATES NOW.";
constexpr const char* kRecoveryAwaitsPress = "PUSH THE BUTTON TO START IT.";
constexpr const char* kRecoveryBody[] = {
    "THE USB DRIVE NAME IS TECHOBOOT.", "COPY A .UF2 FIRMWARE FILE TO IT.",
    "TO GO BACK TO SKYBLIP,", "PUSH THE RESET BUTTON.", "THE RADIO DOES NOT TRANSMIT."};
constexpr int kRecoveryBodyRows = 5;

constexpr const char* recovery_entry(ports::RecoveryPath path) {
    return path == ports::RecoveryPath::PowerOffToFinish ? kRecoveryAwaitsPress : kRecoveryRunning;
}

void draw_recovery(ui::Canvas& canvas, ports::RecoveryPath path);

}  // namespace skyblip::go

#endif
