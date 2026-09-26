# simulator

Two pages served from one build directory. `index.html` drives the WASM build of the firmware with simulated sensors; `console.html` talks to a device that exists, over Web Bluetooth. `make dev` serves both on `sim.skyblip.localhost:5114`, which Chrome treats as a secure origin, so the console works there without a certificate.

## The console

`ble.js` is the transport and knows nothing about what a page draws: it connects, reassembles the stream, and hands whole lines and parsed replies back. `console.html` is the UI. `make test` runs `test_ble.mjs` on node's own runner, no dependency: what it pins down is the reassembly, because a sentence cut across two notifications is the failure that looks like a firmware bug from the page.

The picker lists anything advertising Nordic UART or HM-10, which is skyBlip and SoftRF both, so the same page is how you read one and configure the other.

**The serial endpoint** is the traffic picture: `$PFLAU`, `$PGRMZ`, `$LK8EX1`, a `$PFLAA` per target, `$GPRMC`/`$GPGGA` with a fix. A notification is a slice of a byte stream and not a sentence, so `ble.js` splits on line ends rather than on notifications, the way an EFB does.

Writing to it reaches skyBlip's `on_stream_write`, which discards everything: that characteristic exists because XCSoar refuses a UART service without one (`firmware/hardware/platform/zephyr/link.cpp`). SoftRF is the opposite and takes its whole configuration there, as `$PSRFC` sentences. `$PSRFC,?` dumps the current settings; echoing that line back with one field edited and field 1 set to the protocol version (`1`) stores it, after which SoftRF drops Bluetooth, writes EEPROM and reboots. A sentence typed without a `*` leaves with its checksum appended, because SoftRF drops what it cannot verify. Send all 19 fields: an empty term still counts as updated and reads back as zero.

**The config endpoint** is skyBlip's own, `69c21302`, one JSON command per write and one reply per notification. The buttons cover `get`, `status`, `timing`, `flash`, `radio`, `update` and `diag`; the text field sends anything else, `set` included. The rules the firmware enforces are in `firmware/core/comms/README.md`: the first session to write claims the endpoint, a `set` is refused in flight and on a low cell, and what survives those two is staged for a button press on the device.

A command travels in one write because it is parsed per frame. `ble.js` writes config with response and chunks only the serial stream, where 20 bytes is what BLE guarantees and Web Bluetooth exposes no MTU to do better.

## Next

A settings page and an update page belong beside these, sharing `ble.js`. The update path is already decided on the firmware side: MCUmgr/SMP carries the image, and authorisation is the physical confirmation window `ConfigService::upload_allowed()` opens, because encrypted GATT characteristics are unreliable under Web Bluetooth. `dfu` opens that window, `apply` swaps an image the device watched arrive whole since that window opened (refused as `nothing_staged` for an empty slot and `upload_unfinished` for an upload that stopped short or came before a restart), and both refuse on a cell too low to survive the swap.
