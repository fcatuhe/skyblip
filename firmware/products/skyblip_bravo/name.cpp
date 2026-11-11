#include "products/skyblip_bravo/name.h"

#include "core/util/format.h"

namespace skyblip::bravo {

static_assert(sizeof("skyBlip Bravo 3F2A") - 1 <= identity::kBluetoothNameBytes,
              "the name keeps its tag");

int bluetooth_name(char* out, int capacity, const char* product,
                   const identity::Activation& activation) {
    char name[identity::kBluetoothNameBytes + 1];
    int written = 0;
    while (written < identity::kProductWordBytes && product[written] != 0) {
        name[written] = product[written];
        written++;
    }
    written += format_string(name + written, " ");
    written += format_hex(name + written, activation.bluetooth_tag, 4);
    if (written >= capacity) written = capacity - 1;
    for (int i = 0; i < written; i++) out[i] = name[i];
    out[written] = 0;
    return written;
}

}  // namespace skyblip::bravo
