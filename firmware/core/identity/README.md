# core/identity

Who this unit is, as skyblip.eu knows it: a FICR DEVICEID, the public half of a key of its own, the server key the build trusts, and once the server has signed one, a serial. The design is issue #126, the wire contract `schemas/activation.v1.schema.json`. Nothing here uses the firmware signing keys.

## The three messages

`messages.{h,cpp}` builds every byte string a signature covers, in one place, exactly as the schema lays them out: an ASCII label naming the purpose and version, then fixed-width fields, so no signature made for one purpose can pass for another, a future air packet included.

| Message | Bytes | Signed by |
|---|---|---|
| register | `skyblip:register:v1` Nu Ns | the server |
| registration | `skyblip:registration:v1` DEVICEID Pd Nu Ns server-key-id version | the unit |
| unlock | `skyblip:unlock:v1` DEVICEID Pd serial(2, big-endian) Nu | the server |

DEVICEID is FICR `DEVICEID[1]` then `DEVICEID[0]`, each big-endian, as Zephyr's hwinfo hands it out. A server key id is the first four bytes of the SHA-256 of the raw 32-byte public key. `test/core/test_identity.cpp` builds all three from `schemas/activation.v1.vectors.json` and compares them byte for byte, then signs and verifies each: Ed25519 is deterministic, so the unit's registration signature comes out identical to the one the server's suite checks.

## The unit

`unit.{h,cpp}` is `identity::Unit`, a facade over `ports::Identity`. `load()` reads the DEVICEID, the key and the stored unlock code, and activates the unit only if that code verifies with the trusted server key over its own DEVICEID, its current public key and the stored nonce, for a serial from 1: serial 0 would put 0x000000, which decoders read as no address, on the air, and the server assigns from 1. It runs at every boot, so a code signed by the other key (a unit moved between development and production firmware), for another unit, or for a key the unit no longer holds leaves it unactivated, and it activates again once and gets the same serial back.

`activation.{h,cpp}` is what the rest of the firmware reads: `identity::Activation`, the serial if there is one and the Bluetooth tag, published on `bus::State::activation` by the config service, which owns the unit. Its `address()` is `settings::address_of(serial)` or nothing. `bluetooth_name()` makes the name the unit advertises: `activate 3F2A skyBlip` before activation, where `3F2A` is the tag, and `skyBlip Go 00012A` or `skyBlip Go+ 00012A` after it, the product word coming from the product and the address in six uppercase hex digits.

`unlock_code.{h,cpp}` is the 82 bytes the unit keeps: the serial big-endian, Nu, then U, padded with 0xFF to 84 so the page is written in whole words. An erased page reads all 0xFF and holds no code; anything else decodes, and the signature decides.

## The key and the page, on the T-Echo

`hardware/platform/zephyr/identity.cpp` fills the port, and `key_slots.{h,cpp}` is the part of it that is logic, portable and tested on the host. UICR `CUSTOMER` (0x10001080, 128 bytes) holds four slots of eight words. A slot is blank when every word reads 0xFFFFFFFF, complete when none does, and torn otherwise: a write cut short, which is never used. The current key is the last complete slot. On a boot that finds none, a seed of 32 bytes from the hardware RNG goes into the next blank slot after the last one written, before Bluetooth starts, and is read back; a seed holding an erased word would read back torn, so it is drawn again. A read error hands out no seed. The writes are NVMC word writes through the nRF HAL, in write mode only for the word, with no flash driver call anywhere near UICR: none can ever erase it, which would also erase the bootloader address in `NRFFW[0]`. A unit with no key cannot be activated, and says `incomplete`.

The unlock code lives in its own 4 KB page at 0xEA000 (`identity_partition`), above the bootloader's `USER_FLASH_END`, so no `.uf2` writes it, and below the LittleFS that Meshtastic and SoftRF keep at 0xED000, so a unit that tries another firmware and comes back is still activated. It is written through Zephyr's flash API, which syncs every NVMC step with the Bluetooth radio, because the unlock arrives over a live connection, and on an activated unit it waits for a write window outside the dwells (`core/comms/README.md`). There is one page and no A/B pair: a brown-out between the erase and the write leaves a blank page, which costs one activation again, to the same serial.

On the host, `hardware/platform/host/identity.h` is the unit and the test server of the vectors, and trusts that server's key. It holds the private half of the test key too, which is how it hands the simulator and every product case an unlock code directly: the simulator flies and transmits without a server, and a case that wants a unit with no serial asks for one.

## Which server key a build trusts

Each image embeds one public key and nothing else verifies. A development build (main, `scripts/build_local.sh`, a pull request) trusts `keys/server/development.pub`, a release (`products/<product>/release/app.conf` sets `CONFIG_SKYBLIP_SERVER_KEY_PRODUCTION`) trusts `keys/server/production.pub` and fails to configure without it, the host suite and the simulator trust the test key in `schemas/activation.v1.vectors.json`. `cmake/skyblip.cmake` turns the file into `skyblip_server_key.inc` (`firmware/keys/README.md`).
