# Seeed SenseCAP MeshTracker X1

The board skyBlip Bravo runs on. No unit has been read yet: every pin, address, boundary and timing below comes from the sources listed here, and each one stays unverified until #169 benches a unit. A tagged `TODO` in `meshtracker_x1.dts`, `board.c`, `Kconfig.meshtracker_x1`, `meshtracker_x1_defconfig` and `scripts/mkuf2.py` points back here.

## Sources

| Name below | Source | Revision |
|---|---|---|
| SoftRF | `lyusupov/SoftRF`, `software/firmware/source/SoftRF/src/platform/iomap/Seeed_X1.h`, `platform/nRF52.cpp`, `platform/nRF52.h` | `d091924`, 3 November 2026 |
| Meshtastic | `meshtastic/firmware`, `variants/nrf52840/seeed_mesh_tracker_X1/variant.h` and `variant.cpp`, `boards/mesh-tracker-x1.json` | `17cc649a`, 29 September 2026 |
| Meshtastic bootloader | `meshtastic/nRF52_Bootloader`, `src/boards/mesh_tracker_x1/board.h` and `board.mk`, `src/usb/uf2/uf2cfg.h`, `Makefile`, `linker/nrf52840.ld`, `src/main.c` | tag `2.6.0`, `e75ad3fa` |
| GigaDevice | the GD25Q64E datasheet, DS-00484 | Rev 1.4 |
| Seeed | the MeshTracker X1 wiki page, `Seeed-Studio/wiki-documents` `sites/en/docs/Network/Mesh_Network/MeshTracker_X1/x1_intro.md`: its pin list, its block diagram `HardwareDiagram729.png` and its render `HardwareDiagramBu.png` | `bef7ae40`, 24 September 2026 |

SoftRF and Meshtastic are GPL-3.0 and were read for facts only. No code is copied from either.

## Pins

| Function | Pin | Sources | Notes |
|---|---|---|---|
| Button | P0.06, high when pressed, pull-down | SoftRF, Meshtastic, Meshtastic bootloader, Seeed | Seeed's block diagram also feeds the button and the USB-C detect into a NAND gate on nRST. What that gate does on a press or a cable is #169's to measure. |
| LED red, green, blue | P0.03, P0.24, P0.28, active high | SoftRF (`nRF52.h:45-55` for the polarity), Meshtastic (`LED_STATE_ON 1`), Meshtastic bootloader, Seeed | |
| Buzzer | P0.25, PWM | SoftRF, Meshtastic, Seeed | SoftRF and Meshtastic park it as an input with a pull-down when idle. |
| Sensor rail | P1.07, active high | SoftRF, Meshtastic (`PIN_3V3_EN`), Seeed | Powers the barometer, the IMU, the magnetometer, the buzzer and the haptic driver, per SoftRF's note on the pin. |
| Haptic enable | P1.05 | SoftRF, Meshtastic (`PIN_DRV_EN`), Seeed | The DRV2605's enable, at 0x5A on the sensor bus. |
| Battery divider | P0.02 (AIN0), 100K over 100K, enable P1.06 active high | SoftRF, Meshtastic (`BATTERY_PIN`, `ADC_MULTIPLIER 2.0`, `PIN_BAT_ADC_EN`), Seeed | Meshtastic reads it against a 3.0 V reference. The devicetree keeps the T-Echo's 3.6 V, which half of a full high-voltage cell still fits under. |
| Charge status | P1.03, low when charging | Meshtastic (`EXT_CHRG_DETECT_VALUE LOW`), SoftRF's T1000-E map (`CHG_STATUS active LOW`) | |
| Charge done | P1.04, low when charged | Meshtastic (commented out), SoftRF's T1000-E map | |
| USB-C detect | P0.05 | SoftRF (`CHG_VUSB`), Meshtastic (`EXT_PWR_DETECT`), Seeed | No source gives its polarity. The platform reads VBUS from the SoC instead, as Meshtastic does (`NRF_APM`). |
| QSPI NOR | SCK P0.19, CS P0.20, IO0 P0.21, IO1 P0.22, IO2 P0.23, IO3 P1.00, enable P0.15 active high | SoftRF, Seeed | |
| Radio SPI | SCK P0.11, MOSI P1.09, MISO P1.08, CS P0.12 | SoftRF, Meshtastic, Seeed | The LR2021, for #172 and #176. |
| Radio control | reset P1.10, busy P0.07, IRQ P1.01 (DIO8) | SoftRF, Meshtastic, Seeed | |
| GNSS UART | nRF TX P0.13, nRF RX P0.14, 115200 baud | SoftRF, Meshtastic, Seeed | The AG3335, for #171 and #176. |
| GNSS control | enable P1.11, reset P0.08 high to reset, RTC supply P1.13, sleep P0.30, RTC wake P0.29, PPS P0.04 | SoftRF, Meshtastic, Seeed | Meshtastic's comment calls P0.04 `VCC_ADC`. SoftRF and Seeed call it the PPS. |
| Sensor I2C | SDA P1.15, SCL P1.14 | SoftRF, Meshtastic, Seeed | |
| RTC I2C | SDA P0.26, SCL P0.27, enable P0.09, interrupt P0.31 | SoftRF, Seeed | P0.09 is an NFC pin, so the devicetree sets `nfct-pins-as-gpios`, as Meshtastic's build does (`-DCONFIG_NFCT_PINS_AS_GPIOS=1`). On a unit whose `UICR.NFCPINS` still selects NFC, the first boot writes it and resets once. |
| IMU and magnetometer interrupts | P1.02, P1.12 | SoftRF, Seeed | |
| Reset | P0.18 | SoftRF, Meshtastic bootloader | The bootloader keeps it as nRESET. Nothing drives it. |

SoftRF lists a console UART on P0.16 and P0.17, and Meshtastic names it `SERIAL2`. SoftRF also says the X1 does not expose it (`nRF52.cpp:3885-3893`), so the devicetree leaves it out. The console is USB CDC.

## Parts

| Part | Bus | Address | Sources |
|---|---|---|---|
| SPA06-003 barometer | sensor I2C | 0x77 | SoftRF `nRF52.h:151`, Seeed |
| DRV2605L haptic driver | sensor I2C | 0x5A | SoftRF `nRF52.h:140`, Seeed |
| LSM6DSO IMU, optional | sensor I2C | 0x6A | SoftRF `nRF52.h:153` |
| BMM350 magnetometer, optional | sensor I2C | 0x14 | SoftRF `nRF52.h:150` |
| YSN8900 RTC | RTC I2C | 0x32 | SoftRF `nRF52.h:154`, Seeed |
| GD25Q64E NOR, 8 MiB | QSPI | JEDEC `c8 40 17` | Seeed names a GD25Q64ENIGR. GigaDevice's GD25Q64E datasheet, DS-00484 Rev 1.4, gives that JEDEC ID, as does the GD25Q64C entry of Adafruit_SPIFlash `flash_devices.h` that SoftRF probes it with. |
| Charger | | | SoftRF names an SGM40571. Seeed's block diagram names a BQ24045. #169 reads the part. |

The NOR runs single-line, with `quad-enable-requirements = "NONE"`, so the driver never writes a status register. The datasheet puts QE at S9, written with 31h, which no driver setting here needs. The devicetree sets `has-dpd` with the datasheet's tDP of 3 us and tRES1 of 20 us, so the driver releases a part a previous firmware left in deep power-down before it reads the JEDEC ID. `board.c` raises the NOR's enable, and the sensor rail outside MCUboot, then waits the 30 ms SoftRF waits (`nRF52.cpp:934-942`).

A NOR that fails its JEDEC check leaves MCUboot without slot1: `flash_area_open` returns `-ENODEV`, MCUboot stops at `FIH_PANIC`, and it has no console to say so. A unit that shows nothing on USB after the smoke image is installed failed there. Recover it with the bootloader's 3 s hold and a fresh drag-and-drop, or with #169's SWD restore.

The nRF52840 package is QIAA, read off the chip in Seeed's block diagram. The 32 kHz crystal is Meshtastic's `USE_LFXO`. REG1 stays on the LDO the SoC resets into, because no source says the DC-DC inductor is fitted.

## Boot chain

The factory UF2 bootloader stays. It chain-loads MCUboot from the application origin, and MCUboot verifies and boots slot0. Recovery is the factory bootloader's drag-and-drop.

| Partition | Start | Size | Why |
|---|---|---|---|
| `softdevice_partition` | 0x00000 | 0x27000 | MBR, 0x1000, and S140 7.3.0, 0x26000 (`SD_FLASH_SIZE` in S140 7.3.0's `nrf_sdm.h`, as `adafruit/Adafruit_nRF52_Bootloader` vendors it). Meshtastic's board file and linker script put the application at 0x27000. |
| `boot_partition` | 0x27000 | 0xE000 | MCUboot, the Go's size. |
| `slot0_partition` | 0x35000 | 0xB5000 | Ends at `USER_FLASH_END`. |
| `storage_partition` | 0xEA000 | 0xA000 | The bootloader's `DFU_APP_DATA_RESERVED`, so no `.uf2` reaches it. |
| `uf2_boot_partition` | 0xF4000 | 0xC000 | The factory bootloader, its MBR parameters and its settings. |
| `slot1_partition`, on the NOR | 0x0 | 0xB5000 | The secondary slot, the same size as slot0, as on the Go. The rest of the NOR is #176's. |

`USER_FLASH_END` is `BOOTLOADER_REGION_START - DFU_APP_DATA_RESERVED` = 0xF4000 - 10 * 4096 = 0xEA000 in Meshtastic's bootloader 2.6.0 (`uf2cfg.h:31-32`, `Makefile:116-121`, `linker/nrf52840.ld`). SoftRF tells an X1 by finding the string `MeshTracker-X1` in the bootloader region at 0xF4000 (`nRF52.cpp:771-879`), which agrees on where the bootloader sits. Which bootloader a new unit ships with is not known. Meshtastic-Android's `maintenance_uf2.json` lists two Board-IDs, `nRF52840-MeshTracker-X1-v1` and `nRF52840-MeshTracker-X1`, and a third-party tool, `mikecarper/meshfirmware` `web/src/bootloader-info.js`, calls the `-v1` one the factory's. `scripts/mkuf2.py` holds every `.uf2` to this window, and `scripts/test_mkuf2.py` reads the window back from this devicetree.

MCUboot uses swap-using-offset, as on the Go. A Go image starts at 0x26000, inside S140 7.3.0, and `mkuf2.py` refuses to build one for this board.

Meshtastic's bootloader 2.6.0 enters UF2 mode after the button is held for 3 s through boot (`BUTTON_DFU_HOLD_MS 3000`), and leaves it when the drive is ejected (`UF2_EXIT_ON_EJECT`). With GPREGRET at `0x6d` it skips that check unless the button is down (`main.c:300-320`). It has no line that puts a VBUS wake back to sleep, unlike the T-Echo's bootloader (`../../lilygo/t_echo_plus/factory/README.md`).

## The smoke image

`firmware/test/twister/meshtracker_x1` is MCUboot and a minimal signed application, built by sysbuild. CI builds it, holds it to slot0 with `size_check.py`, wraps it with `mkuf2.py meshtracker_x1` and uploads `meshtracker-x1-smoke.uf2`. It is signed with MCUboot's published development key, so it is a bench image only.

At boot it lights red, green and blue, sounds the buzzer and pulses the haptic once. Each time a host opens its USB CDC port, which raises DTR, it prints its capabilities, whether the DRV2605 answered and every address that answers on both I2C buses, and runs the outputs again. While the port is open it prints the battery, VBUS, both charger lines and the button every second. A 2 s hold switches it off: it parks the outputs, drops the sensor rail, writes `0x6d` to GPREGRET and arms the button as the wake, and it stays on if any of those fails. The radio and the NOR stay powered and the GNSS enable is left undriven, so the off current it leaves is not the product's.
