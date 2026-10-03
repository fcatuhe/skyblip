// What a "set" from the phone would change, in the words of the device's own menu.
#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SETTINGS_CHANGES_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SETTINGS_CHANGES_H

#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

constexpr int kSettingsChangesCap = kConfirmDetailRows * (kConfirmDetailCols + 1);

// Rows joined by '\n', at most kConfirmDetailRows of them: past that, the last row counts the
// rest. Writes nothing and returns 0 when nothing differs, or when cap is under
// kSettingsChangesCap.
int describe_settings_changes(const Settings& from, const Settings& to, char* out, int cap);

}  // namespace skyblip::go

#endif
