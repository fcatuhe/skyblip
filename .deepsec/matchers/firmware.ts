import { regexMatcher, type MatcherPlugin } from "deepsec/config";

export const firmwareDoor: MatcherPlugin = {
  slug: "skyblip-firmware-door",
  description: "Firmware code that reads bytes a stranger controls (radio, Bluetooth link, NMEA, update images) or decides trust (identity, signatures, DFU)",
  noiseTier: "normal",
  filePatterns: [
    "core/comms/**/*.{cpp,h}", "core/dfu/**/*.{cpp,h}", "core/identity/**/*.{cpp,h}", "core/crypto/**/*.{cpp,h}",
    "core/gnss/nmea.{cpp,h}", "core/protocol/**/*.{cpp,h}", "core/util/cbor.{cpp,h}",
    "protocols/**/*.{cpp,h}", "products/common/services/**/*.{cpp,h}",
    "hardware/platform/zephyr/**/*.{cpp,h}",
  ],
  examples: ["namespace link {"],
  match(content) {
    return regexMatcher("skyblip-firmware-door", [{ regex: /^\s*(namespace|class|struct)\s+\w+/gm, label: "input door or trust code" }], content);
  },
};
