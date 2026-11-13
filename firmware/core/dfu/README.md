# core/dfu

What the firmware decides about an update image: how it reads MCUboot's header and TLVs (`image.h`), the record a unit writes before a swap and what the next boot makes of it (`update.h`), what the unit writes down about its last install (`install_report.h`), which SMP commands a phone may send (`smp_policy.h`), and which product an image is for (`product_check.h`). The Zephyr side is `hardware/platform/zephyr/dfu.h` and `mcumgr_hooks.cpp`, and the product rules for a whole update are in `core/comms/config.cpp`.

## One product's images only

A skyBlip Go never installs a skyBlip Bravo image over the air, and a Bravo never installs a Go one. Both products sign with the same key on each channel, so the signature cannot tell them apart. Every image names its product instead, in a protected TLV, and the upload hook refuses an image whose product is not the running image's.

### The mark

The mark is MCUboot's image class, `IMAGE_TLV_UUID_CID` (0x75), with the vendor beside it, `IMAGE_TLV_UUID_VID` (0x74). `products/<product>/prj.conf` sets them through Zephyr's `CONFIG_MCUBOOT_IMGTOOL_UUID_VID`, `_VID_NAME`, `CONFIG_MCUBOOT_IMGTOOL_UUID_CID` and `_CID_NAME` (Zephyr v4.4.2, `modules/Kconfig.mcuboot:125-158`), which `cmake/mcuboot.cmake:125-131` passes to imgtool as `--vid` and `--cid`. imgtool derives each UUID with `uuid5` (MCUboot `scripts/imgtool/image.py`, `parse_uuid`) and adds both to the protected TLVs (`image.py:661-673`), which the image's hash and signature cover (MCUboot `docs/design.md`, Protected TLVs).

| TLV | Name it is derived from | UUID |
|---|---|---|
| 0x74, vendor | `skyblip.eu`, in the DNS namespace | `b6e3b0ae-1d5b-55ea-8007-e9e12850d63a` |
| 0x75, class | `skyblip_go`, in the vendor's namespace | `d1d4d665-5f2b-5695-ad59-f122902c2120` |
| 0x75, class | `skyblip_bravo`, in the vendor's namespace | `46d97790-da40-5396-a354-779456a09465` |

The class name is the product's directory under `products/`, which is also its link name (`dfu::Product`) and, with dashes, the start of its release files. `scripts/build_image.sh` fails a build whose slim, full or confirmed image does not carry its product's class (`scripts/check_image_product.py`).

### Why a TLV and not a key per product

The decision was between the two, and the TLV wins on the evidence:

- MCUboot built the class for this case. The help of `CONFIG_MCUBOOT_UUID_CID` says it is there "to prevent processing images for a different CPU or device produced by the same vendor" (MCUboot `boot/zephyr/Kconfig:1192-1196`, at `6d3b3d2c`, the revision Zephyr v4.4.2 pins in its `west.yml`).
- The key already means the channel. `firmware/keys/README.md` ties the bootloader's downgrade rule to the key, and `firmware.yml` and `release.yml` verify every image against `development.pub.pem` or `production.pub.pem`. A key per product would double the private keys in both GitHub environments and the public keys CI checks, for a boundary the signed TLV draws already.
- A key per product would not refuse earlier. The key hash, like the class, sits in the TLV area after the body (`docs/design.md`, Protected TLVs), so the hook would wait for the same last bytes either way.

A key per product does buy one thing the TLV does not have yet: MCUboot would refuse the foreign image at boot. See the open items below.

### Where the unit refuses

`mcumgr_hooks.cpp` listens to `MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK`. Zephyr's img_mgmt raises it before it writes each chunk of every upload, whatever client sent it, the update page or a bare `mcumgr` (Zephyr v4.4.2 `subsys/mgmt/mcumgr/grp/img_mgmt/src/img_mgmt.c:837-857`). So a direct mcumgr upload meets the same check as the page.

`ProductCheck` reads the header off the first chunk and copies the protected area as the chunks pass. On the chunk that completes that area, it compares the image's class with the class of the image in slot0, which is the image running. A different class or a missing one refuses that chunk before img_mgmt writes it, with the image group's `IMG_MGMT_ERR_INVALID_TLV` (5), and every later chunk of that upload until a new one starts at offset 0. The slot never receives the signature that follows, img_mgmt never raises `PENDING`, and `ConfigService` never installs: nothing reaches a swap.

The check refuses at the first chunk when the image has no protected TLVs at all, when its protected area is wider than the 128 bytes the check holds (`scripts/check_image_product.py` fails a build past it), when the upload's declared length ends before that area does, and when the unit cannot read its own class. img_mgmt never holds the declared length to the header, and a progressive erase leaves the slot past it as it was, so an upload that stopped short of the class would otherwise pass every chunk and still raise `PENDING` over bytes an earlier upload left. A Go image carries 85 bytes of protected TLVs: the 4-byte area header, the vendor and the class at 20 bytes each, then the hub image's two TLVs at 36 and 5 (`products/skyblip_go/sysbuild.cmake`). The check compares with what slot0 runs rather than with a constant, so no class is compiled into it, and the class a unit expects is the one its own build signed.

The `update` frame names the product the unit runs, `product` in `schemas/link.v1.json`, so the page offers a unit only its own product's published images. A Go leaves the key out, which every reader takes for `skyblip_go`, so its frame keeps the width it had at a 20-byte link. The update page reads the same class off the file before it sends anything (`server/app/javascript/skyblip/image.js`). It takes a unit for a Go unless the unit names another product, its `update` frame unread included, so a Go on firmware without the check never receives a Bravo image from it. It refuses a file naming no product before anything is sent to a unit that names its product, and words error 5 as `wrong_product`, or as `unmarked_image` for a file naming no product.

### Open

- MCUboot does not check the class at boot yet. MCUboot 2.4.0 has the check, `CONFIG_MCUBOOT_UUID_CID` in `boot/bootutil/src/image_validate.c`, but no implementation of the `boot_uuid_init()` and `boot_uuid_cid_match()` it calls: `boot/bootutil/include/bootutil/mcuboot_uuid.h` declares them and nothing in the tree defines them. Until a bootloader implements them, the app's hook is the boundary. An image reaches a swap only through the app, because `smp_policy.h` refuses every SMP image state write and reset. Turning the bootloader's check on needs that implementation, a new bootloader on every unit and a bench test on silicon.
- A Go on firmware built before this check takes any image signed by its key. The page reads a unit that names no product as a Go, and offers it only Go images.
- An image built before this check names no product, so a unit on a newer image refuses it over the air. Going back to one is a drag-and-drop of its `.uf2`. A Go leaves `product` out of its frame, so the page cannot tell a Go with the check from one without it: it sends such a file to a Go, and a Go with the check refuses it once the upload reaches its protected area, near its end.
- The hook itself has run only on `native_sim`, in `test/twister/smp_gate`. The refusal on an nRF52840, through the page and through `mcumgr`, is a hardware acceptance item of #187.

## Before the restart

`ConfigLinkService::install()` asks `Dfu::request_swap()` before it restarts the unit. On the device, `request_swap()` hashes slot1's header, body and protected TLVs, as MCUboot does, and compares the digest with the image's SHA-256 TLV (`check_image()` in `image.h`). Only an image that matches is marked for a test swap with `boot_request_upgrade()`. The unit then reads MCUboot's swap type back from the slots and prints one console line, `dfu: slot1 <check>, swap request <errno>, swap type <type>`. It restarts whatever the result, because it has already parked its parts, and the old image comes back to say what went wrong. `test/core/test_dfu_check.cpp` holds the check to an image `imgtool sign` made, protected TLVs included.

## The install report

The install report is troubleshooting for #278. #294 retires it once Bluetooth updates install and confirm in the field. It is one entry of 9 bytes in NVS, under the key `install.report`, beside the update record:

| Field | Written by | What it says |
|---|---|---|
| `check` | the image that sends the update, before the restart | what `check_image()` found in slot1: `matched`, `damaged`, `unreadable` or `undigested` |
| `swap`, `request_error` | the same image | the swap type MCUboot reads back, and what `boot_request_upgrade()` returned |
| `boots` | every image that boots while the record stands | the restarts since the install |
| `probation_boots`, `proof` | the new image, on probation | how many times it ran, and how far its last run got: `booted`, `started`, `gnss_heard`, `hub_bootable`, `proven` or `confirm_failed` |
| `reset` | every image that boots | the reason for the latest restart. After a revert, it names what ended the new image's run |

The report lives exactly as long as the update record. `install()` writes a new one, and an image that confirms itself, or that finds an image the record does not name, erases both. A boot that finds no record erases the report too, so an entry an older image left behind does not outlive the next boot of an image that knows the key. The new image writes at most six times during its probation, unplaced, because only the first minute after an install pays for them. A host asks for it with the bare `install_report` command, and the answer is 22 bytes at its widest.

### What it leaves behind

- On each unit: the `install.report` entry, 9 bytes, erased with the update record. A unit that went back to firmware without the report keeps it until a firmware that knows the key boots.
- On the link: the `install_report` command, 35, and the `staged_check`, `swap_type` and `proof` enums in `schemas/link.v1.json`. `proof` stays, because `ConfigLinkService::proof()` decides when an image confirms itself.
- In the firmware: `install_report.{h,cpp}` here; in `ConfigLinkService`, `report_`, `reported_`, `kReportKey`, `load_report()`, `write_report()`, `note_proof()` and `forget_report()`, called from `load_image_state()`, `record_update()`, `forget_update()`, `publish_image_state()` and `confirm_image_once_healthy()`; `ConfigService::set_install_report()` and `send_install_report()`; their tests in `test/core/test_dfu_install_report.cpp`, `test/products/test_install_report.cpp` and `test/products/test_link_payload.cpp`. `request_swap()` and its check stay: they keep a damaged image from reaching MCUboot.
- On the page: `server/app/javascript/skyblip/install_report.js`, the `install_report` ask in `update.js`, the report line in `firmware_controller.js` and `briefing/devices/firmwares/show.html.erb`, and the `report` words in `config/locales/briefing.*.yml`.
- `rg '#294'` finds a `TODO:` beside each part. The firmware that retires the report keeps one `erase("install.report")` at boot, so no unit keeps the entry.

### When an update does not land

From a distance: the user opens the unit's firmware page in the briefing app and connects. The page asks for the report whenever the unit says its image is not confirmed, names the cause in plain words, and shows one line with a Copy report button. The user sends that line. The unit keeps the report until its next install, so a page reopened days later still reads it.

On the bench, before anything else touches the unit: `./scripts/blip.py --name "skyBlip Bravo" command '{"command":"update"}'`, then `command '{"command":"install_report"}'`. Keep the USB console running with the loop in #278 to catch the `dfu:` line before the restart.

| What the report says | What happened |
|---|---|
| no report, and `update` says `confirmed` on the old version | the unit never wrote its update record: the store was not ready, or the write failed |
| `check` other than `matched` | slot1 does not hold the image that was sent, or the flash refused to read it. Send the image again |
| `matched`, then `swap` other than `test` or a `request_error` | the swap flag never reached slot1's trailer, so MCUboot booted the old image |
| `matched`, `test`, `probation_boots` 0 | MCUboot refused the image at boot without running it: its signature, its key or the slot geometry |
| `probation_boots` 1 or more | the new image ran and never confirmed. `proof` says where it stopped, and `reset` what ended it |
