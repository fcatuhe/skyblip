// Harness, not a test: a row of the nearby list read back by its source letter and its address.
#ifndef SKYBLIP_TEST_SUPPORT_NEARBY_GLASS_H
#define SKYBLIP_TEST_SUPPORT_NEARBY_GLASS_H

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/nearby.h"
#include "test/support/glass_text.h"

namespace skyblip::go {

inline bool nearby_lists(const ui::Canvas& canvas, char letter, const char* address) {
    const char source[2] = {letter, 0};
    for (int y = kNearbyFirstRowY; y + 7 * kNearbyScale <= kGlassHeight; y += kNearbyLineHeight)
        if (reads_at(canvas, kNearbyIdX, y, source, kNearbyScale) &&
            reads_at(canvas, kNearbyAddressX, y + kNearbyAddressDrop, address, 1))
            return true;
    return false;
}

}  // namespace skyblip::go

#endif
