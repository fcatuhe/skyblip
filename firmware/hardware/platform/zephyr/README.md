# the Bluetooth link

What the link and `central.cpp` put on the air, and why those UUIDs. The link is three files over `link_state.h`: `link_gatt.cpp` holds the GATT table and the writes, `link_advertising.cpp` the advert and the scan response, and `link.cpp` the connections, their sessions and the notifications.

## Three services, because an EFB and our own app want different things

| Service | Characteristics | Who reads it |
|---|---|---|
| Nordic UART `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | `...0002` written by the app, `...0003` notified by us | XCSoar, Enroute Flight Navigation, LK8000 since 7.6.8, anything SoftRF-shaped |
| HM-10 `FFE0` | `FFE1`, notify and write on one characteristic | SkyDemon, LK8000 before 7.6.8, Enroute on a device without Nordic UART, and the profile PowerFLARM Fusion and Flex ship |
| skyBlip `69C21301-0187-4204-A91D-02EB8858B440` | `...02` config, `...03` flight log, each write and notify. `...04` peer, written by a paired Go and notified by Bravo, served only by a product that sets `CONFIG_SKYBLIP_PEER_SERVER`, Bravo | our own configuration page, and a paired Go (`core/comms/README.md`, The peer endpoint) |

The NMEA stream goes out on both public profiles, as SoftRF does (`src/platform/bluetooth/Bluefruit.cpp`, `bleuart_HM10` beside `bleuart_NUS`). The open-source soaring stack prefers Nordic UART, while other devices expose HM-10's `FFE0`/`FFE1`. Offering both costs one service definition and covers every client we know of.

The EFB chooses the service. Each app looks up the UUID it knows, wherever it sits in the GATT table: XCSoar and Enroute try Nordic UART first, and Enroute falls back to `FFE0` only when a device has no Nordic UART (`TrafficDataSource_BluetoothLowEnergy.cpp` at enroute `8e5bc1b`). LK8000 from 7.6.8 subscribes to every UART characteristic it knows (`BluetoothGattClientPort.configureCharacteristics()`). `notify_one()` sends each frame on one service per connection, Nordic UART when the connection subscribed to it, so a client subscribed to both gets each frame once.

Keep Nordic's roles: `6E400002` is what the central writes and `6E400003` is what the peripheral notifies, and XCSoar refuses a service missing either (`android/src/BleSerialPort.java`). So `...0002` exists, accepts writes and drops them, because nothing reads an inbound NMEA sentence yet. Config and the log sit on our own base, from `uuidgen`, because they are our protocol and an EFB would read a UART pipe as NMEA.

## What is advertised

The advertisement is full: flags, the 128-bit Nordic UART UUID and the 16-bit `FFE0`, 25 of 31 bytes. XCSoar builds its LE scan filters from service UUIDs (`android/src/BluetoothHelper.java`), so a device advertising only a name never appears in its list.

The name rides the scan response, where 29 characters fit, and the product decides it (`core/identity/README.md`):

- Before activation it is `activate 3F2A skyBlip`, where `3F2A` is the low 16 bits of FICR `DEVICEADDR`, the address Zephyr advertises with. The glass shows the same four digits.
- After activation it is `skyBlip Go 00012A` or `skyBlip Go+ 00012A`, the unit's own address as the self-test page shows it. It stays the unit's own while the unit transmits an aircraft address in its place (`core/settings/README.md`).
- A Bravo is never activated and is always `skyBlip Bravo 3F2A` (`products/skyblip_bravo/README.md`).

Our activation page's chooser filters on the Nordic UART UUID, which is advertised either way.

`begin()` only enables Bluetooth. Advertising starts on the first `advertise_as()`, which the product calls once it has verified the stored unlock code, so no phone sees a name about to change. A later `advertise_as()`, after an unlock, renames the unit live with `bt_set_name()` and `bt_le_adv_update_data()`, without a reboot. The scan response is double-buffered and swapped whole through an atomic pointer, because the loop renames while the host thread may be restarting advertising after a connection.

Advertising stops when a connection is created, and Zephyr 4.5 does not resume it. `connected()` and `recycled()` both restart it: the first takes the next free connection object, the second runs when the stack frees one. `-ENOMEM` from either means every object is in use.

## The unit's identity

`identity.{h,cpp}` fills `ports::Identity`: the DEVICEID through hwinfo, the tag, the key in UICR `CUSTOMER`, and the unlock code in `identity_partition` at 0xEA000. `identity.cpp` is built only for a product that sets `CONFIG_SKYBLIP_ACTIVATION`. `core/identity/key_slots.cpp` decides which slot holds the key.

- `random.h` fills `ports::Random` from the RNG behind `zephyr,entropy`, with bias correction on, for the key material, every nonce and the peer link's boot epoch.
- `bluetooth_tag.h` reads the tag, the low 16 bits of FICR `DEVICEADDR`, apart from the rest, because it names a unit on the air whether or not it is activated: Bravo has the tag and no key.
- `TEchoPlus::begin()` (`t_echo_plus.h`) provisions the key before it enables Bluetooth. `MeshtrackerX1` provisions none: it fills the role with `ports::KeylessIdentity`, the tag and the DEVICEID, which `device_id.h` reads off FICR in the order hwinfo hands the Go.
- The UICR write is NVMC word writes through the nRF HAL, read back before it is trusted. `CONFIG_SOC_FLASH_NRF_UICR` stays off, so no flash driver call can reach UICR, let alone erase it.
- `cmake/skyblip.cmake` generates `skyblip_server_key.inc`, the server key the build trusts, from `firmware/keys/server.pub`.

## Several centrals

`CONFIG_BT_MAX_CONN` is 3 and equals `comms::LinkSessions::kMaxSessions`, with a `static_assert` so the two cannot drift: every connection a phone can take is a row in the table. A session id is `bt_conn_index() + 1`, the controller's own slot, so ids never collide and never outlive the connection.

`link.cpp` serves only connections where the unit is the peripheral. The Go's link to a paired Bravo, where the Go is central, is `central.cpp`'s. `connected()` skips it by its role (`bt_conn_get_info()`), and it never enters `g_connections`. So it never enters `LinkSessions`, gets no MTU request and no share of the NMEA broadcast, and a write it makes here is refused `WRITE_NOT_PERMITTED`.

The paired peer keeps a connection slot:

- On the Go, while a Bravo is paired, `reserve_peer_connection(true)` lowers the phone ceiling to two. Advertising stops at two phones, even while Bravo is away. A third phone that connects before it stops is disconnected with `REMOTE_LOW_RESOURCES` and never reaches the bus.
- On Bravo, its Go connects as a central, and `connected()` hands `LinkSessions` the address `bt_conn_get_dst()` gives. `reserve_peer_connection_for()` keeps the slot for that address: advertising goes on while the Go is away, and a third phone is disconnected the same way.

A peer write reaches the bus as an `RxFrame` on `Endpoint::Peer`, and `central_of()` names its sender's address under the same lock. Subscription state is not held here: `bt_gatt_is_subscribed()` is per connection and already correct.

## The Go's central

`central.cpp` is built only where `CONFIG_BT_CENTRAL` is set, in the Go's `prj.conf`. It fills `ports::Central`:

- The scan is active, because the name rides the scan response, with duplicates filtered. It reports only an advertiser whose complete name starts with the prefix the product passes, `skyBlip Bravo `, so a hangar full of phones cannot fill the event queue ahead of a Bravo the duplicate filter would never report again.
- A connection is `bt_conn_le_create()` to the pinned address, its type included, with the initiator listening at the duty the product passes, with Zephyr's 3 s create timeout (`CONFIG_BT_CREATE_CONN_TIMEOUT`), after which the product asks again.
- Once connected the Go asks for the MTU, discovers the peer characteristic, `...04`, and subscribes with `CONFIG_BT_GATT_AUTO_DISCOVER_CCC`. The link is `Up` only then, with its payload size, so the product can refuse one below 126 bytes before writing anything.
- `peer_claim` goes out as a write without response, refused when wider than the payload. The service loop never makes the write itself, because off the system work queue `bt_att_create_pdu()` waits forever for a buffer (`subsys/bluetooth/host/att.c:794-815` at v4.4.2). `write()` takes the connection and handle under the lock that clears them, copies the frame and submits it to the system work queue, where an empty pool answers `-ENOMEM` at once. One write is in flight at a time, `kCentralWritesInFlight`, and the next answers `WouldBlock` until its sent callback lands.
- A notification goes onto its own queue of eight frames. One the queue cannot take, or longer than an `RxFrame`, is dropped, as is an event the event queue cannot take.

`att_budget.h` sizes the ATT pool. `CONFIG_BT_ATT_TX_COUNT`, 18, holds three phones' shares of six (four notifications, a response and an SMP reply), and holds a paired Go's two phones and Bravo's three (one write, one request and one response). A Bravo that stops taking frames costs the phones nothing: its one write stays in flight, the next `peer_claim` is refused `WouldBlock`, the loop never waits, and the 4 s supervision timeout of `BT_LE_CONN_PARAM_DEFAULT` ends the link before the Go reconnects.

Zephyr drops the sent callbacks of a link that is gone, so each new link starts its write count at zero, and a generation number tells a callback from an older link apart. A write the work queue fails, `-ENOTCONN` included, gives its count back whatever link it belonged to, because the count is the work item's own.

`connect()` creates the connection into a local and publishes it under the lock the callbacks release it under. Each callback releases only the connection it is given, if the lock still holds it. A create that fails before it is published is found disconnected once published, so its reference is dropped exactly once.

No SMP and no bonding, on this link as on the phones', so the Windows Web Bluetooth update path keeps working. The address is pinned on both sides instead (`core/comms/README.md`, The Go's link to its Bravo).
