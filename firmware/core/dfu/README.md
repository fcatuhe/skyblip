# core/dfu

What the firmware decides about an update image: how it reads MCUboot's header and TLVs (`image.h`), the record a unit writes before a swap and what the next boot makes of it (`update.h`), what the unit writes down about its last install (`install_report.h`), which SMP commands a phone may send (`smp_policy.h`), and which product an image is for (`product_check.h`). The Zephyr side is `hardware/platform/zephyr/dfu.h` and `mcumgr_hooks.cpp`, and the product rules for a whole update are in `products/common/services/config_service.cpp`.

## One product's images only

A skyBlip Go never installs a skyBlip Bravo image over the air, and a Bravo never installs a Go one. Both products sign with the same key on each channel, so the signature cannot tell them apart. Every image names its product in a protected TLV instead, and the upload hook refuses an image whose product is not the running image's.

### The mark

The mark is MCUboot's image class, `IMAGE_TLV_UUID_CID` (0x75), with the vendor beside it, `IMAGE_TLV_UUID_VID` (0x74). `products/common/prj.conf` and each `products/<product>/prj.conf` set them through Zephyr's `CONFIG_MCUBOOT_IMGTOOL_UUID_VID`, `_VID_NAME`, `CONFIG_MCUBOOT_IMGTOOL_UUID_CID` and `_CID_NAME` (Zephyr v4.4.2, `modules/Kconfig.mcuboot:125-158`), which Zephyr's `cmake/mcuboot.cmake` passes to imgtool as `--vid` and `--cid`. imgtool derives each UUID with `uuid5` and adds both to the protected TLVs, which the image's hash and signature cover (MCUboot `docs/design.md`, Protected TLVs).

| TLV | Name it is derived from | UUID |
|---|---|---|
| 0x74, vendor | `skyblip.eu`, in the DNS namespace | `b6e3b0ae-1d5b-55ea-8007-e9e12850d63a` |
| 0x75, class | `skyblip_go`, in the vendor's namespace | `d1d4d665-5f2b-5695-ad59-f122902c2120` |
| 0x75, class | `skyblip_bravo`, in the vendor's namespace | `46d97790-da40-5396-a354-779456a09465` |

The class name is the product's directory under `products/`, also its link name (`dfu::Product`). `scripts/build_image.sh` fails a build whose images do not carry their product's class (`scripts/check_image_product.py`).

### Why a TLV and not a key per product

- MCUboot built the class for this case: `CONFIG_MCUBOOT_UUID_CID` is there "to prevent processing images for a different CPU or device produced by the same vendor" (MCUboot `boot/zephyr/Kconfig:1192-1196`, at `6d3b3d2c`, the revision Zephyr v4.4.2 pins).
- The key already means the channel (`firmware/keys/README.md`), and a key per product would double the private keys for a boundary the signed TLV already draws.
- A key per product would not refuse earlier: the key hash, like the class, sits in the TLV area after the body.

A key per product would make MCUboot refuse the foreign image at boot, which the TLV does not do yet (Open, below).

### Where the unit refuses

`mcumgr_hooks.cpp` listens to `MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK`, which Zephyr's img_mgmt raises before it writes each chunk of every upload (Zephyr v4.4.2 `subsys/mgmt/mcumgr/grp/img_mgmt/src/img_mgmt.c:837-857`). So a bare `mcumgr` upload meets the same check as the update page.

`ProductCheck` reads the header off the first chunk, then copies the protected area and the 4-byte info of the TLV area after it as the chunks pass. On the chunk that completes them, it compares the image's class with the class of the image running in slot0, and the upload's declared length with the image's end. A different class, a missing one, or a length that is not exactly the image's refuses that chunk before img_mgmt writes it, with `IMG_MGMT_ERR_INVALID_TLV` (5), and every later chunk until a new upload starts at offset 0. Nothing reaches a swap.

- The check refuses at the first chunk an image with no protected TLVs, a protected area wider than the 128 bytes it holds (`scripts/check_image_product.py` fails such a build), a declared length that ends before that area, and a unit that cannot read its own class. img_mgmt never holds the declared length to the header, so an upload that stopped short of the class would otherwise raise `PENDING` over bytes an earlier upload left.
- A Go image carries 85 bytes of protected TLVs: the 4-byte area header, the vendor and the class at 20 bytes each, and the hub image's two at 36 and 5 (`products/skyblip_go/sysbuild.cmake`).
- The check compares with what slot0 runs, so no class is compiled in.
- An upload declared longer than its image is refused, because a swap can carry slot1's sector past a shorter image onto slot0's first page past it, where Briefing's `.uf2` leaves the install trust record (`../comms/README.md`, The browser that installed the unit). imgtool writes nothing past the TLV area, so every image we sign uploads as exactly its own length.

A first chunk the hook refuses leaves the check on the upload before it. img_mgmt keeps that upload when it refuses a first chunk, and resumes it without asking the hook when the phone sends its SHA again at offset 0 (Zephyr v4.4.2 `zephyr_img_mgmt.c:724-731`). So an upload that a refused image interrupted still passes when the phone sends it again.

The update page offers a unit only its own product's images, by the `identity` frame, and reads the class off the file before it sends anything (`server/app/javascript/skyblip/image.js`). It words error 5 as `wrong_product`, or `unmarked_image` for a file naming no product.

### Open

- MCUboot does not check the class at boot yet. MCUboot 2.4.0 calls `boot_uuid_init()` and `boot_uuid_cid_match()` under `CONFIG_MCUBOOT_UUID_CID`, but nothing in its tree defines them. Until a bootloader implements them, the app's hook is the boundary, and `smp_policy.h` refuses every SMP image state write and reset so an image reaches a swap only through the app.
- A Go on firmware built before this check takes any image signed by its key and names no product, so a Bravo image chosen by hand reaches it.
- An image built before this check names no product, so a newer unit refuses it over the air. Go back to one by drag-and-drop of its `.uf2`. A `.uf2` built before November 15, 2026 hangs a unit that has swapped over Bluetooth, and a current one recovers it (`scripts/mkuf2.py` blanks the swap status).
- The hook has run only on `native_sim`, in `test/twister/smp_gate`. The refusal on an nRF52840 is a hardware acceptance item of #187.

## The keys the bootloader verifies

A unit installs over the air only an image signed by a key its MCUboot verifies, so an image MCUboot would refuse at boot is refused before the restart. `bootloader_keys.h` reads those keys off the MCUboot image in `boot_partition`. MCUboot holds each P-256 key as its DER SubjectPublicKeyInfo, 91 bytes that open on the same 26-byte header (`autogen-pubkey.c`, which `imgtool getpub` writes), and imgtool's KEYHASH TLV is the SHA-256 of those 91 bytes. So the unit finds every key by its header, hashes each, and compares the staged image's KEYHASH with them.

The bootloader changes only by drag-and-drop, which restarts the app, so `hardware/platform/zephyr/dfu.h` reads it once. A unit that cannot read it, or finds no key, falls back on the key that signed the running image. The `update` frame names the keys as `bootloader_keys`, and `ConfigService::signed_by_trusted_key()` applies the rule when an upload lands (`firmware/keys/README.md`).

## Before the restart

`ConfigLinkService::install()` asks `Dfu::request_swap()` before it restarts the unit. On the device, `request_swap()` hashes slot1's header, body and protected TLVs as MCUboot does, and compares the digest with the image's SHA-256 TLV (`check_image()` in `image.h`). Only an image that matches is marked for a test swap with `boot_request_upgrade()`. The unit reads MCUboot's swap type back into the install report, then restarts whatever the result, because it has already parked its parts and the old image comes back to say what went wrong.

## The install report

The install report says why an update did not land, so the briefing app's events diagnose a unit far away (#278). It has two parts.

The install's part is one entry of 9 bytes in NVS, under the key `install.report`, beside the update record:

| Field | Written by | What it says |
|---|---|---|
| `check` | the image that sends the update, before the restart | what `check_image()` found in slot1: `matched`, `damaged`, `unreadable` or `undigested` |
| `swap`, `request_error` | the same image | the swap type MCUboot reads back, and what `boot_request_upgrade()` returned |
| `boots` | every image that boots while the record stands | the restarts since the install |
| `probation_boots`, `proof` | the new image, on probation | how many times it ran, and how far its last run got: `booted`, `started`, `gnss_heard`, `hub_bootable`, `proven` or `confirm_failed` |
| `reset` | every image that boots | the reason for the latest restart. After a revert, it names what ended the new image's run |

The install's part lives as long as the update record. `install()` writes a new one, and an image that confirms itself, or that finds an image the record does not name, erases both. A boot that finds no record erases the report too. The new image writes at most six times during its probation, unplaced, because only the first minute after an install pays for them.

The landing's part is what `ConfigService::refuse_landing()` finds when it refuses an upload that landed, `nothing_staged`, `not_approved` or `wrong_key`. Those words name what failed, not why:

| Field | What it says |
|---|---|
| `landed_check` | what `check_image()` found in slot1 at the refusal. `matched` means slot1 holds the whole image its digest names, so the refusal is about the image. Anything else means the flash does not hold what was sent, or would not read |
| `staged_key` | the first four bytes of the KEYHASH the unit read in slot1, beside the running key the `update` frame names. Absent when the unit found none |

The unit keeps the landing's part in RAM from the refusal until the next upload window opens or the unit restarts. It hashes slot1 only on a refusal, so an upload that installs is hashed once, before the restart. A host asks for the report with the bare `install_report` command, and the answer is 30 bytes at its widest.

### When an update does not land

The briefing app's firmware page asks for the report on every connect and names the cause. It sends every report to skyblip.eu, the install's part as `device.install_reported` and a refused landing as `device.upload_refused`, so the events alone say which row below applies.

On the bench, before anything else touches the unit, run `./scripts/blip.py --name "skyBlip Bravo" command '{"command":"update"}'`, then `command '{"command":"install_report"}'`.

| What the report says | What happened |
|---|---|
| `wrong_key`, `landed_check` `matched`, `staged_key` none of `update`'s `bootloader_keys` | the image is signed by a key the unit's bootloader does not verify. Install an image signed by one of them. A unit that names no `bootloader_keys` installs only images signed by its `key` |
| a landing refusal, `landed_check` other than `matched` | slot1 does not hold the image that was sent, or the flash would not read it back. The upload or the flash is at fault, not the image |
| `wrong_key`, `landed_check` `matched`, no `staged_key` | slot1 holds the whole image, but the unit could not find its KEYHASH: a bug in how the unit reads slot1's TLVs |
| no report, and `update` says `confirmed` on the old version | the unit never wrote its update record: the store was not ready, or the write failed |
| `check` other than `matched` | slot1 does not hold the image that was sent, or the flash refused to read it. Send the image again |
| `matched`, then `swap` other than `test` or a `request_error` | the swap flag never reached slot1's trailer, so MCUboot booted the old image |
| `matched`, `test`, `probation_boots` 0 | MCUboot refused the image at boot without running it: its signature, its key or the slot geometry |
| `probation_boots` 1 or more | the new image ran and never confirmed. `proof` says where it stopped, and `reset` what ended it |
