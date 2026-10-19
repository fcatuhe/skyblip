#include "products/skyblip_go/pages/activation.h"

#include "core/util/format.h"
#include "products/skyblip_go/pages/installing.h"

namespace skyblip::go {

void draw_activation(ui::Canvas& fb, uint16_t bluetooth_tag) {
    draw_notice_heading(fb, kActivationHeader, kActivationTitle);
    fb.draw_text(kInstallingLeftX, installing_body_y(0), kActivationWhere, true, 1);
    fb.draw_text(kInstallingLeftX, installing_body_y(1), "AND PICK THIS UNIT:", true, 1);

    char tag[5];
    tag[fmt_hex(tag, bluetooth_tag, 4)] = 0;
    fb.draw_text(kInstallingLeftX, kActivationTagY, tag, true, kActivationTagScale);

    char name[32];
    int n = fmt_string(name, "ACTIVATE ");
    n += fmt_string(name + n, tag);
    n += fmt_string(name + n, " SKYBLIP");
    name[n] = 0;
    fb.draw_text(kInstallingLeftX, kActivationNameY, name, true, 1);
}

}  // namespace skyblip::go
