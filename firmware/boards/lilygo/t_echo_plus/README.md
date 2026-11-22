# LilyGO T-Echo Plus

The board skyBlip Go runs on, a T-Echo Plus or a plain T-Echo: the board probes which parts are fitted. The factory bootloader is read in [`factory/README.md`](factory/README.md). `board.h` is the board assembled once, a template over the platform: `hardware/platform/zephyr/t_echo_plus.h` on silicon, `hardware/platform/host/platform.h` on the host.

## The gated rails come up at board level

`board.c` raises the gated peripheral rails in a `SYS_INIT` at `PRE_KERNEL_1`, and Zephyr links it into every image built for the board, MCUboot included. MCUboot needs it: the secondary slot lives on the external flash, which sits behind the same switched rail as the e-paper, the GNSS and the sensors on T-Echo REV_2 and later (SoftRF `iomap/LilyGO_TEcho.h:80`). Without the rail, MCUboot reads an unpowered flash and every update fails to verify.

- It holds the flash's WP# and HOLD# high. The part runs single-line, where those are real inputs, and a floating one can write-protect the chip or stall a transfer.
- It raises the L76K's reset, then its wake, after the rail, as SoftRF does (`platform/nRF52.cpp:1589-1593`). The receiver pulls both up itself (Quectel L76K Hardware Design 4.2.1-4.2.2). These two pins, P1.05 and P1.02, agree across SoftRF's iomap and MeshCore's variant, and are the first suspects if a unit comes up with no receiver.
- `KERNEL_INIT_PRIORITY_DEVICE` runs after the nRF GPIO driver and well before the SPI NOR driver at `POST_KERNEL`.

## What the board reads before any driver owns the pins

LilyGO fits more than one part behind the same footprint, so two facts are read rather than declared (`hardware/README.md`, The two questions a pin and a register answer). Both need pins a Zephyr driver owns from `POST_KERNEL` on, so `board.c` reads them at `PRE_KERNEL_1`, after the rails, and the platform asks for them later as board hooks. MCUboot skips both.

- **The panel ID.** This board's e-paper MISO is a placeholder, so the ID is clocked out over MOSI with the line reversed, half duplex, mode 0. The sequence is SoftRF's (`platform/nRF52.cpp:3785-3792`): reset low for 20 ms, then 200 ms before the panel answers, which doubles as its power-on settle. No critical section is needed at `PRE_KERNEL_1`, since no scheduler runs.
- **The buzzer pin.** It is read high-Z, then against the internal pull-up of about 13 kOhm (nRF52840 PS v1.8). A piezo stage is tens of nanofarads, so the 1 ms settle SoftRF also waits (`platform/nRF52.cpp:1271`) is several time constants, and a pin still low is held low by something.

## The haptic is a part on a bus

P0.08 is the DRV2605's enable, not a motor drive ([`hardware/parts/drv2605/README.md`](../../../hardware/parts/drv2605/README.md)). `Capability::Haptic` comes from the DRV2605 answering at 0x5A and naming itself, never from the board being a Plus, because a pin raised with no driver behind it claims a motor that cannot move. The probe is SoftRF's (`src/platform/nRF52.cpp:1158-1163`).

## The bus scan

`i2c_scan.h` probes every address on the sensor bus once, when the self-test page first asks: 112 probes at 100 kHz, about 20 ms. A zero-length write is the probe, as Zephyr's i2c shell and SoftRF do (`platform/nRF52.cpp:1128-1163`). The scan names the parts this board is known to carry, including the ones fitted and deliberately undriven, such as the PCF8563 RTC, so the page names what answered and which of the two BME280 addresses did.

## The devicetree

**Pin maps.** `board_power_gpios`, `board_probe_gpios` and `board_part_gpios` are pin maps, never devices, so `board.c`, the shutdown path and `pins.h` read the same numbers. They borrow `gpio-keys`, the one upstream binding with a plain `gpios` property, and stay `status = "disabled"`: enabled, `CONFIG_INPUT_GPIO_KEYS` would one day reconfigure a rail enable as an interrupt input and switch the board's peripherals off at boot. Disabled, the `GPIO_DT_SPEC` macros still resolve. The `leds` node is `gpio-leds`, an exact binding, and stays enabled: with no `CONFIG_LED_GPIO` nothing binds, and if a product enables it, each active-low LED starts off.

**Pins left out.** A node claims something drives the pin, so two are absent. P0.18, the button LilyGO labels reset, is the SoC's nRESET: the factory bootloader is built with `CONFIG_GPIO_AS_PINRESET` (Adafruit_nRF52_Bootloader `Makefile:354`) and programs `UICR->PSELRESET` on every boot (`system_nrf52.c:341-355`), so do not copy Meshtastic's use of it as a GPIO. P0.21 goes to the SX1262's DIO3, which the part drives itself to power its TCXO, so the SoC end stays an input, unlike meshcore's `variant.cpp:30-31`.

**LEDs.** The REV_2 and Plus map, active-low: green P1.01, red P1.03, blue P0.14, from SoftRF (`platform/iomap/LilyGO_TEcho.h:22-24`, applied for REV_2 and the Plus at `platform/nRF52.cpp:1595-1601`), nrf52-ogn-tracker (`variants/t_echo/variant.h:20-24`) and LilyGO's README pin table. meshcore's 13, 14 and 15 is SoftRF's REV_1 map, and P0.13 is this board's regulator enable, so it cannot hold on a Plus. The LEDs sit on the gated rail and are released on the way down (`hardware/README.md`, Parking the LEDs).

**Battery divider.** 100K over 100K on P0.04 (AIN2), so the ADC sees half the cell (SoftRF `platform/nRF52.h`, Meshtastic's t-echo variant). The ratio lives in the devicetree, and the divider driver scales the reading back with it. A 0.6 V reference at gain 1/6 gives 3.6 V full scale against the 2.1 V of a full cell.

**DC-DC.** `&reg1` runs the SoC's DC-DC converter instead of the LDO it resets into. That needs the inductor on DCC/DEC4: meshcore's firmware for this board enables it (`variants/lilygo_techo/TechoBoard.h:16`, `src/helpers/NRF52Board.cpp:227-238`), and a bench current measurement confirms it. The repository holds no T-Echo schematic. Zephyr 4.5 reads the property at SoC init (`soc/nordic/nrf52/soc.c:35-36`) and removed `CONFIG_SOC_DCDC_NRF52X`.

**Sensor bus.** Nothing on it is declared. The BME280's address is disputed, 0x76 in our BOM and 0x77 in LilyGO's README and SoftRF, and Zephyr's driver sleeps through each conversion, so our part probes both ([`hardware/parts/bme280/README.md`](../../../hardware/parts/bme280/README.md)). Zephyr has no driver for the BHI260AP hub or a haptic subsystem for the DRV2605, so our parts drive both and the bus scan says what is fitted. The PCF8563 RTC at 0x51 is deferred: P0.12 gates this bus and the board has no coin cell, so it cannot keep time across a power cycle, SoftRF never reads it back, and each log record already flags UTC validity (`core/flight/log_record.h`). Declare it with `nxp,pcf8563` and `CONFIG_RTC` once a schematic shows it on the ungated 3V3 and the flight log has a reader.

**Internal flash.** The factory bootloader owns both ends: MBR and SoftDevice below 0x26000, itself from 0xF4000. Its drag-and-drop window is 0x1000..0xEA000 (`src/usb/uf2/uf2cfg.h:25-26`, `DFU_APP_DATA_RESERVED = 10*4096` at `Makefile:139`), so MCUboot and slot0 lie below 0xEA000. Above it, no `.uf2` can write the unit's unlock code at 0xEA000 (`identity_partition`, `core/identity/README.md`) or NVS from 0xEC000.

**External flash.** LilyGO fits an MX25R1635F or a ZD25WQ16B depending on availability, with no way to tell from the board. So it is `jedec,spi-nor` with `CONFIG_SPI_NOR_SFDP_RUNTIME`, which reads the geometry from the chip, and not `nordic,qspi-nor`, which returns `-ENODEV` on a JEDEC mismatch and would leave slot1, and every update, dead. Quad mode is out, because the two parts differ in quad-enable bit and quad page-program opcode, both compile-time properties. Plain 03h READ is specified to 33 MHz on both, and `use-fast-read` stays off because 0Bh is capped at 8 MHz on the MX25R in its low-power mode. It runs on SPIM1 at 8 MHz, because the SX1262 has SPIM3, the only 32 MHz instance. An MCUboot swap is dominated by about 15 s of internal page erases, so the slower bus costs little. IO2 and IO3 are WP# and HOLD#, which `board.c` holds high before MCUboot's flash driver runs.

**Log partition.** `log_partition` on the NOR is 0x12A000 bytes, 298 sectors of 4 KB: `kLogSectors` in `log_flash.h`, which a `static_assert` holds to this devicetree. The product sizes its sector table to it (`../../../core/store/README.md`).

**Slot sizes.** slot1 is exactly slot0's size, 182 sectors of 4 KB. Zephyr derives imgtool's `--slot-size` from slot1 (`zephyr/cmake/mcuboot.cmake:96-102`), and the padded image goes straight into slot0 through the `.uf2`. A bigger slot1 puts the trailer past `USER_FLASH_END`, where the bootloader drops it, which `scripts/mkuf2.py` refuses. MCUboot's `docs/design.md:296-299` allows equal slots for swap-using-offset. Both slots have 4 KB sectors, as the mode requires.

