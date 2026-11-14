#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_PARKED_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_PARKED_H

#include "products/skyblip_go/pages/frame.h"

namespace skyblip::go {

constexpr BodyLine kParkedFlatCell[] = {{"BATTERY EMPTY"}};
constexpr int kParkedFlatCellLines = 1;
constexpr BodyLine kParkedLowCell[] = {{"CHARGE THE"}, {"BATTERY"}};
constexpr int kParkedLowCellLines = 2;

}  // namespace skyblip::go

#endif
