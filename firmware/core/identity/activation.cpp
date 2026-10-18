#include "core/identity/activation.h"

#include "core/util/format.h"

namespace skyblip::identity {

int bluetooth_name(char* out, int cap, const char* product, const Activation& activation) {
    char name[kBluetoothNameCap + 1];
    int n = 0;
    if (const settings::AirAddress address = activation.address()) {
        n += fmt_string(name, product);
        n += fmt_string(name + n, " ");
        n += fmt_hex(name + n, *address, 6);
    } else {
        n += fmt_string(name, "activate ");
        n += fmt_hex(name + n, activation.bluetooth_tag, 4);
        n += fmt_string(name + n, " skyBlip");
    }
    if (n >= cap) n = cap - 1;
    for (int i = 0; i < n; i++) out[i] = name[i];
    out[n] = 0;
    return n;
}

}  // namespace skyblip::identity
