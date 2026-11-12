# What the factory bootloader reports

`INFO_UF2.TXT` is copied verbatim, CRLF line ends kept, off the `X1` volume of the owner's first MeshTracker X1, read on 12 November 2026 before anything had been installed on it. Its SHA-256 is `72ff1d4205348d771a29648b19ede0344c18de33c68844f3c9a0b8c67b791c85`. It is the evidence for what the partition map in `../meshtracker_x1.dts`, the window in `scripts/mkuf2.py` and the switch-off byte in `../pins.h` assume, so it is kept rather than quoted.

The same day the owner read the unit's `CURRENT.UF2`, SHA-256 `a3710f3a894dd75d16a926c93049ae1f210ed754077c5cdd026a56042a205780`. It is Meshtastic's build, 2.7.23.2546839, so it is not in this repository. What this README says it shows was measured on that unit, not read from source.

The version line names commit `b93789fe53e1e096912439e52fa990cf98bed81e` of `meshtastic/nRF52_Bootloader`, "Change mesh tracker x1 info", committed on 12 August 2026, the file's `Date`. That commit sets the `Model` line of the `mesh_tracker_x1_v2` board, and the `mesh_tracker_x1` board at the same commit still says `Seeed MeshTracker-X1 for Meshtastic`. Both boards carry the same `Board-ID`, so the `Model` line is what names the v2 board, and only the v2 board has the button route below. The commit is on no release tag. Every `file:line` below is at that commit.

| The file says | What depends on it |
|---|---|
| `UF2 Bootloader 0.10.0-18-gb93789f` | the source every line below is read from |
| `Model: Seeed MeshTracker-X1` | `UF2_PRODUCT_NAME`, `src/boards/mesh_tracker_x1_v2/board.h:61`: the board is `mesh_tracker_x1_v2`, whose button route is below |
| `Board-ID: nRF52840-MeshTracker-X1-v1` | `UF2_BOARD_ID`, `board.h:63` |
| `Date: Aug 12 2026` | the build, the day of the commit |
| `SoftDevice: S140 7.3.0` | `softdevice_partition`, 0x0..0x27000, and `mkuf2.py`'s refusal to write below it |

A unit that reports a different bootloader or SoftDevice has not been checked against any of this. Read its `INFO_UF2.TXT` first, and keep its `CURRENT.UF2` before installing anything.

## The SoftDevice

`src/boards/mesh_tracker_x1_v2/board.mk:2` sets `SD_VERSION = 7.3.0`. Its `nrf_sdm.h:144` gives `SD_FLASH_SIZE` 0x26000 and `nrf52/nrf_mbr.h:68` gives `MBR_SIZE` 0x1000, both under `lib/softdevice/s140_nrf52_7.3.0/s140_nrf52_7.3.0_API/include/`. So the SoftDevice ends at 0x27000, and the application, which on this firmware is MCUboot, starts there.

Measured in `CURRENT.UF2`: the SoftDevice's info struct holds its magic, 0x51B1E5DB, at 0x3004, its end, 0x27000, at 0x3008, and its firmware ID, 0x0123, at 0x300C. Its last byte that is not 0xFF is at 0x26497, and Meshtastic's vector table starts at 0x27000.

## The write window

The window a `.uf2` may write is `USER_FLASH_START..USER_FLASH_END` = `MBR_SIZE .. BOOTLOADER_REGION_START - DFU_APP_DATA_RESERVED` (`src/usb/uf2/uf2cfg.h:25-26`). A release build links the bootloader at 0xF4000 (`linker/nrf52840.ld:14`), which is `BOOTLOADER_REGION_START` for the nRF52840 in `lib/sdk11/components/libraries/bootloader_dfu/dfu_types.h:93`, and `Makefile:139` sets `DFU_APP_DATA_RESERVED` to `10*4096`. So the window is 0x1000..0xEA000, which is what `scripts/mkuf2.py` guards and `scripts/test_mkuf2.py` asserts. A debug build, `DEBUG=1` (`Makefile:383-396`), reserves nothing and moves the bootloader to 0xEA000: `USER_FLASH_END` is the same, but the bootloader would then sit in `storage_partition`.

Measured in `CURRENT.UF2`: 3728 valid blocks of 256 bytes cover 0x1000..0xEA000 without a gap, and nothing at or above 0xEA000. That is the window, read back off the unit. It does not show where the bootloader sits, which is the start address in `UICR` at 0x10001014, and #169's SWD session can read it.

`CURRENT.UF2` holds the SoftDevice and Meshtastic. Dragging it back onto the `X1` volume restores both. It holds nothing at or above 0xEA000, so Meshtastic's own settings there are not in it.

## The button

The board defines `BUTTON_DFU_V2` as P0.06 (`board.h:43`), and `board_init` makes it an input with a pull-down (`src/boards/boards.c:86`). If nothing has asked for DFU, `check_dfu_mode` reads it every 50 ms (`src/main.c:286-315`). A read that finds it released ends the loop and the boot goes on to the application. Sixty reads in a row that find it held, about 3 s, set `uf2_dfu` and light the LED white, red, green and blue together (`board.h:32-35`). Seeed's wiki gives the same route: hold the button, plug the cable, and keep holding until the LED is white (`Seeed-Studio/wiki-documents`, `sites/en/docs/Network/Mesh_Network/MeshTracker_X1/Meshtastic/get_start_with_x1.md:203` and `:485`, at `da5251e8`). The same page has the cable reset the unit when the button is held as it goes in, and released at once. What the reset circuit does is #169's to measure.

The loop blocks while the button is held, up to the 3 s, so the application starts only once a wake press is released, and the application then reads the button up. `core/power/wake.h` lets that boot run on the reset cause alone: a button wake from SYSTEM OFF reports `LowPowerWake` and no `UsbVbus` (`test/products/skyblip_bravo/test_power.cpp`). The bootloader changes nothing for a cable wake, which it lets through with or without the byte. Whether `boot_path` refuses one on silicon is a separate question: the nRF52840 may report it as `UsbVbus` without the `LowPowerWake` the refusal needs.

A UF2 entry from the hold waits 3 s for USB to enumerate, then goes on into the application (`main.c:372-374`). So a wake press held past 3 s on the cell boots about 3 s after the LED turns white, and with the cable in it opens the `X1` drive.

## The skip byte

`DFU_MAGIC_SKIP` is 0x6d (`main.c:110`). With it in GPREGRET, `check_dfu_mode` clears GPREGRET and returns before it reads the button (`main.c:254`, `:266-273`). This bootloader has no line that sends a VBUS wake back to SYSTEM OFF, unlike the T-Echo's (`../../../lilygo/t_echo_plus/factory/README.md`).

So on the X1 the byte buys nothing: a wake press is under 3 s and boots anyway. And it costs a recovery: the first boot after a switch-off that wrote it skips the button, so a 3 s hold from off starts the application instead of opening the drive. Seeed's hold-and-plug may still reach the drive, because the wake already cleared the byte, but only if the cable resets the SoC as Seeed's page says. The X1 writes 0x00 at switch-off instead (`kFactoryDfuButton` in `../pins.h`, `boot_magic_for_system_off` in `core/power/shutdown.h`). It matches no magic, so the bootloader leaves it in place and reads the button. Writing it, rather than nothing, keeps what the bootloader reads after a switch-off independent of what the register held. The smoke image writes the same byte.

`DFU_MAGIC_UF2_RESET`, 0x57 (`main.c:109`), stays the recovery's. It sets `dfu_start`, lights the LED white (`main.c:287-291`) and takes the same 3 s USB wait (`main.c:372-374`).

## The double-reset word

`DFU_DBL_RESET_MAGIC` is 0x5A1AD5 in the word at 0x20007F7C (`main.c:112-115`), which the bootloader's linker script reserves (`linker/nrf52840.ld:29`). Commit `48af1a75`, "Enable web dfu enter", forces `reason_reset_pin` false (`main.c:257`) and drops it from the test (`main.c:262-263`). So that word holding the magic enters DFU on any reset, with no USB wait (`main.c:375-377`), and on the cell the unit then stays in the bootloader. The bootloader never writes the magic itself, because `main.c:343-346` runs only on a reset-pin reset, and it clears the word before it starts the application (`main.c:353-357`).

0x6d used to skip this test after a switch-off as well, so with 0x00 the word is read on every boot. This firmware sets no RAM retention for SYSTEM OFF, so after a wake a match needs a random word to equal the magic. A warm reset keeps whatever the application left there. In the images CI built from `052f4a5` (run 43261137340), neither MCUboot nor the application holds 0x5A1AD5 as a literal. Which variable, if any, sits at 0x20007F7C needs the ELF map, which CI does not keep, so it stays unverified.

The other word the bootloader reads is at 0x27200, the application origin plus 0x200: 0x87eeb07c there would enter DFU on every boot (`main.c:123`). In the smoke and Bravo images of that run it is 0x47f0e92d.

## USB

The bootloader enumerates as 2886:0057, for UF2 and for CDC alike (`board.h:54-56`), and its volume is labelled `X1` (`board.h:62`). It takes an application block under two families (`src/usb/uf2/ghostfat.c:419-420`): `CFG_UF2_FAMILY_APP_ID`, 0xADA52840 for the nRF52840 (`uf2cfg.h:17`), which `mkuf2.py` writes, and `CFG_UF2_BOARD_APP_ID`, 0x28860057, the VID over the PID (`uf2cfg.h:11-13`). Measured: every block of `CURRENT.UF2` carries flag 0x2000, family present, with 0x28860057.
