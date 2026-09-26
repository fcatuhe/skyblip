#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RECOVERY_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RECOVERY_H

#include "ports/dfu.h"
#include "products/skyblip_go/pages/installing.h"

namespace skyblip::go {

constexpr const char* kRecoveryHeader = "RECOVERY";
constexpr const char* kRecoveryTitle = "USB BOOTLOADER";
constexpr const char* kRecoveryRunning = "RUNNING NOW";
constexpr const char* kRecoveryAwaitsPress = "PRESS THE BUTTON TO START IT";
constexpr const char* kRecoveryBody[] = {
    "ON USB: A DRIVE NAMED TECHOBOOT", "DROP A .UF2 THERE: NEW FIRMWARE",
    "PRESS RST: BACK TO SKYBLIP", "NOTHING TRANSMITS MEANWHILE"};
constexpr int kRecoveryBodyRows = 4;

constexpr const char* recovery_entry(ports::RecoveryPath path) {
    return path == ports::RecoveryPath::PowerOffToFinish ? kRecoveryAwaitsPress : kRecoveryRunning;
}

void draw_recovery(ui::Canvas& fb, ports::RecoveryPath path);

}  // namespace skyblip::go

#endif
