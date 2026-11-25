# core/power

What a cell's terminal voltage means, what the device does about it, and the order it goes dark in. `charging.*` holds the charge temperature window and `reset_reason.*` names the last reset.

The cell is a 4.2 V LiPo pouch: 2400 mAh on this board and 850 mAh on the plain T-Echo, with the same values for both. The voltage gives the fraction of charge left, never the time it lasts.

## The ladder

`static_assert`s in `cutoff.h`, `wake.h` and `battery.h` hold the order of these steps.

| Step | mV | Constant | What it is for |
|---|---|---|---|
| float | 4200 | `kFullMv` | what a charger holds a full pouch at, and the gauge's 100% |
| low | 3600 | `kLowMv` | the knee, about 12%: above it 100 mV crosses ten points of charge, below it twenty |
| critical | 3500 | `kCriticalMv` | land, about 5%. No durable write but the flight record |
| boot lockout | 3400 | `kBootLockoutMv` | a device this flat does not start a flight it cannot finish |
| flat | 3200 | `kFlatMv`, `kEmptyMv` | the cutoff: the device takes itself down. The gauge's 0% |

The gauge reads zero where the device stops, so a pilot never sees a percentage run out while the aircraft still transmits. `battery.cpp` asserts the curve's first point against `kEmptyMv`.

The lockout sits above the cutoff because a cell relaxes once the load goes: a device that shut down at 3200 mV reads about 3400 mV by the next press. It guards a switch-on, not a restart: a watchdog, lockup or software reset may come in flight, so that boot answers only to the cutoff.

The cutoff is a loaded reading. Stopping at 3.2 V under load leaves the pack resting near 3.4 V, where it can sit for months without reaching the datasheet's 3.0 V floor or the protection board's 2.5 V trip.

## The levels, and what each one does

Every reader of the cell takes the `PowerLevel` and none compares millivolts again, so no two readers disagree.

| Level | Entered under | Lamp | Radar ring | Status row | Glass once off |
|---|---|---|---|---|---|
| `Normal` | | green or blue, one wink every 3 s | the state word | `%` | the wordmark |
| `Low` | `kLowMv` | red, one wink every 3 s | `BAT n%` | `% LOW` | `CHARGE THE BATTERY` |
| `Critical` | `kCriticalMv` | red, a blink every 600 ms | `BAT n%` | `% LOW` | `CHARGE THE BATTERY` |
| `Flat` | `kFlatMv` | red, a blink every 600 ms, then dark | `BAT n%` | `% LOW` | `BATTERY EMPTY` |

`needs_charge` is true from `Low` down. A stow leaves the glass blank at any level. The lamp tells `Low` from `Critical` by cadence: `Low` can last an hour and must cost no more than the alive wink, and `Critical` is the step a pilot must act on.

`CutoffMonitor` moves the level only on a run of `kLevelSamples` (3) consecutive readings, so a transmit burst never acts. Going back up, every reading of the run must also clear the step by `kRecoveryMarginMv` (35 mV), or a parked cell straddling a line flickers across it. The margin is the half-hour spread of a parked cell's median on E68BD9's run (`docs/bench/2026-10-05-power-run-E68BD9.md`). A `static_assert` keeps `kCriticalMv` plus the margin under `kLowMv`, so a cell climbing out of `Critical` lands in `Low`. A recovery climbs every step its readings cleared at once.

The cable sets `Normal` on its first reading and nothing counts until it leaves, because the charge current holds the terminal above the cell. `Flat` latches, so a cell relaxing once the radio is quiet cannot cancel its shutdown. An `Unknown` monitor has seen no believable reading yet, as with an unpopulated divider, and acts like `Normal`.

The enum's values are the diagnostics record's level byte, not ladder order (`../diagnostics/README.md`). Ask `needs_charge` or switch on the level. Never compare it.

## The write rule, and the supply warning

From `Critical` down, `may_write` refuses every durable write except the flight record. The settings blob rewrites an NVS sector in the flash the image runs from, and a burst sagging the cell during that rewrite is the failure. The flight record is worth most when the cell is lowest, and it is an append to the external NOR.

A latched POFCON supply warning stops every write but the flight record, whatever the level. POFCON watches the SoC's own rail, well under any healthy cell, so when it fires the brownout reset is milliseconds away and a 3 s panel park cannot finish. It therefore never sets `Flat` or starts a shutdown. `supply_warnings` counts the firings: any on a healthy cell means a supply problem or a threshold set too high.

## The curve, and the cable

On the cable the divider reads the USB rail, not the cell: 4670 to 4700 mV on E68BD9 and above 4700 mV on 0B1B2C (`docs/bench/2026-10-05-power-run-E68BD9.md`, #114). So on the cable the gauge reports `valid` false, `percent` 0 and `cell_mv` as the divider read it, as evidence of the rail. `charging` is the cable itself, because this board has no charger status pin.

The gauge shows the median of the last three readings, so a transmit burst is discarded whole. Off the cable the percentage only falls.

The discharge table is the textbook shape for a single pouch cell, not this pack measured on this board, and is worst at the ends. A logged discharge of a fitted unit under the real dwell map replaces its points. Until then the gauge gives no time remaining: minutes are the percentage over the curve's slope, and a small error in the level becomes a large one in the slope.

## The window, and what a refused reading leaves behind

A reading is the cell only inside `plausible_mv`: above `kImplausibleFloorMv` (1800 mV), which an unpopulated or floating divider sits under, and at most `kImplausibleCeilingMv` (4700 mV), which a rail reaches and no 4.2 V cell does. The platform hands back what the divider read either way, 0 when the ADC gave nothing, with VBUS beside it, so a refused reading never hides the cable.

The gauge counts each refusal (`implausible` in the `power` record) and shows the refused value as `cell_mv` with `valid` false. Off the cable the cutoff monitor never sees a refused reading, so it neither counts towards a level nor breaks a run.

## The trim, set on the line

`settings.battery_offset_mv` is the only trim: one number per unit, measured against a bench supply on the line. Two 1% resistors put the divider out by up to 84 mV at a full cell, and the SAADC's gain error adds more, which is tens of percentage points on the flat middle of the curve. The service applies it once, so the gauge and the cutoff monitor act on the same millivolts.

`kCalibrationCeilingMv` (250 mV) is three times the divider's worst case: past it the reading is wired wrong. `core/settings` enforces it where the value enters.

The device cannot trim itself, because the divider reads the USB rail while the cable is in and never sees the charger's float.

## What the glass says about a flat cell, and when it says it

A device that reaches `kFlatMv` paints `BATTERY EMPTY` before the rails go, because nobody pressed anything and the frame is the only way a pilot learns what happened. A cell that emptied on a shelf painted nothing, so `refused_frame` pushes the same frame for the refused press. Later refused presses leave it there: anything but a new word is `Leave`, because a full refresh is seconds of panel rail off a cell with none to spare.

A device switched off while the cell is `Low` or `Critical` wears `CHARGE THE BATTERY`. The next press gets a device, unless the cell has since drained past the lockout: that refusal pushes `BATTERY EMPTY` over it.

The platform's `system_power().cell_on_glass()` keeps the word across SYSTEM OFF. A second retained bit, `went_dark_flat`, is set wherever the cutoff or a refused boot parks the flat frame. The next boot that runs reports it on the self-test page and in the status reply, then drops it, so a support case learns it after the glass has changed.

The cable alone changes nothing on the glass, plugged in or pulled out. A press on the cable gets a device whatever the cell reads, because the lockout spares external power, and its first frame takes the word off. Until then the charger's own red LED says the cell is filling.

### The charger wake

A charger plugged into a switched-off device must not wake it. On the T-Echo the factory bootloader ensures that: every SYSTEM OFF writes `0x6d`, `DFU_MAGIC_SKIP`, to GPREGRET (`boot_magic_for_system_off` in `shutdown.h`) so a waking press skips DFU, and LilyGO's build sends a VBUS wake back to SYSTEM OFF whenever that byte is set (`../../boards/lilygo/t_echo_plus/factory/README.md`). The X1's bootloader lets a cable wake through, so there the rule rests on `boot_path` (`../../boards/seeed/meshtracker_x1/factory/README.md`).

`boot_path` refuses a wake with RESETREAS.VBUS set, which the nRF52840 sets only on a wake from SYSTEM OFF by VBUS rising (`nrf52840_bitfields.h:8848`, in hal_nordic's MDK). A button wake sets OFF instead (`:8872`), and a power-on or a brown-out leaves the register empty (nRF52840 PS v1.8 §5.3.3), so a first cell, or a cable into a unit with no cell, boots. Zephyr's hwinfo reports VBUS as `RESET_POR` (`drivers/hwinfo/hwinfo_nrf.c:138-141` at v4.4.2), so `read_reset_causes()` reads the bit itself, and a charger wake arrives as `UsbVbus` with `PowerOn`. The rule refuses VBUS alone, as SoftRF does on the X1 (`lyusupov/SoftRF` `d091924`, `src/platform/nRF52.cpp:925-931`). A reset-pin reset, a held button and a restart all boot.

### Arming the button

The wake pin senses a level, so every road to SYSTEM OFF waits for the button to come up, the cutoff and a refused press included. Nothing bounds the wait, so a bag holding the button down keeps the unit awake. With `power_switch` on `Button` the button always wakes, because, with the cable swallowed by the bootloader, a unit whose button was withheld could be woken only by the reset pin (#113). Cable mode is the one exception, below.

## Cable mode

`settings.power_switch` picks what switches a Go on and off. `Button` is the default and everything above. `Cable` is for a Go wired to the aircraft's USB socket, with its cell left in, so it follows the master switch (#488). The Go+ and the Go both have it, on the menu's `POWER` row. Bravo has no such setting and stays on its button.

The hardware already takes the cell out of the load on the cable. Q7, an SI2301 P-FET between VBAT and the 5 V rail, has VBUS on its gate, so with the cable in the rail runs from VBUS through D7 and the cell only charges (LilyGO's `schematic/T-Echo_Schematic.pdf`). When the cable goes, Q7 closes and the cell carries the unit, so a cranking dip should not reset a Go with its cell in. No bench has checked that yet.

In cable mode:

- Every SYSTEM OFF writes GPREGRET `0x00` instead of `0x6d` and leaves the button unarmed (`boot_magic_for_system_off` and `button_wakes` in `shutdown.h`). With no skip, the T-Echo's bootloader lets a VBUS wake through (`../../boards/lilygo/t_echo_plus/factory/README.md`), and `boot_path` runs it. The X1's bootloader enters DFU only on a 3 s hold, so there the button stays armed.
- A cable wake signs on, as a press does, so a pilot sees the unit come on with the master.
- `CableWatch` switches the unit off once the cable has been gone for `kCableGoneHoldMs`, with `ShutdownReason::CableGone`. It counts only a cable it saw, so a unit woken on its cell by RESET runs until a long press or the cutoff. The boot after an install counts the cable as seen, because the restart hid it from the image that boots, so a Go whose cable went during an update still switches off. That boot is any boot whose image state is not `confirmed`: the new image on probation, or the old one back after a revert or a refusal (`../dfu/README.md`).
- The radar's ring counts the hold down, `OFF IN` over `4 MIN`, from `bus::PowerState::cable_off_in_ms` (`../../products/skyblip_go/pages/README.md`).
- The hold runs only on the ground and with no update under way. A cable lost in flight costs the cell, and an airborne unit that switched itself off would stop transmitting. An update holds it from the press that opens the upload window until the window closes or the image installs (`ConfigService::updating()`), because a unit that switched itself off would cut the upload. After a landing, or once the window closes, the hold starts again.
- The VBUS wake fires on VBUS rising. A cable that comes back while the unit parks for its loss would find it in SYSTEM OFF with nothing to wake it, so the unit restarts instead (`Shell::cable_returned`).
- A long press or the link's `power_off` with the cable in switches the unit off until the cable next comes back, or RESET.

`kCableGoneHoldMs` is 5 minutes, to ride out a start that fails. Each crank lasts seconds, and the cell bridges it. The longest gap is a pilot who switches the master off while the starter cools. Rotax gives the 912 2 minutes of cooling after 10 s of cranking (912 Series Maintenance Manual Line, 2.8). Lycoming rests the starter 5 minutes between cranks of 10 to 12 s (IO-320 Operator's Manual 60297-31, section 3). A hold that runs out early costs a reboot on the next master-on, with a GNSS fix to find again. A hold that runs long costs a few minutes of the cell transmitting at the parking spot, which the next cable refills.

On a cell, a unit in cable mode does not wake on its main button. With GPREGRET `0x00` the waking press is still held when the bootloader reads P1.10, and it starts DFU with no timeout. RESET, the upper button on the left side, wakes it as a pin reset. For the same reason, a unit plugged in with the main button held lands in DFU.

The soft off was considered and kept as the fallback: System ON with every part parked, woken by USBDETECTED. It never resets, so the bootloader is never involved and the button keeps working, but it costs some uA over SYSTEM OFF, not measured, and needs the watchdog paused or fed.

## Charge mode, considered and refused

A charger wake does not beep the battery level, as SoftRF-moshe-braner does. The charger IC drives its own LED, `core/indication/lamp.h` has no charge row, and on the T-Echo the charger wake never reaches this firmware.

## Time in state, because there is no current

`OnTime` in `duty.h` counts the milliseconds a consumer was on: the backlight, the annunciator and the companion link, each counted by the service that drives it and published on `bus::State::duty` (`../bus/README.md`). Multiplied by per-consumer milliamps from a datasheet or a bench, it is the only power figure this device can produce.

It credits the span between two observations, so a pass that ran long carries its own length and the unsigned difference survives the 49.7-day clock wrap. A flip between two observations is credited to the latched state, so the error is one pass and does not accumulate.

## The off state, on every road

`drive_shutdown`, run by every switch-off, and `park_refusal`, run by a refused boot, park the same parts. An install reboots into the bootloader and takes neither.

- The radio executor is parked for good. SYSTEM OFF waits for it, at most `kRadioParkCeilingMs` (2 s), which only a hung radio reaches. Past it the thread is aborted, possibly holding the SPI bus lock, so SYSTEM OFF sends no SPI and the radio stays in STDBY_RC, about 0.6 mA by the datasheet.
- The lamp, the buzzer and the motor are parked, after the sign-off on a switch-off (`../indication/README.md`). The last duty record, written as the shutdown starts, leaves out the sign-off's motor time.
- A DRV2605 that answered at bring-up goes to standby with its enable, P0.08, held low. Otherwise P0.08 is released to an input: on a Plus that stops it back-feeding the driver, and on the plain T-Echo it is console RX.
- The panel wears its last frame and gets deep sleep (0x10), at `kRefusalParkCeilingMs` at the latest on a refusal.

Then `power_down` walks `kPowerDownOrder`:

- `RadioSleep`: NRESET pulsed for 100 us, BUSY awaited for up to 10 ms, then SetSleep with cold start, 160 nA against 600 nA warm (DS 13.1.1). The pulse puts the part in STDBY_RC, the only state SetSleep is valid from.
- `ExternalFlashDeepPowerDown` and `ExternalFlashLinesReleased`: 0xB9, then CS, WP# and HOLD# disconnected.
- `PanelLinesReleased`: the seven panel lines disconnected.
- `SensorBusReleased`: TWIM0 disabled and SDA and SCL disconnected, because Zephyr leaves it enabled without device power management.
- `GnssStandby`: the L76K's WAKE_UP driven low and kept low. That is its standby, so no `$PCAS12` is sent.
- `GnssLinesReleased`: the UART stopped and disabled, TX, RX and RESET_N disconnected with no pull. RESET_N is never pulsed.
- `PeripheralRailOff`: P0.12 driven low into SYSTEM OFF, because a floating one restarted a unit (Meshtastic #10510). The BME280 and BHI260AP sit behind it and get no sleep command.
- `WakePinArmed`: `kRailSettleMs` (20 ms) later. With `power_switch` on `Button` only the button wakes this device. In cable mode on the T-Echo the pin stays unarmed (Cable mode, above).

P0.13 is never lowered: per the vendor schematic it enables U11, the AP2112K-3.3 that feeds the SoC and the radio.

## What is still unmeasured

No current is sensed anywhere on this board, so nothing here can count coulombs and no figure in minutes may be published. The sleep current the shutdown sequence leaves has never been measured either.

LilyGO measured 56.77 uA in SYSTEM OFF on this board with its own sequence (T-Echo_V1.0 Power Consumption Test, 2024-09-09). The battery divider, R18 and R22 at 100k each, draws about 19 to 21 uA whatever the firmware does. A meter in series with the pack after a switch-off reads the real figure, and a blinking GNSS PPS LED (LED6) on a switched-off unit means the receiver still runs.

A power run from full to cutoff, read by `scripts/power_budget.py`, gives the average draw off the pack's capacity and splits it across consumers by the `duty` records (`scripts/README.md`).
