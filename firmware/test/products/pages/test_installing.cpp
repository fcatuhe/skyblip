// The page the glass wears through the swap, read back off the pixels.
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/installing.h"
#include "test/support/frame_glass.h"

using namespace skyblip;
using namespace skyblip::go;

TEST_CASE("installing page: it says what is happening and what not to do, unclipped") {
    Glass canvas;
    draw_installing(canvas);
    CHECK(reads_heading(canvas, kInstallingTitle, Heading::Interrupt));
    CHECK(reads_centred_lines(canvas, kNoticeBodyY, kInstallingBody, kInstallingBodyRows));
}

// Nothing to press: the keys stay off the glass, so the page asks for nothing it would ignore.
TEST_CASE("installing page: it shows no key, because no gesture changes it") {
    Glass canvas;
    draw_installing(canvas);
    CHECK_FALSE(has_button_key(canvas));
    CHECK_FALSE(has_pad_key(canvas));
}

TEST_CASE("receiving page: it says the install follows on its own, and the button cancels it") {
    Glass canvas;
    draw_receiving(canvas);
    CHECK(reads_heading(canvas, kReceivingTitle, Heading::Interrupt));
    CHECK(reads_centred_lines(canvas, kNoticeBodyY, kReceivingBody, kReceivingBodyRows));
    CHECK(reads_button_key(canvas, kReceivingCancelVerb));
    CHECK_FALSE(has_pad_key(canvas));
    CHECK_FALSE(reads_heading(canvas, kInstallingTitle, Heading::Interrupt));
}

TEST_CASE("installing page: it cannot be read as the prompt it replaced") {
    Glass canvas;
    draw_installing(canvas);
    CHECK_FALSE(reads_button_key(canvas, kConfirmAllowVerb));
}
