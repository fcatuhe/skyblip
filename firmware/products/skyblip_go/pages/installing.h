#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_INSTALLING_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_INSTALLING_H

#include "products/skyblip_go/glass.h"

namespace skyblip::go {

constexpr int kInstallingLeftX = 4;
constexpr int kInstallingTitleY = 40;
constexpr int kInstallingLineHeight = 14;
constexpr int kInstallingBodyY = 90;

constexpr const char* kInstallingHeader = "FIRMWARE";
constexpr const char* kInstallingTitle = "INSTALLATION";
constexpr const char* kInstallingBody[] = {"KEEP THE DEVICE ON.", "IT STARTS AGAIN IN",
                                           "APPROXIMATELY 30 S.", "THE DISPLAY DOES NOT CHANGE."};
constexpr int kInstallingBodyRows = 4;

constexpr const char* kReceivingTitle = "DOWNLOAD";
constexpr const char* kReceivingBody[] = {"KEEP THE PHONE NEAR THE DEVICE.",
                                          "AFTER THE DOWNLOAD, THE DEVICE",
                                          "INSTALLS THE FIRMWARE.", "PUSH ONCE TO CANCEL."};
constexpr int kReceivingBodyRows = 4;

constexpr int installing_body_y(int row) { return kInstallingBodyY + row * kInstallingLineHeight; }

void draw_notice_heading(ui::Canvas& canvas, const char* header, const char* title);
void draw_installing(ui::Canvas& canvas);
void draw_receiving(ui::Canvas& canvas);

}  // namespace skyblip::go

#endif
