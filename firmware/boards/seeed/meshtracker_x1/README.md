# Seeed SenseCAP MeshTracker X1

The board skyBlip Bravo runs on. The factory bootloader's files, read off a unit (`factory/README.md`), confirm the SoftDevice and flash boundaries at 0x27000 and 0xEA000 and name the bootloader source. Every other pin, address, boundary and timing below comes from the sources listed here and stays unverified until #169 benches a unit. A tagged `TODO` in the board files and the X1's platform points back here.

## Sources

| Name below | Source | Revision |
|---|---|---|
| SoftRF | `lyusupov/SoftRF`, `software/firmware/source/SoftRF/src/platform/iomap/Seeed_X1.h`, `platform/nRF52.cpp`, `platform/nRF52.h` | `d091924`, 3 November 2026 |
| Meshtastic | `meshtastic/firmware`, `variants/nrf52840/seeed_mesh_tracker_X1/variant.h` and `variant.cpp`, `boards/mesh-tracker-x1.json` | `17cc649a`, 29 September 2026 |
| Meshtastic bootloader | `meshtastic/nRF52_Bootloader`, `src/boards/mesh_tracker_x1_v2/board.h` and `board.mk`, `src/boards/boards.c`, `src/usb/uf2/uf2cfg.h`, `Makefile`, `linker/nrf52840.ld`, `src/main.c`, the factory bootloader of the owner's unit | `b93789fe`, 12 August 2026 |
| GigaDevice | the GD25Q64E datasheet, DS-00484 | Rev 1.4 |
| Seeed | the MeshTracker X1 wiki page, `Seeed-Studio/wiki-documents` `sites/en/docs/Network/Mesh_Network/MeshTracker_X1/x1_intro.md`: its pin list, its block diagram `HardwareDiagram729.png` and its render `HardwareDiagramBu.png` | `bef7ae40`, 24 September 2026 |

SoftRF and Meshtastic are GPL-3.0 and were read for facts only. No code is copied from either.

## Pins

| Function | Pin | Sources | Notes |
|---|---|---|---|
| Button | P0.06, high when pressed, pull-down | SoftRF, Meshtastic, Meshtastic bootloader, Seeed | Seeed's block diagram also feeds the button and the USB-C detect into a NAND gate on nRST, which #169 measures. |
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
| RTC I2C | SDA P0.26, SCL P0.27, enable P0.09, interrupt P0.31 | SoftRF, Seeed | P0.09 is an NFC pin, so the devicetree sets `nfct-pins-as-gpios`, as Meshtastic does. The factory bootloader is built with `CONFIG_NFCT_PINS_AS_GPIOS` (`Makefile:356-358` at `b93789fe`), so its first boot already set `UICR.NFCPINS`, and ours writes nothing there. |
| IMU and magnetometer interrupts | P1.02, P1.12 | SoftRF, Seeed | |
| Reset | P0.18 | SoftRF, Meshtastic bootloader | The bootloader keeps it as nRESET. Nothing drives it. |

On the case, the LED sits just above the button.

SoftRF lists a console UART on P0.16 and P0.17, and also says the X1 does not expose it (`nRF52.cpp:3885-3893`), so the devicetree leaves it out. The board declares no console. The smoke image declares a USB CDC console in its own overlay.

## Parts

| Part | Bus | Address | Sources |
|---|---|---|---|
| SPA06-003 barometer | sensor I2C | 0x77 | SoftRF `nRF52.h:151`, Seeed |
| DRV2605L haptic driver, LRA | sensor I2C | 0x5A | SoftRF `nRF52.h:140`, Seeed |
| LSM6DSO IMU, optional | sensor I2C | 0x6A | SoftRF `nRF52.h:153` |
| BMM350 magnetometer, optional | sensor I2C | 0x14 | SoftRF `nRF52.h:150` |
| YSN8900 RTC | RTC I2C | 0x32 | SoftRF `nRF52.h:154`, Seeed |
| GD25Q64E NOR, 8 MiB | QSPI | JEDEC `c8 40 17` | Seeed names a GD25Q64ENIGR. GigaDevice's GD25Q64E datasheet, DS-00484 Rev 1.4, gives that JEDEC ID, as does the GD25Q64C entry of Adafruit_SPIFlash `flash_devices.h` that SoftRF probes it with. |
| Charger | | | SoftRF names an SGM40571. Seeed's block diagram names a BQ24045. #169 reads the part. |

The NOR runs single-line at 8 MHz, the Go's NOR clock, on high-drive QSPI pads, because two Bluetooth updates read slot1 corrupt at 32 MHz on standard-drive pads (#278). nrfx's own pin setup applies high drive (`nrfx_qspi.c:63-72`) and Zephyr's pinctrl leaves it to the board. 8 MHz single-line moves about 1 MB/s, far above the Bluetooth upload's 8 KB/s. With `quad-enable-requirements = "NONE"` the driver never writes a status register. MCUboot takes these settings from this devicetree, so only a `.uf2` changes them on a unit. The devicetree sets `has-dpd` with the datasheet's tDP of 3 us and tRES1 of 20 us, so the driver wakes a part a previous firmware left in deep power-down before it reads the JEDEC ID. `board.c` raises the NOR's enable and the GNSS RTC supply, and outside MCUboot the sensor rail, then waits the 30 ms SoftRF waits (`nRF52.cpp:934-942`).

A NOR that fails its JEDEC check leaves MCUboot without slot1: `flash_area_open` returns `-ENODEV` and MCUboot stops at `FIH_PANIC`, with no console to say so. A unit that shows nothing on USB after the smoke image is installed failed there. Recover it with the bootloader's 3 s hold and a fresh drag-and-drop, or with #169's SWD restore.

The nRF52840 package is QIAA, read off the chip in Seeed's block diagram. The 32 kHz crystal is Meshtastic's `USE_LFXO`. REG1 stays on the LDO the SoC resets into, because no source says the DC-DC inductor is fitted.

## Boot chain

The factory UF2 bootloader stays. It chain-loads MCUboot from the application origin, and MCUboot verifies and boots slot0. Recovery is the factory bootloader's drag-and-drop.

| Partition | Start | Size | Why |
|---|---|---|---|
| `softdevice_partition` | 0x00000 | 0x27000 | MBR, 0x1000, and S140 7.3.0, 0x26000 (`SD_FLASH_SIZE` in S140 7.3.0's `nrf_sdm.h`). The unit's `INFO_UF2.TXT` names S140 7.3.0, and its `CURRENT.UF2` puts the SoftDevice's end at 0x27000 (`factory/README.md`). |
| `boot_partition` | 0x27000 | 0xE000 | MCUboot, the Go's size. |
| `slot0_partition` | 0x35000 | 0xB5000 | Ends at `USER_FLASH_END`. |
| `storage_partition` | 0xEA000 | 0xA000 | The bootloader's `DFU_APP_DATA_RESERVED`, so no `.uf2` reaches it. |
| `uf2_boot_partition` | 0xF4000 | 0xC000 | The factory bootloader, its MBR parameters and its settings. |
| `slot1_partition`, on the NOR | 0x0 | 0xB5000 | The secondary slot, the same size as slot0, as on the Go. |
| `log_partition`, on the NOR | 0xB5000 | 0x200000 | The flights and the capture, 512 sectors of 4 KB, `kLogSectors` in `log_flash.h`. The NOR's last 5.3 MiB are unused. |

`USER_FLASH_END` is `BOOTLOADER_REGION_START - DFU_APP_DATA_RESERVED` = 0xF4000 - 10 * 4096 = 0xEA000 in the factory bootloader (`uf2cfg.h:25-26`, `Makefile:139`, `linker/nrf52840.ld:14`), and the unit's `CURRENT.UF2` covers 0x1000..0xEA000 and nothing above (`factory/README.md`). SoftRF finds the string `MeshTracker-X1` in the bootloader region at 0xF4000 (`nRF52.cpp:771-879`), which agrees on where the bootloader sits. #169's SWD session reads that address from `UICR`. Another unit may ship another bootloader: Meshtastic-Android's `maintenance_uf2.json` lists the Board-IDs `nRF52840-MeshTracker-X1-v1`, which our unit reports, and `nRF52840-MeshTracker-X1`. `scripts/mkuf2.py` holds every `.uf2` to this window, and `scripts/test_mkuf2.py` reads the window back from this devicetree.

MCUboot uses swap-using-offset, as on the Go. A Go image starts at 0x26000, inside S140 7.3.0, and `mkuf2.py` refuses to build one for this board.

The factory bootloader enters UF2 mode once the button has been held about 3 s through boot, and lights the LED white. It holds the start while the button stays down, up to those 3 s, so the application reads a shorter wake press as already up. With GPREGRET at `0x6d` it skips the button, so the X1 writes `0x00` at switch-off. Unlike the T-Echo's bootloader (`../../lilygo/t_echo_plus/factory/README.md`), it does not send a VBUS wake back to sleep. `factory/README.md` cites the source for each.

## The board composition

`board.h` is the X1 assembled once, a template over the platform as the T-Echo's is: `hardware/platform/zephyr/meshtracker_x1.h` on silicon, `hardware/platform/host/meshtracker_x1.h` on the host. `pins.h` holds the four lines the parts' drivers drive as `io::Gpio` numbers, and on silicon a `static_assert` holds each to the devicetree.

The constructor touches no part, because the product is a global whose constructor runs before `main()` arms the watchdog and before the product decides whether a wake runs. `probe()` does the work later, and the product then copies the capabilities into its roles. The roles' references are fixed at construction: the annunciator is always the platform's, and a DRV2605 the probe finds attaches to it.

| Answers in `probe()` | Grants |
|---|---|
| the LR2021, its version | `Capability::AdsbReceiver` |
| the SPA06, its chip id | `Capability::Barometer` |
| the DRV2605, identifying itself | `Capability::Haptic` and `Capability::HapticDriver` |

The LR2021's receiver pushes each frame into the `bus::AdsbQueue` the product hands the board. The board polls the AG3335, the battery, the button and the PPS edge as the T-Echo's does, and triggers the SPA06 once a second on the PPS edge, because the part has no filter ([`hardware/parts/spa06/README.md`](../../../hardware/parts/spa06/README.md)). It scans no bus and keeps no inventory. Nothing on Bravo reads the YSN8900, so nothing probes it.

## The way down

Bravo's product parks the LR2021, the buzzer, the motor, the lamps and the DRV2605 first (`../../../products/skyblip_bravo/README.md`). `meshtracker_x1_power.h` then walks `core/power`'s power-down order on the X1's lines:

| Step | On the X1 |
|---|---|
| radio sleep | nothing: the product's receiver already put the LR2021 to sleep or held it in reset, and a refused boot held it in reset through NRESET |
| flash deep power-down | nothing: the NOR loses its enable below |
| flash lines released | the QSPI peripheral disabled and its six pads let go |
| panel lines released | nothing: the X1 has no panel |
| sensor bus released | both TWIM peripherals disabled and their four pads let go |
| GNSS standby | the GNSS enable, P1.11, driven low |
| GNSS lines released | the UARTE stopped and its pads let go, and the reset, sleep and RTC-wake lines let go |
| peripheral rail off | the sensor rail P1.07, the RTC enable P0.09 and the NOR enable P0.15 driven low |
| wake pin armed | the button, P0.06, armed as a level wake, high when pressed |

GPREGRET is written first: `0x00`, which leaves the factory bootloader's button hold in reach, or its UF2 magic for a recovery (`factory/README.md`). The GNSS RTC supply, P1.13, stays high, so the AG3335 keeps its orbits, time and last position for a hot start. Every step is unverified until #169 measures the off current.

System OFF and the wake from it keep every pin's level, while a soft, watchdog, pin or brownout reset releases them all (nRF52840 PS v1.1 §5.3.6.8). So after a reset P1.13 floats, and the AG3335 may lose its backup domain and start cold. `board.c` raises it in the first init of MCUboot and of the application, which leaves it floating only while the factory bootloader runs. MCUboot changes only with a `.uf2`, so on a unit updated over Bluetooth it floats through MCUboot too. #169 times a fix after a button switch-off and after a reboot to check it.

## The smoke image

`firmware/test/twister/meshtracker_x1` is MCUboot and a minimal signed application, built by sysbuild. CI builds it, holds it to slot0 with `size_check.py`, wraps it with `mkuf2.py meshtracker_x1` and uploads `meshtracker-x1-smoke.uf2`. It is signed with MCUboot's published development key, so it is a bench image only.

At boot it lights red, green and blue, sounds the buzzer and pulses the haptic once. Each time a host opens its USB CDC port, it prints its capabilities, whether the DRV2605 answered and every address on both I2C buses, and runs the outputs again. While the port is open it prints the battery, VBUS, both charger lines and the button every second. A 2 s hold switches it off: it parks the outputs, drops the sensor rail, writes `0x00` to GPREGRET and arms the button as the wake, and stays on if any of those fails. The radio and the NOR stay powered and the GNSS enable is left undriven, so its off current is not the product's.
