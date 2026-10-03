# The BHI260AP firmware image

`BHI260AP.fw` is Bosch Sensortec's RAM image for the BHI260AP, vendored unmodified.

| | |
|---|---|
| Source | [boschsensortec/BHI2xy_SensorAPI](https://github.com/boschsensortec/BHI2xy_SensorAPI), `firmware/bhi260ap/BHI260AP.fw`, release v1.6.0 |
| Size | 103,676 bytes, a whole number of 32-bit words |
| SHA-256 | `318def511fd8eb762bf1e61cf02cfd6ecac70f8627d3bacfa7d24a1641f9e915` |
| Licence | BSD-3-Clause, Copyright (c) 2022 Bosch Sensortec GmbH |

It is the RAM variant, not `BHI260AP-flash.fw`: the Plus gives the hub no flash of its own, so the image is uploaded by the host at every boot (`../README.md`). The host keeps it in one of two places, below.

## How it reaches the part

A full image carries it and a slim one does not. Both are built from one commit, at one version, and both pin it by digest: `skyblip_imu_image()` in `cmake/skyblip.cmake` hashes this file into `bhi260ap_fw_sha256.inc` for every build, and turns it into a byte list (`bhi260ap_fw.inc`) only when `CONFIG_SKYBLIP_IMU_IMAGE_LINKED` is set. `hardware/platform/zephyr/imu_firmware.cpp` is the only translation unit that includes either, and the platform hands them to the board as `imu_firmware()`, empty in a slim image, and `imu_firmware_digest()`. The host build and the WASM simulator carry an eight-byte stand-in, and nothing in the repo is a 650 KB generated header anybody could be tempted to edit.

| | Full image | Slim image |
|---|---|---|
| File | `skyblip-go.full.signed.bin`, and every `.uf2` | `skyblip-go.signed.bin`, the usual update |
| Size over the air | 488,896 B (0.1.0+885) | 385,219 B, 21.2% less |
| Boots the hub from | its own copy | the copy on `imu_image_partition` |
| Writes that partition | when it differs from its own copy | never |

`scripts/build_image.sh` builds both, `SB_CONFIG_SKYBLIP_IMU_IMAGE_LINKED` in `products/skyblip_go/Kconfig.sysbuild` choosing which, and `scripts/size_check.py` gates the full one: the UF2 bootloader's write window is internal flash only, 0x1000..0xEA000 (`boards/lilygo/t_echo_plus/t_echo_plus.dts`), so a board that has never run our firmware can only be given a full image, and `slot0` stays sized by it. What the slim image buys is update time, not room in the slot.

## The copy on the external flash

`imu_image_partition` is the last 128 KB of `&ext_flash`, 0x1E0000, after `log_partition` so the log keeps its offset. It holds the largest BHI260AP RAM image Bosch ships short of the BME688 ones, and `imu_firmware.cpp` refuses to build an image that would not fit. It is its own partition, so the log cannot address a byte of it: `FlashRegion::erase_sector()` refuses an index past the partition it opened.

`parts::Bhi260ImageStore` keeps it. The first 256-byte page is a header, `SKBH`, a layout byte, the length and the SHA-256 of the payload, and the payload starts on the next page.

- **Checked before use.** The board hashes the whole payload once at bring-up, before the self test is drawn, and a slim image boots the hub from it only if the header's digest is the one the image pins and the payload hashes to it. The hub's own verify (`VERIFY`) stays behind that as a second check.
- **Header last.** A write erases the header's sector first, programs the payload a page at a time, writes the header, then reads it all back and hashes it. A power cut anywhere before the header leaves an erased header, which reads as nothing held rather than as something plausible.
- **Written by a confirmed full image only.** The board writes when a hub answered the bus scan, the partition does not hold the image's own digest, and the image is confirmed. A full image on probation writes nothing: if the bootloader reverts it, the slim image before it is never left facing a copy it does not pin. The rule is checked on every boot, so a hub fitted after the fact, a replaced flash part and a Bosch update are all the same case.
- **Paced like the log.** Every erase and page asks `bus::RfState::claim_flash_window()` for the same window the flight log uses, at most eight steps a pass. The whole write takes tens of seconds and never costs the radio a dwell.

## What a unit without it does

A slim image whose hub answers and finds no usable copy leaves the hub at `IDLE` and says `NOBLOB`: on the status page's `IMU` line, on the self test's IMU row (`BHI260AP NOBLOB`), and as `"imu"` in the `update` reply. Everything else runs, because the hub feeds the slip ball and nothing else.

It also never confirms itself. `ConfigLinkService::hardware_proven()` waits for `state.imu.bootable`, so a slim image sent to a unit that needed the full one stays on probation, and the next reset reverts it to the image before, which booted the hub.

A plain T-Echo has no hub to answer: it reports `"imu":"none"`, writes nothing, and takes the slim image for good.

## What the update page reads

The `update` reply's `"imu"` is `none`, `missing`, `corrupt`, `unreadable`, `writing`, or the first 8 bytes of the held copy's SHA-256 in hex. Both images carry two protected TLVs, signed with them: 0xa0, the SHA-256 this image pins, and 0xa1, 1 when it carries the image. `scripts/check_imu_image.py` fails the build of an image without them. The manage page compares the two and names the file a unit needs: the slim one when there is no hub or the copy is the one it pins, the full one otherwise (`simulator/README.md`).

A unit that needs the full image usually runs the slim one of the same version, so the page and the device both accept an image of the running version, and `dfu::outcome()` tells a landed update from a reverted one by image hash, not by version.

The dead space on the internal part is not worth the same conversation. `softdevice_partition` is 152 KB of S140 we never link against and the UF2 window does reach, but the factory bootloader decides where to start the application from what it finds there, so the reward is the space and the penalty is a unit that only comes back over SWD.

## Replacing it

Take the same path from a later release, check the first two bytes are `2B 66` (the `0x662B` magic the driver refuses an image without), keep the length a multiple of four, and update the table above. Nothing else moves: every build pins the new digest, so the next release's slim image refuses the old copy, the page names the full file for every unit with a hub, and the first full image each unit takes writes the new copy. The device says whether it worked without a debugger: the six-pack's turn coordinator draws its cage from the part being fitted and the ball from the part reporting, so an image the hub refuses to verify reads as an empty cage a few seconds after boot.
