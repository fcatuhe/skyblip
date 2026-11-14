#include "products/skyblip_go/pages/activation.h"

#include "core/util/format.h"

namespace skyblip::go {

void draw_activation(ui::Canvas& canvas, uint16_t bluetooth_tag) {
    draw_heading(canvas, kActivationTitle, Heading::Interrupt);
    char tag[5];
    tag[format_hex(tag, bluetooth_tag, 4)] = 0;
    const BodyLine body[] = {{kActivationWhere}, {"/ACTIVATE."}, {""},
                             {"THEN SELECT:"},   {""},           {tag, kActivationTagScale}};
    draw_body(canvas, Heading::Interrupt, /*keys=*/false, body, 6);
}

}  // namespace skyblip::go
