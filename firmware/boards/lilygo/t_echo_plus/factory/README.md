# What the factory bootloader reports

`INFO_UF2.TXT` is copied verbatim off the TECHOBOOT volume of our own T-Echo Plus, read on 12 September 2026 before anything had been installed on it. It is the evidence for what the partition map in `../t_echo_plus.dts` assumes, so it is kept rather than quoted.

`t_echo/INFO_UF2.TXT` is the same file off our plain T-Echo, and it is byte for byte identical: the same bootloader, the same SoftDevice, and `Model: LilyGo T-Echo` on both. So the partition map holds on either board, and the bootloader cannot tell a Plus from a plain T-Echo. What is fitted is read by the bus scan instead (`../board.h`).

What it pins down:

| The file says | What depends on it |
|---|---|
| `SoftDevice: S140 version 6.1.1` | `softdevice_partition`, 0x0..0x26000, and `mkuf2.py`'s refusal to write below it |
| `UF2 Bootloader 0.6.1-2-g1224915` | the write window, below |
| `Board-ID: nRF52840-TEcho-v1` | the SoC variant, which is what picks the window's size |

At tag 0.6.1 the write window is `USER_FLASH_START..USER_FLASH_END` = `MBR_SIZE .. BOOTLOADER_REGION_START - DFU_APP_DATA_RESERVED` (`src/usb/uf2/uf2cfg.h:20-21`), and the nRF52840 branch of `Makefile:117` sets `DFU_APP_DATA_RESERVED = 10*4096`. With the region starting at 0xF4000 that is 0x1000..0xEA000, which is what `scripts/mkuf2.py` guards and `scripts/test_mkuf2.py` asserts. The device is two commits past that tag on LilyGO's build, and neither constant moved between them.

A unit that reports a different bootloader or SoftDevice version has not been checked against any of this. Read its `INFO_UF2.TXT` first: a smaller `USER_FLASH_END` silently truncates an install, and that is the failure the guards in `mkuf2.py` exist to catch.

## What it does with a wake

`0.6.1-2-g1224915` is Linar Yusupov's fork of the Adafruit bootloader (`lyusupov/Adafruit_nRF52_Bootloader`), two commits past the tag: `42d10b5` adds the board, and `1224915`, "Fix against unwanted wakeup by USB VBUS power on/off signal when battery is main power source", adds four lines to `src/main.c:181-184`. Adafruit's own master does not carry them. They run before the board is initialised, on every reset:

```c
if ((NRF_POWER->RESETREAS & POWER_RESETREAS_VBUS_Msk) && dfu_skip) {
  NRF_POWER->RESETREAS |= POWER_RESETREAS_VBUS_Msk;
  NRF_POWER->SYSTEMOFF = 1;
}
```

`dfu_skip` is GPREGRET holding `0x6d`, `DFU_MAGIC_SKIP`, the byte the application leaves there on its way to SYSTEM OFF. So while it is set, a cable plugged in wakes the SoC and the bootloader puts it straight back to sleep, before MCUboot and before the application. SoftRF, whose author wrote the fix, writes the same byte before every sleep (`src/platform/nRF52.cpp:3213-3226`), so for SoftRF giving up the cable is the point. GPREGRET is cleared only after that test, so the byte survives and every later cable is swallowed the same way. A GPIO wake does not set the VBUS bit, and the pin configuration survives the wake reset, so an armed button still wakes the unit through it.

The application cannot simply stop writing the byte. `BUTTON_DFU` is P1.10, the device's only button (`src/boards/lilygo_techo/board.h`), and without the skip the bootloader reads it on every boot: a press that wakes the unit is still held when it looks, and `dfu_start` comes up USB DFU with no timeout. `BUTTON_FRESET` is P0.11, the pad, and both together are BLE OTA. The skip also bypasses the double-reset window and its RAM word at `0x20007F7C`, whose contents through SYSTEM OFF are not defined, at the cost of a double-click of RESET not arming on the first boot after a power-off.

So the byte goes with the button (`core/power/shutdown.h`, `bootloader_passes`). A switch-off that arms the button writes `0x6d` and gives up the cable: nothing on that glass needs the refusal except `CHARGE BATTERY`, which stays until the press. A switch-off that withholds the button, the cutoff and a refusal on a flat cell, writes `0x00`: no press can wake that unit, so the cable is let through to the refusal, which re-arms the button and so writes `0x6d` on its own way down. The cost of `0x00` is the bootloader's ordinary boot on the cable or the reset pin that ends it: the 500 ms double-reset wait, and on the reset pin one more read of that undefined RAM word.

`0x57` for recovery outranks both (`hardware/platform/zephyr/dfu.h`).
