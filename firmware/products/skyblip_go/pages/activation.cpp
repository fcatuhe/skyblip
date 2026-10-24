#include "products/skyblip_go/pages/activation.h"

#include "core/util/format.h"
#include "products/skyblip_go/pages/installing.h"

namespace skyblip::go {

void draw_activation(ui::Canvas& canvas, uint16_t bluetooth_tag) {
    draw_notice_heading(canvas, kActivationHeader, kActivationTitle);
    canvas.draw_text(kInstallingLeftX, installing_body_y(0), kActivationWhere, true, 1);
    canvas.draw_text(kInstallingLeftX, installing_body_y(1), "AND PICK THIS UNIT:", true, 1);

    char tag[5];
    tag[format_hex(tag, bluetooth_tag, 4)] = 0;
    canvas.draw_text(kInstallingLeftX, kActivationTagY, tag, true, kActivationTagScale);

    char name[32];
    int written = format_string(name, "ACTIVATE ");
    written += format_string(name + written, tag);
    written += format_string(name + written, " SKYBLIP");
    name[written] = 0;
    canvas.draw_text(kInstallingLeftX, kActivationNameY, name, true, 1);
}

}  // namespace skyblip::go
