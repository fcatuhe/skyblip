# simulator

Two pages served from one build directory. `index.html` drives the WASM build of the firmware with simulated sensors; `console.html` talks to a device that exists, over Web Bluetooth. `make dev` serves both on `sim.skyblip.localhost:5114`, which Chrome treats as a secure origin, so the console works there without a certificate.

## The console

`ble.js` is the transport and knows nothing about what a page draws: it connects, reassembles the stream, and hands whole lines and parsed replies back. `console.html` is the UI. `test_ble.mjs` pins down the reassembly, because a sentence cut across two notifications is the failure that looks like a firmware bug from the page.

The picker lists anything advertising Nordic UART or HM-10, which is skyBlip and SoftRF both, so the same page is how you read one and configure the other.

**The serial endpoint** is the traffic picture: `$PFLAU`, `$PGRMZ`, `$LK8EX1`, a `$PFLAA` per target, `$GPRMC`/`$GPGGA` with a fix. A notification is a slice of a byte stream and not a sentence, so `ble.js` splits on line ends rather than on notifications, the way an EFB does.

Writing to it reaches skyBlip's `on_stream_write`, which discards everything: that characteristic exists because XCSoar refuses a UART service without one (`firmware/hardware/platform/zephyr/link.cpp`). SoftRF is the opposite and takes its whole configuration there, as `$PSRFC` sentences. `$PSRFC,?` dumps the current settings; echoing that line back with one field edited and field 1 set to the protocol version (`1`) stores it, after which SoftRF drops Bluetooth, writes EEPROM and reboots. A sentence typed without a `*` leaves with its checksum appended, because SoftRF drops what it cannot verify. Send all 19 fields: an empty term still counts as updated and reads back as zero.

**The config endpoint** is skyBlip's own, `69c21302`, one JSON command per write and one reply per notification. The buttons cover `get`, `status`, `timing`, `flash`, `radio`, `update` and `diag`; the text field sends anything else, `set` included. The rules the firmware enforces are in `firmware/core/comms/README.md`: the first session to write claims the endpoint, a `set` is refused in flight and on a low cell, and what survives those two is staged for a button press on the device.

A command travels in one write because it is parsed per frame. `ble.js` writes config with response and chunks only the serial stream, where 20 bytes is what BLE guarantees and Web Bluetooth exposes no MTU to do better.

## The update client

`update.js` is what the website's update page runs (`website/content/pages/manage.*`), and it knows nothing about the page either: it holds the device's update dialect as a state machine and hands every change to one callback. The page renders that state and looks the words up in its own tables, so the two languages live in the page and not here. `bin/simulator-build` copies these modules into `website/public/` verbatim, which is why they import each other by relative path and need no bundler.

The update is two channels, and the device refuses the usual mcumgr sequence on purpose (`firmware/core/comms/config.cpp`, `core/dfu/smp_policy.h`):

1. `{"cmd":"dfu"}` on the config endpoint. The device answers `confirm_dfu` and asks on its glass; two presses answer `{"ack":true,"reason":"dfu"}` and open a ten-minute upload window, on the ground only.
2. The image over MCUmgr/SMP, image group, upload command: the only SMP write the device accepts, and only inside that window. `smp.js` sends SMP v2 headers, so the image group's own error codes come back (`err`) rather than the generic `rc` the original protocol folds them into; a refusal by the command hook is still a bare `rc` 11.
3. `{"cmd":"apply"}`, `confirm_apply`, two presses again. An SMP `image state` write or `os reset` is refused, so none is sent.
4. The device paints INSTALLING, reboots into the swap, and the link drops. On the next connect it pushes the `update` frame unasked whenever the image is not confirmed, or (with #90) its settings fell back, and `update.js` reads that frame whenever it comes.

Every refusal is a `reason` on the config endpoint or an error on the SMP one, and the state machine passes it on as a notice key without deciding what it means. A key the page has no words for is still shown, under its own name.

Sizes come from the device, never from a constant. Web Bluetooth exposes no MTU, but smp_bt cuts every reply at MTU-3, so before the first upload `smp.js` sends an OS echo longer than any ATT payload, in 20-byte writes, and reads the write size off the first slice of the reply. This device's local ATT MTU is 498 (`CONFIG_BT_BUF_ACL_RX_SIZE` 502 less the L2CAP header, and `CONFIG_BT_L2CAP_TX_MTU`), so a Chrome that asks for 517 settles on 498: 495-byte writes. Each image-upload request is then one write of that size, carrying about 470 bytes of image, the way mcumgr-web sizes a chunk to one write. If the browser refuses a write anyway, the size steps down through 244 and 182, the payloads of MTU 247 and 185, to 20, and the upload resumes. A link that never exchanged its MTU, or a device that does not echo, gets the whole `buf_size` packet (`CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE`, read from the OS group's parameters) in 20-byte slices, which `CONFIG_MCUMGR_TRANSPORT_BT_REASSEMBLY` puts back together. Replies are reassembled on the length in the header. The first upload packet carries the image's SHA-256: img_mgmt answers a second upload of the same image with the offset it reached, so a window that closed mid-image resumes rather than restarts, as long as the device has not rebooted.

`ble.js` queues every GATT write, config and SMP alike, behind the one before it, because a write issued while another is in flight can fail.

`cbor.js` is the smallest codec SMP needs: integers, strings, byte strings, arrays, maps, booleans and null, definite and indefinite length (zcbor opens every map as indefinite), floats and tags on the way in only.

### Tests

`make test` runs every `test_*.mjs` on node's own runner, and CI runs the same. `test_cbor.mjs` holds the codec to the RFC 8949 appendix, `test_smp.mjs` the framing, the packet budget and the upload's offsets, and `test_update.mjs` the whole flow against `fake_skyblip.mjs`: a skyBlip behind a fake `navigator.bluetooth`, which answers the config dialect as `config.cpp` does (with `upload_unfinished` from #89, the `settings` key from #90 and the status keys from #87) and the SMP one as Zephyr's img_mgmt does. The fake also truncates or refuses a write longer than its MTU, as a browser might. The fake is a reading of that code, not the code, so a green run says the page agrees with the reading. Nothing here has run against a device.

## Next

A settings page belongs beside these, sharing `ble.js`.
