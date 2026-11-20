# hardware

What fills the roles `ports/` declares. `parts/` is a chip and its datasheet. `platform/` is everything that comes from the silicon the board is soldered to. `io/` is the bus vocabulary the two meet over.

Write a part against `io::Spi`, `io::I2c` and `io::Uart`, never against Zephyr or a platform. Then each part's `model.h` stands in for the chip on the host, and `make test` exercises the driver's real register writes. A model is headers only, so it never enters an image. The L76K's is three: `model.h` is what the receiver hears and how it moves, `model_commands.h` what it does with each `$PCAS` sentence, and `model_sentences.h` what it says. The GNSS models share `parts/gnss_truth.h`, where the antenna is, how it moves and what time it is, so a scenario flies the L76K and the AG3335 the same way.

Wait out a window the datasheet asks the host to hold, such as a reset pulse or a save, on `io::Delay`, stated in microseconds. On silicon the calling thread sleeps for at least the window (`k_usleep`, resumed if woken early), so the service loop and the radio thread give the CPU back during a 10 ms panel reset. It spins with `k_busy_wait` only for a hold shorter than one kernel tick or where no thread may sleep: an interrupt, interrupts locked, or before the kernel runs. On the host it advances every part model's clock, and the model refuses a hold that came up short: the SX1262 model raises `Fault::ShortReset` or `Fault::SpiBeforeSleepSettled`, and the SSD1681 model stays in deep sleep.

## The platform contract

`boards/` is a template over a platform, not a consumer of a base class, so a platform proves itself by compiling. The contract is nowhere in the type system, so `platform/contract.h` spells it out as compile-time assertions. Each platform asserts itself against it at the bottom of its own header, and the error names the missing member. `platform/host/platform.h` and `platform/zephyr/t_echo_plus.h` are both the T-Echo Plus, on the host and on silicon.

On silicon each board has its own composition, which brings up the parts that board has and no others. The adapters (`clock.h`, `link.h`, `kvstore.h` and the rest) are shared. `platform/zephyr/meshtracker_x1.h` is the X1's, which `boards/seeed/meshtracker_x1/board.h` is written over for skyBlip Bravo:

- `meshtracker_x1_lines.h` holds the board's own lines, which the smoke image drives alone: the button, the lamps, the buzzer, the battery and charger lines, the settings store and the NOR.
- Around them it adds the LR2021's SPI and IRQ, `platform/zephyr/ag3335.h` (the AG3335's UART, PPS and five control lines), the RTC's bus, the log partition, the link, DFU, and `meshtracker_x1_power.h`, the X1's walk of `core/power`'s power-down order.
- Its `begin_wake()` reads the reset causes and configures the button and charger lines alone, so the product decides whether to run before `begin()` brings anything up.

`platform/host/meshtracker_x1.h` is the same platform on the host, every part a model. The X1 has no panel, no IMU image, no pad, and a second I2C bus, `io::BusId::Rtc`, so it does not assert `platform/contract.h`, which is the T-Echo's.

A platform has three kinds of member, each listed in `platform/contract.h`:

- The buses and lines a part is constructed over: `spi()`, `i2c()`, `uart()`, `uart_rate()`, `gpio()`, `wire()` and `delay()`.
- The ports it fills directly, which the board hands to `ports::Roles`, such as `clock()`, `link()` and `key_value()`. `link()` has a wire format outside apps depend on ([`platform/zephyr/README.md`](platform/zephyr/README.md)).
- The producers the board polls, such as `read_barometer()`, `read_divider_mv()`, `button_down()` and `pps()`. They are not ports, because nothing calls them on the core's behalf: a service receives the `events::` value the board pushes.

`capabilities()` asks no part, because the board's constructor calls it before `main()` arms the watchdog. `barometer_answers()` reads the BME280's chip id over I2C, so the board calls it from `probe()`.

`read_barometer()` is one fetch, because the BME280 measures pressure and temperature in the same conversion. The temperature carries its own validity: a pressure without a temperature is still a pressure, and the `$LK8EX1` a tablet reads has a sentinel for the missing field. The sensor sits inside the case and reads warm, and no correction is applied.

`imu_firmware()` is about a hundred kilobytes of vendor firmware the BHI260AP must be given before it is a sensor. It is a platform member, not a constant in the part, so only the device image carries it: the host build hands over eight bytes its model boots on, and the browser's WASM stays small. A base image carries none and hands over an empty span. It has `imu_firmware_digest()`, the SHA-256 of the image it was built against, and `imu_flash()`, the partition where a full image left a copy ([`parts/bhi260/firmware/README.md`](parts/bhi260/firmware/README.md)).

## The settings store, over what another firmware left

`platform/zephyr/kvstore.h` mounts NVS on the first three 4 KB sectors of `storage_partition`: 0xEA000..0xED000 on the X1 and 0xEC000..0xEF000 on the T-Echo. A unit that ran Meshtastic 2.8.0 or later may still hold Meshtastic's WarmNodeStore ring at 0xEA000..0xED000 (`meshtastic/firmware` `v2.8.0.47db0e3`, `src/mesh/WarmNodeStore.h:93-97`): all three of the X1's sectors and the first of the T-Echo's. Each page opens on a magic, `WRNG`, `WRN2` or `WRN3` (`WarmNodeStore.cpp:16-18`), and keeps its last 8 bytes erased. NVS reads those bytes as a sector it never closed, so Zephyr 4.5.0's `nvs_startup` may erase part of it, restart garbage collection, mount over it or refuse it (`subsys/kvss/nvs/nvs.c:963-1208`). A Bravo whose store does not mount keeps its settings in RAM and forgets its pairing at every boot.

So `begin()` runs `platform/store_mount.h` first. The partition is ours from the first install, so erasing what another firmware left is the intent:

- If any of the three sectors opens on a WarmNodeStore magic, it erases all three once and mounts.
- Otherwise it mounts. If NVS refuses the content, `-EDEADLK` or `-ESPIPE`, it erases all three and mounts again.
- Any other failure comes from the flash or the configuration, which an erase cannot cure, so it erases nothing and the store stays down.

Our own values cannot open on a magic, which starts 0x57 0x52 (`W`, `R`). A settings blob opens on its version byte (`core/settings/blob.h`), 1 on Bravo and 13 on the Go. The update record opens on the major and minor of the image it updated from (`core/dfu/update.cpp`), so it matches only for a version 87.82.

Meshtastic's LittleFS sits at 0xED000..0xF4000 (`meshtastic/Adafruit_nRF52_Arduino` `0fd295f1`, `libraries/InternalFileSytem/src/InternalFileSystem.cpp:29,34`), outside the X1's sectors. On the T-Echo the second and third sectors fall inside it. Nothing here recognises a LittleFS block: NVS decides, and a refusal erases it like any other content.

## The power-failure comparator, through nrfx

`platform/zephyr/supply_monitor.h` arms the nRF52840's power-failure comparator, POFCON, through nrfx, because Zephyr has no driver or binding for it (checked at Zephyr v4.5.0, hal_nordic `c708b9fe`). `core/power` decides what a warning means (`core/power/cutoff.h`, `may_write`).

- The nRF POWER binding carries only `reg` and `interrupts` (`dts/bindings/power/nordic,nrf-power.yaml`). Zephyr's Nordic comparator drivers drive COMP and LPCOMP, other peripherals on other pins.
- nrfx exposes it: `nrfx_power_pof_init`, `nrfx_power_pof_enable` and `nrfx_power_pof_disable` (`nrfx/drivers/include/nrfx_power.h`), over `nrf_power_pofcon_set` (`nrfx/hal/nrf_power.h`).
- Zephyr already routes the interrupt. POWER and CLOCK share one IRQ line. The clock driver connects it at boot to `clock_control_nrf_common_irq_handler()`, which calls `nrfx_power_irq_handler()` when `CONFIG_NRFX_POWER` is set (`drivers/clock_control/clock_control_nrf_common.c:211-233`). So the monitor registers a handler and never touches an IRQ.
- `products/common/prj.conf` sets `CONFIG_NRFX_POWER`. Nothing needs `nrfx_power_init()`: `nrfx_power_pof_init` only stores the handler, and `nrfx_power_pof_enable` only writes POFCON and INTENSET (`nrfx/drivers/src/nrfx_power.c:172-209`).

The threshold is 2.8 V, the highest POFCON offers for VDD (nRF52840 PS v1.8 section 5.1.3, POFCON.THRESHOLD). The earlier the warning, the more time it gives, and a threshold under 2.8 V would warn after the write it was meant to prevent. Everything above it is the divider's business, since our cutoff is a 3.2 V cell. It is below any healthy cell and above the SoC's 1.7 V brownout floor. The bench must confirm that a 14 dBm burst on a 3.4 V cell leaves VDD above 2.8 V. If it does not, fix the decoupling, not the threshold. The repository holds no T-Echo Plus schematic, so whether VDD is the cell or a regulated 3.3 V rail is unknown, and the choice is the same either way.

The header compiles without the Kconfig, because the platform header reaches suites with no reason to carry nrfx_power. Without it the comparator is an absent capability: `armed()` says false, the product logs it, and `core/power` falls back to the voltage rule. `armed()` reads the register back, so a comparator that failed to arm is never reported armed.

## The two questions a pin and a register answer

`buzzer_pin_held_low()` and `read_panel_id()` are read before any driver owns the pins, because LilyGO fits more than one thing behind the same footprint.

The buzzer pin is read high-Z and then against the internal pull-up. Held low both times, something pulls it to ground, and on this hardware that is the piezo's drive stage. A T-Echo Plus reads it low and has a buzzer. A plain T-Echo has an empty pad, follows the pull-up and has none. The board concludes `Capability::Buzzer` from that reading alone (`boards/lilygo/t_echo_plus/board.h`), as it concludes `Capability::Haptic` from a DRV2605 answering at 0x5A.

`parts/ssd1681/panel.h` keeps the panel ID's two ways of missing apart, because different people fix them. `Panel::Unknown`, printed `NO ID`, means nothing could be clocked out: the board port did not run, or the pins were not ready. `Panel::Unlisted`, printed `UNLISTED`, is an ID that was read and matches no row of SoftRF's table. That is the expected answer on a T-Echo Plus, whose row in that table carries a date string and no bytes. A bench that sees `UNLISTED` has a panel worth writing down. A bench that sees `NO ID` has a board that answered nothing.

## Parking the LEDs

The T-Echo's LEDs hang off the gated peripheral rail (SoftRF `platform/iomap/LilyGO_TEcho.h:79-80`: "Power: EINK, RGB, CN1") and are active-low, so off is the pin driven high. A pin held high once the rail collapses sources current back through the LED. So `Indicator::park()` switches them off and then disconnects them, as SoftRF's `SoC_fini` does (`platform/nRF52.cpp:2799-2807`), rather than `show(Colour::None)` alone. It uses `GPIO_DISCONNECTED`, not `GPIO_INPUT`, because an input keeps the pin's input buffer powered. The next `show()` reconfigures the pin as an output.
