#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_INSTALLING_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_INSTALLING_H

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/frame.h"

namespace skyblip::go {

constexpr const char* kInstallingTitle = "INSTALLATION";
constexpr BodyLine kInstallingBody[] = {
    {"KEEP THE GO ON."}, {""}, {"IT STARTS AGAIN"}, {"IN APPROXIMATELY"}, {"30 S."}};
constexpr int kInstallingBodyRows = 5;

constexpr const char* kReceivingTitle = "DOWNLOAD";
constexpr BodyLine kReceivingBody[] = {{"KEEP THE PHONE"}, {"NEAR THE GO."},     {""},
                                       {"AFTER THE"},      {"DOWNLOAD, THE GO"}, {"INSTALLS IT."}};
constexpr int kReceivingBodyRows = 6;
constexpr const char* kReceivingCancelVerb = "CANCEL";

void draw_installing(ui::Canvas& canvas);
void draw_receiving(ui::Canvas& canvas);

}  // namespace skyblip::go

#endif
