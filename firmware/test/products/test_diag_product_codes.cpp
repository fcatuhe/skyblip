// The ordinals a diagnostics record carries that core/diag does not own: gesture, page, mode,
// prompt.
#include "core/comms/config.h"
#include "core/diag/payload.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/input/controls.h"
#include "products/skyblip_go/pages/page.h"
#include "products/skyblip_go/services/screen.h"

using namespace skyblip;

namespace {

constexpr uint8_t kUnpinned = 0xFF;

// No default in any switch below: a member added to one of these enums fails this build, naming it.
uint8_t wire(go::Gesture value) {
    switch (value) {
        case go::Gesture::None: return 0;
        case go::Gesture::Tap: return 1;
        case go::Gesture::LongTouch: return 2;
        case go::Gesture::Press: return 3;
    }
    return kUnpinned;
}

uint8_t wire(go::Page value) {
    switch (value) {
        case go::Page::Radar: return 0;
        case go::Page::Nearby: return 1;
        case go::Page::SixPack: return 2;
        case go::Page::GMeter: return 3;
        case go::Page::Status: return 4;
        case go::Page::Sats: return 5;
        case go::Page::RadioLog: return 6;
        case go::Page::Raw: return 7;
        case go::Page::Capture: return 8;
        case go::Page::SelfTest: return 9;
        case go::Page::kCount: return kUnpinned;
    }
    return kUnpinned;
}

uint8_t wire(go::Mode value) {
    switch (value) {
        case go::Mode::Page: return 0;
        case go::Mode::Menu: return 1;
    }
    return kUnpinned;
}

uint8_t wire(comms::Pending value) {
    switch (value) {
        case comms::Pending::None: return 0;
        case comms::Pending::Set: return 1;
        case comms::Pending::Dfu: return 2;
        case comms::Pending::Recovery: return 4;
        case comms::Pending::PowerOff: return 5;
        case comms::Pending::EraseLog: return 6;
        case comms::Pending::GnssCold: return 7;
    }
    return kUnpinned;
}

template <class E>
void codes_pinned(int count, int retired = 0) {
    int found = 0;
    for (int value = 0; value < 256; value++) {
        const uint8_t code = wire(static_cast<E>(value));
        if (code == kUnpinned) continue;
        CHECK(code == value);
        CHECK(value < count);
        found++;
    }
    CHECK(found == count - retired);
}

}  // namespace

TEST_CASE(
    "diag product codes: go::Gesture's codes are the contact record's gesture byte, and a "
    "code changed or added here moves the gesture list and its maximum in the schema") {
    codes_pinned<go::Gesture>(4);
}

TEST_CASE(
    "diag product codes: go::Page's codes are the screen record's page byte, and a page given "
    "a code here renames that page in every corpus already written unless the schema moves") {
    codes_pinned<go::Page>(10);
    CHECK(go::kPageCount == 10);
}

TEST_CASE(
    "diag product codes: go::Mode's codes are the screen record's mode byte, and a member "
    "added here raises the maximum the schema puts on mode") {
    codes_pinned<go::Mode>(2);
}

TEST_CASE(
    "diag product codes: comms::Pending's codes are the screen record's prompt byte, and a "
    "code changed or added here moves the prompt list in the schema") {
    // 3 was apply, and stays out of use so a capture written before still reads
    codes_pinned<comms::Pending>(8, /*retired=*/1);
}
