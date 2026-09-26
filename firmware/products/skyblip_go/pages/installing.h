#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_INSTALLING_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_INSTALLING_H

#include "products/skyblip_go/glass.h"

namespace skyblip::go {

constexpr int kInstallingLeftX = 4;
constexpr int kInstallingCellW = 6;
constexpr int kInstallingTitleY = 40;
constexpr int kInstallingLineH = 14;
constexpr int kInstallingBodyY = 90;

constexpr const char* kInstallingHeader = "FIRMWARE";
constexpr const char* kInstallingTitle = "INSTALLING";
constexpr const char* kInstallingBody[] = {"LEAVE THE DEVICE ON", "IT RESTARTS BY ITSELF",
                                           "IN ABOUT 30 S", "THE SCREEN STAYS STILL"};
constexpr int kInstallingBodyRows = 4;

constexpr int installing_body_y(int row) { return kInstallingBodyY + row * kInstallingLineH; }

void draw_notice_heading(ui::Canvas& fb, const char* header, const char* title);
void draw_installing(ui::Canvas& fb);

}  // namespace skyblip::go

#endif
