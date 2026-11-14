#include "products/skyblip_go/pages/activation.h"

#include "core/util/format.h"
#include "products/skyblip_go/pages/installing.h"

namespace skyblip::go {

void draw_activation(ui::Canvas& canvas, uint16_t bluetooth_tag) {
    draw_heading(canvas, kActivationTitle, Heading::Interrupt);
    draw_centred_lines(canvas, kNoticeBodyY, kActivationBody, kActivationBodyRows);
    char tag[5];
    tag[format_hex(tag, bluetooth_tag, 4)] = 0;
    draw_centred_line(canvas, kActivationTagY, tag, kActivationTagScale);
}

}  // namespace skyblip::go
