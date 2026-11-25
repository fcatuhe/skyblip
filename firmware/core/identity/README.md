# core/identity

Who this unit is, as skyblip.eu knows it: a FICR DEVICEID, the public half of a key of its own, the server key the build trusts, and once the server has signed one, a serial. The design is issue #126. The wire contract is the activation frames of `schemas/link.v1.json` and `schemas/activation.v1.schema.json`. Nothing here uses the firmware signing keys.

## The four messages

`messages.{h,cpp}` builds every byte string a signature covers, in one place, as the schema lays them out: an ASCII label naming the purpose and version, then fixed-width fields. The label is what keeps a signature made for one purpose from passing for another, a future air packet included.

| Message | Bytes | Signed by |
|---|---|---|
| register | `skyblip:register:v1` unit_nonce server_nonce | the server |
| registration | `skyblip:registration:v2` device_id public_key unit_nonce server_nonce server_key_id release product variant bluetooth_tag(2, big-endian) version | the unit |
| unlock | `skyblip:unlock:v1` device_id public_key serial(2, big-endian) unit_nonce | the server |
| attestation | `skyblip:attestation:v1` device_id public_key server_nonce release product variant version | the unit |

- DEVICEID is FICR `DEVICEID[1]` then `DEVICEID[0]`, each big-endian, as Zephyr's hwinfo hands it out.
- A server key id is the first four bytes of the SHA-256 of the raw 32-byte public key.
- `release` is one byte, 1 for a build made with the release fragments in `products/common/release/`, whose bootloader refuses an older image. `ports::Dfu::release_build()` reports it as the inverse of `downgrade_allowed()`. Nothing on the air differs: a development unit is a real aircraft with a real serial.
- `product` and `variant` are one byte each, the codes `schemas/link.v1.json` gives them.

The registration names the unit as its identity frame does, so the server records an activated unit with its product, variant and Bluetooth tag on the unit's own signature. Firmware as old as v0.8.0 signs `skyblip:registration:v1`, which stops at `release` and the version, and the server still takes it. The attestation is the unit's word on the firmware it runs, over a nonce the server minted (`core/comms/README.md`, Attestation).

`schemas/activation.v1.vectors.json` holds all four, and the server's suite checks the same vectors. Ed25519 is deterministic, so the unit's signatures match the server's byte for byte.

## The unit

`unit.{h,cpp}` is `identity::Unit`, a facade over `ports::Identity`. `load()` reads the DEVICEID, the key and the stored unlock code at every boot. It activates the unit only if the code verifies with the trusted server key over the unit's own DEVICEID, its current public key and the stored nonce, for a serial from 1. Serial 0 would put 0x000000 on the air, which decoders read as no address. A code for another key or another unit leaves it unactivated, and a new activation gives it the same serial back.

`activation.{h,cpp}` is what the rest of the firmware reads: `identity::Activation`, the serial if there is one and the Bluetooth tag, published on `bus::State::activation` by the config service. `address()` is `settings::address_of(serial)` or nothing. `bluetooth_name()` is the Go's `NameRule`: `activate 3F2A skyBlip` before activation, where `3F2A` is the tag, and `skyBlip Go 00012A` or `skyBlip Go+ 00012A` after it, the address in six uppercase hex digits. skyBlip Bravo is never activated and always advertises `skyBlip Bravo 3F2A` (`products/skyblip_bravo/name.h`).

`variant.h` is `identity::Variant`, `standard` or `plus`, the hardware build of a product. The Go works it out at every boot from the parts it found, and stores it nowhere.

`unlock_code.{h,cpp}` is the 82 bytes the unit keeps: the serial big-endian, `unit_nonce`, then the unlock signature, padded with 0xFF to 84 so the page is written in whole words. An erased page holds no code. Anything else decodes, and the signature decides.

## The key and the page, on the T-Echo

`hardware/platform/zephyr/identity.cpp` fills the port. `key_slots.{h,cpp}` is its logic, portable and tested on the host.

UICR `CUSTOMER` (0x10001080, 128 bytes) holds four slots of eight words. A slot is seven words of material, 28 bytes from the hardware RNG, then a tag: the first four bytes of the SHA-256 of `skyblip:slot:v1`, the DEVICEID and the material, each word little-endian. The seed is the SHA-256 of `skyblip:seed:v1` and the material.

- The tag hashes the DEVICEID, so a slot copied onto another chip reads torn there, and that chip draws a key of its own.
- The seed leaves the DEVICEID out on purpose: an RNG that hands several chips the same draw gives them the same key, which the server's `Units` page flags.
- A slot is blank when every word reads 0xFFFFFFFF, complete when no word does and the tag matches, and torn otherwise. A torn slot is never used.
- The current key is the last complete slot. A boot that finds none draws fresh material into the next blank slot, before Bluetooth starts, and reads it back.
- A draw that would leave an erased word, or that is one byte repeated (a buffer a driver never filled), is drawn again, four times at most. A read error hands out no seed. A unit whose four slots are torn has no key, cannot be activated, and says `keyless`.
- A build from before pull request #168 kept eight raw seed words in a slot. Each format reads the other's slot as torn, so a unit moved across that line draws a new key and needs an admin to promote it again.
- The writes are NVMC word writes through the nRF HAL, with no flash driver call near UICR. An erase of UICR would also erase the bootloader address in `NRFFW[0]`.

The unlock code lives in its own 4 KB page at 0xEA000 (`identity_partition`). It sits above the bootloader's `USER_FLASH_END`, so no `.uf2` writes it, and below the LittleFS that Meshtastic and SoftRF keep at 0xED000, so a unit that tries another firmware and comes back is still activated. It is written through Zephyr's flash API, which syncs every NVMC step with the Bluetooth radio, because the unlock arrives over a live connection. On an activated unit the write waits for a window outside the dwells (`core/comms/README.md`). There is one page and no A/B pair: a brown-out between the erase and the write costs one activation again, to the same serial. So an unlock for the serial the unit already holds is acknowledged without touching the page.

On the host, `hardware/platform/host/identity.h` is the unit and the test server of the vectors. It holds the test server's private key, so the simulator and every product case get an unlock code without a server.

## Which server key a build trusts

Only skyblip.eu signs for a unit. Every build of a product that sets `CONFIG_SKYBLIP_ACTIVATION`, release or not, embeds the server key `keys/server.pub`, and nothing else verifies. `cmake/skyblip.cmake` turns it into `skyblip_server_key.inc` and refuses to configure without it (`firmware/keys/README.md`). A product without activation, such as Bravo, embeds no server key and provisions no unit key. The host suite and the simulator trust the test key in `schemas/activation.v1.vectors.json`.
