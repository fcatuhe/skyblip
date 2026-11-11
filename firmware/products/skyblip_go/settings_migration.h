#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_MIGRATION_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_MIGRATION_H

#include <cstddef>
#include <cstdint>

#include "core/util/result.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

Status migrate_blob(uint8_t version, const uint8_t* in, size_t length, Settings& out);

}  // namespace skyblip::go

#endif
