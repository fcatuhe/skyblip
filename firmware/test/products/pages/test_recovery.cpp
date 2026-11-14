// The page the glass wears while the factory bootloader has the device, read back off the pixels.
#include <initializer_list>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/recovery.h"
#include "test/support/frame_glass.h"

using namespace skyblip;
using namespace skyblip::go;

TEST_CASE("recovery page: it names the drive, the file and the way back, unclipped") {
    for (const ports::RecoveryPath path :
         {ports::RecoveryPath::Rebooted, ports::RecoveryPath::PowerOffToFinish}) {
        Glass canvas;
        draw_recovery(canvas, path);
        CHECK(reads_heading(canvas, kRecoveryTitle, Heading::Interrupt));
        const bool keys = recovery_button(path)[0] != 0;
        CHECK(reads_body(canvas, Heading::Interrupt, keys, kRecoveryBody, kRecoveryBodyRows));
    }
}

TEST_CASE("recovery page: a power off asks for the press on its key, a reboot shows no key") {
    Glass rebooted;
    draw_recovery(rebooted, ports::RecoveryPath::Rebooted);
    Glass powered_off;
    draw_recovery(powered_off, ports::RecoveryPath::PowerOffToFinish);

    CHECK(reads_button_key(powered_off, kRecoveryStartVerb));
    CHECK_FALSE(has_button_key(rebooted));
    CHECK_FALSE(has_pad_key(rebooted));
    CHECK_FALSE(has_pad_key(powered_off));
}

TEST_CASE("recovery page: it cannot be read as the prompt it replaced") {
    Glass canvas;
    draw_recovery(canvas, ports::RecoveryPath::PowerOffToFinish);
    CHECK_FALSE(reads_button_key(canvas, kConfirmAllowVerb));
}
