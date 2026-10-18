# the Bluetooth link

What `link.cpp` puts on the air, and why those UUIDs.

## Three services, because an EFB and our own app want different things

| Service | Characteristics | Who reads it |
|---|---|---|
| Nordic UART `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | `...0002` written by the app, `...0003` notified by us | XCSoar, Enroute Flight Navigation, anything SoftRF-shaped |
| HM-10 `FFE0` | `FFE1`, notify and write on one characteristic | LK8000, and the profile PowerFLARM Flex ships |
| skyBlip `69C21301-0187-4204-A91D-02EB8858B440` | `...02` config, `...03` flight log, each write and notify | our own configuration page |

The NMEA stream goes out on both public profiles at once, which is what SoftRF does (`src/platform/bluetooth/Bluefruit.cpp`, `bleuart_HM10` beside `bleuart_NUS`). The two conventions split the market: the open-source soaring stack prefers Nordic UART and XCSoar checks it first, while FLARM chose `FFE0`/`FFE1` for Fusion and Flex, which is what SkyDemon's 2024 BLE support was built against. Offering both costs one service definition and covers every client we know of.

Nordic's roles are not ours to reinterpret. `6E400002` is what the central writes and `6E400003` is what the peripheral notifies, and XCSoar refuses a service missing either (`android/src/BleSerialPort.java`). So `...0002` exists, accepts writes and drops them: nothing in this firmware reads an inbound NMEA sentence yet. Config and the log are on our own base, from `uuidgen`, because they are our protocol and a UART pipe is what three apps would read them as.

Before September 2026 the config and log characteristics sat on `6E400002` and `6E400003` themselves. That was the Nordic base with the roles inverted: an EFB that knew the convention would have subscribed to our config characteristic and waited for `$PFLAA` that was never coming.

## What is advertised

The advertisement is full: flags, the 128-bit Nordic UART UUID, the 16-bit `FFE0`. That is 25 of 31 bytes, and it is not optional padding. XCSoar builds its LE scan filters from service UUIDs (`android/src/BluetoothHelper.java`), so a device advertising only a name never appears in its list.

The name therefore rides the scan response, where 29 characters fit, and the product decides it (`core/identity/README.md`). Before activation it is `activate 3F2A skyBlip`, 21 characters, where `3F2A` is the low 16 bits of FICR `DEVICEADDR`, the address Zephyr advertises with, and the glass shows the same four digits. After it is `skyBlip Go 00012A` or `skyBlip Go+ 00012A`, the address the unit transmits under, which is the one the panel shows, so the entry in a phone's list matches the glass. The Nordic UART UUID is advertised either way, which is what our activation page's chooser filters on.

`begin()` only enables Bluetooth. Advertising starts on the first `advertise_as()`, which the product calls once it has verified the stored unlock code, so no phone ever sees a name that is about to change. A later `advertise_as()`, after an unlock, renames the unit live: `bt_set_name()` for the GAP characteristic, `bt_le_adv_update_data()` for the scan response, no reboot. The scan response is double-buffered and swapped whole through an atomic pointer, because the loop renames while the host thread may be restarting advertising after a connection.

## The unit's identity

`identity.{h,cpp}` fills `ports::Identity`: the DEVICEID through hwinfo, the tag from FICR `DEVICEADDR`, the key in UICR `CUSTOMER`, the unlock code in `identity_partition` at 0xEA000, and nonces from the RNG behind `zephyr,entropy`. `Platform::begin()` provisions the key before it enables Bluetooth. The UICR write is NVMC word writes through the nRF HAL, read back as memory before it is trusted, and `CONFIG_SOC_FLASH_NRF_UICR` stays off, so no flash driver call can ever reach UICR, let alone erase it. Which slot holds the key is `core/identity/key_slots.cpp`'s to decide. The server key the build trusts is compiled in from `skyblip_server_key.inc`, which `cmake/skyblip.cmake` generates from `firmware/keys/server.pub`.

## Several centrals

`CONFIG_BT_MAX_CONN` is 3 and `comms::LinkSessions::kMaxSessions` matches it, with a `static_assert` here so the two cannot drift. A session id is `bt_conn_index() + 1`, the controller's own slot, so ids never collide and never outlive the connection.

Subscription state is not held here. `bt_gatt_is_subscribed()` is per connection and already correct; the three booleans this file used to keep were one device's worth of state for what is now three centrals.

Advertising stops the instant a connection is created, and Zephyr 4.4 does not resume it. Both `connected()` and `recycled()` start it again: the first picks up the next free connection object, the second is the stack saying one has been freed. `-ENOMEM` from either is normal and means every object is in use.
