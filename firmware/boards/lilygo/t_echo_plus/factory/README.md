# What the factory bootloader reports

`INFO_UF2.TXT` is copied verbatim off the TECHOBOOT volume of a new T-Echo Plus, before anything was installed on it. It is the evidence for the partition map in `../t_echo_plus.dts`, so it is kept rather than quoted.

`t_echo/INFO_UF2.TXT`, off a plain T-Echo, is byte for byte identical, `Model: LilyGo T-Echo` included. So the partition map holds on either board, and the bootloader cannot tell a Plus from a plain T-Echo: the bus scan reads what is fitted (`../board.h`).

What it pins down:

| The file says | What depends on it |
|---|---|
| `SoftDevice: S140 version 6.1.1` | `softdevice_partition`, 0x0..0x26000, and `mkuf2.py`'s refusal to write below it |
| `UF2 Bootloader 0.6.1-2-g1224915` | the write window, below |
| `Board-ID: nRF52840-TEcho-v1` | the SoC variant, which is what picks the window's size |

At tag 0.6.1 the write window is `USER_FLASH_START..USER_FLASH_END` = `MBR_SIZE .. BOOTLOADER_REGION_START - DFU_APP_DATA_RESERVED` (`src/usb/uf2/uf2cfg.h:20-21`), and the nRF52840 branch of `Makefile:117` sets `DFU_APP_DATA_RESERVED = 10*4096`. With the region at 0xF4000 that is 0x1000..0xEA000, which `scripts/mkuf2.py` guards and `scripts/test_mkuf2.py` asserts. Neither constant moved in the two commits past the tag the device runs.

A unit that reports a different bootloader or SoftDevice has not been checked against any of this. Read its `INFO_UF2.TXT` first: a smaller `USER_FLASH_END` silently truncates an install.

## What it does with a wake

`0.6.1-2-g1224915` is Linar Yusupov's fork of the Adafruit bootloader (`lyusupov/Adafruit_nRF52_Bootloader`). Its commit `1224915` adds four lines to `src/main.c:181-184`, which Adafruit's master does not carry. They run before the board is initialised, on every reset:

```c
if ((NRF_POWER->RESETREAS & POWER_RESETREAS_VBUS_Msk) && dfu_skip) {
  NRF_POWER->RESETREAS |= POWER_RESETREAS_VBUS_Msk;
  NRF_POWER->SYSTEMOFF = 1;
}
```

`dfu_skip` is GPREGRET holding `0x6d`, `DFU_MAGIC_SKIP`. While it is set, a cable plugged in wakes the SoC and the bootloader sends it straight back to SYSTEM OFF, before MCUboot and the application. That is what we want: a charger plugged into a device in a flight bag must not wake it. SoftRF, whose author wrote the fix, writes the same byte before every sleep (`src/platform/nRF52.cpp:3213-3226`). GPREGRET is cleared only after that test, so the byte survives every later cable. A GPIO wake does not set the VBUS bit, and the pin configuration survives the wake reset, so an armed button still wakes the unit.

The application must write the byte. `BUTTON_DFU` is P1.10, the device's only button (`src/boards/lilygo_techo/board.h`). Without the skip the bootloader reads it on every boot, finds the waking press still held, and starts USB DFU with no timeout. `BUTTON_FRESET` is P0.11, the pad, and both together are BLE OTA. RESET is the upper button on the left side of the case and this button the lower one. The skip also bypasses the double-reset window and its RAM word at `0x20007F7C`, undefined through SYSTEM OFF, so a double-click of RESET does not arm on the first boot after a power-off.

So every switch-off writes `0x6d` (`kFactoryDfuButton` in `../pins.h`, `boot_magic_for_system_off` in `core/power/shutdown.h`) and arms the button, including a switch-off by the cutoff. Cable mode is the exception: it writes `0x00` so the bootloader lets the cable wake through, and leaves the button unarmed so no waking press is held into DFU (`core/power/README.md`, Cable mode). The price is the cable: a charger never reaches the application, so a unit switched off on `BATTERY EMPTY` or `CHARGE THE BATTERY` stays off through the whole charge. The button is the way back, which the lockout refuses on a flat cell and lets through on the cable (`core/power/README.md`).

`0x57` outranks it (`core/power/shutdown.h`): a unit switched off with recovery armed comes up as USB mass storage instead of chain-loading MCUboot. That is the software route into the bootloader, for a unit whose reset button is hard to reach. The bootloader's own route works whatever state the firmware is in: a double-click on RESET within 0.5 s. Both bytes sit at offset 0 of the retention area on `&gpregret1` in the devicetree, which the bootloader reads on every boot (`src/main.c:106-110`, `check_dfu_mode` at `:241-270`). Nothing in the tree writes its unsigned OTA magics, 0xA8 and 0xB1.
