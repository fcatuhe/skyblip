#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SETTINGS_CHANGES_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SETTINGS_CHANGES_H

#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

constexpr int kSettingsChangesBytes = kConfirmDetailRows * (kConfirmDetailColumns + 1);

// INFO: fc 20nov26 past kConfirmDetailRows the last row counts the rest, and no change returns 0
// INFO: fc 17nov26 sounds is whether the Go has a buzzer or a haptic, as the menu row reads it
int describe_settings_changes(const Settings& from, const Settings& to, char* out, int capacity,
                              bool sounds);

}  // namespace skyblip::go

#endif
