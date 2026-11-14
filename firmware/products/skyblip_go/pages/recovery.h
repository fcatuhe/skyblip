#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RECOVERY_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_RECOVERY_H

#include "ports/dfu.h"
#include "products/skyblip_go/pages/installing.h"

namespace skyblip::go {

constexpr const char* kRecoveryTitle = "RECOVERY";
constexpr BodyLine kRecoveryRunning[] = {{"THE BOOTLOADER"},
                                         {"OPERATES NOW"},
                                         {"THE USB DRIVE IS"},
                                         {"TECHOBOOT"},
                                         {"COPY A .UF2 FILE"},
                                         {"TO IT"},
                                         {""},
                                         {"TO GO BACK, PUSH"},
                                         {"RESET"}};
constexpr int kRecoveryRunningRows = 9;
constexpr const BodyLine* kRecoveryBody = kRecoveryRunning + 2;
constexpr int kRecoveryBodyRows = kRecoveryRunningRows - 2;
constexpr const char* kRecoveryStartVerb = "START";

constexpr const char* recovery_button(ports::RecoveryPath path) {
    return path == ports::RecoveryPath::PowerOffToFinish ? kRecoveryStartVerb : "";
}

void draw_recovery(ui::Canvas& canvas, ports::RecoveryPath path);

}  // namespace skyblip::go

#endif
