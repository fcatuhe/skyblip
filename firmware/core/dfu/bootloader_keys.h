#ifndef SKYBLIP_CORE_DFU_BOOTLOADER_KEYS_H
#define SKYBLIP_CORE_DFU_BOOTLOADER_KEYS_H

#include "core/dfu/image.h"
#include "ports/dfu.h"

namespace skyblip::dfu {

// INFO: fc 15nov26 MCUboot embeds each P-256 key as DER, and imgtool's KEYHASH is its SHA-256
bool find_bootloader_keys(ImageReader& bootloader, ports::BootloaderKeys& out);

}  // namespace skyblip::dfu

#endif
