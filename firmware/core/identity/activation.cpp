#include "core/identity/activation.h"

#include "core/util/format.h"

namespace skyblip::identity {

int bluetooth_name(char* out, int capacity, const char* product, const Activation& activation) {
    char name[kBluetoothNameBytes + 1];
    int written = 0;
    if (const settings::AirAddress address = activation.address()) {
        while (written < kProductWordBytes && product[written] != 0) {
            name[written] = product[written];
            written++;
        }
        written += format_string(name + written, " ");
        written += format_hex(name + written, *address, 6);
    } else {
        written += format_string(name, "activate ");
        written += format_hex(name + written, activation.bluetooth_tag, 4);
        written += format_string(name + written, " skyBlip");
    }
    if (written >= capacity) written = capacity - 1;
    for (int i = 0; i < written; i++) out[i] = name[i];
    out[written] = 0;
    return written;
}

}  // namespace skyblip::identity
