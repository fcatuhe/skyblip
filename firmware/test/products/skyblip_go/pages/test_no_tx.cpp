// The NO TX mark, read off the glass: the word, the reason under it, white on black.
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/pages/frame.h"
#include "products/skyblip_go/pages/no_tx.h"
#include "test/support/glass_text.h"

using namespace skyblip;
using namespace skyblip::go;

TEST_CASE("no tx mark: NO TX over its reason, reversed in the top-left corner") {
    for (const timing::NoTx reason :
         {timing::NoTx::Radio, timing::NoTx::Gnss, timing::NoTx::Fix, timing::NoTx::Utc,
          timing::NoTx::Pps, timing::NoTx::Address}) {
        Glass glass;
        glass.clear(true);
        draw_no_tx(glass, reason);
        CHECK(reads_at(glass, kNoTxMargin, kNoTxMargin, kNoTxWord, 1, false));
        CHECK(reads_at(glass, kNoTxMargin, kNoTxReasonY, timing::to_string(reason), 1, false));
        CHECK(glass.get_pixel(kNoTxMargin - kNoTxPad, kNoTxMargin - kNoTxPad));
        CHECK_FALSE(glass.get_pixel(kGlassWidth / 2, kNoTxMargin));
    }
}

// ADDRESS, seven characters, is the widest reason the patch has to hold.
TEST_CASE("no tx mark: the widest reason sits whole inside its reversed patch") {
    Glass glass;
    glass.clear(true);
    draw_no_tx(glass, timing::NoTx::Address);
    REQUIRE(std::string(timing::to_string(timing::NoTx::Address)).size() == 7);
    const int right = kNoTxMargin + text_width("ADDRESS", 1);
    CHECK(reads_at(glass, kNoTxMargin, kNoTxReasonY, "ADDRESS", 1, false));
    CHECK(glass.get_pixel(right + kNoTxPad - 1, kNoTxReasonY));
    CHECK_FALSE(glass.get_pixel(right + kNoTxPad, kNoTxReasonY));
    for (const timing::NoTx reason : {timing::NoTx::Radio, timing::NoTx::Gnss, timing::NoTx::Fix,
                                      timing::NoTx::Utc, timing::NoTx::Pps})
        CHECK(std::string(timing::to_string(reason)).size() <= 7);
}

TEST_CASE("no tx mark: a Go that transmits wears no mark") {
    Glass glass;
    glass.clear(true);
    draw_no_tx(glass, timing::NoTx::None);
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 48; x++) CHECK_FALSE(glass.get_pixel(x, y));
}
