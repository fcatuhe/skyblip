# core/power

What a terminal voltage means, what the device does about it, and the order it goes dark in. `battery.*` is the gauge a pilot reads, `cutoff.*` the rule that acts, `charging.*` the temperature window, `wake.*` whether a boot becomes a device, `shutdown.*` the road out, `reset_reason.*` what the last one was, `duty.h` how long a consumer was on.

The cell is a 4.2 V LiPo pouch: 2400 mAh on this board, and LilyGO fits the same footprint with an 850 mAh pack on the plain T-Echo. Nothing below changes between them. A pouch cell's voltage says what fraction of the charge is left; only its capacity says how long each fraction lasts, and capacity is the thing no reading here can see.

## The ladder

Every other number about the cell hangs off these four, so they are declared together in `cutoff.h` and a `static_assert` holds their order.

| Step | mV | What it is for |
|---|---|---|
| float | 4200 | what a charger holds a full pouch at, and `kFullMv`, the gauge's 100% |
| low | 3600 | the knee: above it the cell spends 100 mV crossing ten points of charge, below it the same 100 mV costs twenty. `kLowMv`, about 12% |
| critical | 3500 | land. No durable write but the flight record is allowed. `kCriticalMv`, about 5% |
| boot lockout | 3400 | a device this flat does not start a flight it cannot finish (`wake.h`) |
| flat | 3200 | the cutoff: the device takes itself down, and `kEmptyMv`, the gauge's 0%. `kFlatMv` |

Three properties of that order are worth more than the numbers themselves.

The gauge reads zero where the device stops, not where a datasheet calls the cell empty. A percentage that runs out while the aircraft is still transmitting teaches a pilot to distrust the number, and `battery.cpp` asserts the curve's first point against `kFlatMv` so the two cannot drift apart.

The lockout sits above the cutoff. A cell relaxes once the load goes away, so a device that shut itself down at 3200 reads about 3400 by the time a thumb reaches the button, and a lockout underneath the cutoff would hand back a device with one minute in it.

The lockout guards a switch-on, not a restart. A watchdog, lockup or software reset means the device was running a moment ago, possibly in flight, so that boot is held to the cutoff the running device answers to, and a fault never leaves a flying unit dark with a cell the cutoff would have kept on air (`wake.cpp`).

The cutoff is a loaded reading, because a loaded reading is the only kind this device ever takes. A pouch datasheet puts the discharge floor at 3.0 V and its protection board trips near 2.5 V; stopping at 3.2 V under a receiver and a 14 dBm radio leaves the pack resting near 3.4 V, which is a voltage it can sit at in a flight bag for months without reaching either.

## The levels, and what each one does

Three of the steps are a `PowerLevel`, and the level is the one fact every reader of the cell takes: the lamp, the radar ring, the status row, the frame the glass wears once off, and the write rule. None of them compares millivolts again, so they cannot disagree about where the cell is.

| Level | Entered under | Lamp | Radar ring | Status row | Glass once off | Refuses |
|---|---|---|---|---|---|---|
| `Normal` | | green or blue, one wink every 3 s | the state word | `%` | the wordmark | nothing |
| `Low` | `kLowMv` | red, one wink every 3 s | `BAT n%` | `% LOW` | `CHARGE BATTERY` | nothing |
| `Critical` | `kCriticalMv` | red, a blink every 600 ms | `BAT n%` | `% LOW` | `CHARGE BATTERY` | every durable write but the flight record |
| `Flat` | `kFlatMv` | red, a blink every 600 ms, then dark | `BAT n%` | `% LOW` | `FLAT BATTERY` | everything: the device takes itself down, and a press after it is refused |

`needs_charge` is the one predicate the product asks, true from `Low` down, and it is what puts `BAT` in the ring, `LOW` on the status row and `CHARGE BATTERY` on the glass of a long press or a companion switch-off. A stow stays blank at any level. The lamp tells `Low` from `Critical` by cadence, because the first can stand for an hour and has to cost what the alive wink costs, and the second is the step a pilot is meant to act on.

`CutoffMonitor` moves the level on runs of `kLevelSamples` consecutive readings, in both directions. Three under a step go down to it, which is what keeps a transmit burst from acting, and a falling cell is not slowed by anything more. Coming back up takes three that clear the step by `kRecoveryMarginMv` (35 mV), which is what keeps a cell resting on a step from flickering across it. The run alone did not: on E68BD9's run to cutoff (`docs/bench/2026-10-05-power-run-E68BD9.md`) the level crossed between `Normal` and `Low` five times in five minutes at 3591 to 3601 mV, and between `Low` and `Critical` three times in a minute, because three readings a second apart on either side of a line are routine while a parked cell takes minutes to cross it. The margin is that run's spread: the gauge's median moves 35 mV inside a half hour at the median of its half-hour windows, and three readings that all clear a line have a median that clears it too, so a median that wanders less than the margin never brings the level back up. A `static_assert` keeps `kCriticalMv` plus the margin under `kLowMv`, so a cell climbing out of `Critical` lands in `Low` rather than having to clear it too. A recovery climbs every step its readings cleared at once. The cable is the exception both ways, because the charge current holds the terminal above the cell: a cable puts the level at `Normal` on its first reading and nothing counts until it leaves. `Flat` latches, so a cell relaxing once the radio is quiet cannot cancel the shutdown it started.

An `Unknown` monitor has seen no believable reading yet, which is also what an unpopulated divider reads for ever, and it acts like `Normal`. Its first reading at or above `kLowMv` makes it `Normal`; one under waits for the run like any other step.

The enum's values are the diagnostics record's level byte, so they are not in ladder order: `Low` joined last and took code 4, and `Critical` and `Flat` kept the codes and thresholds they had as `Low` and `Cutoff`, so every capture already on a flash decodes to the same step (`../diag/README.md`). Readers therefore ask `needs_charge` or switch on the level, never compare it.

## The write rule, and the supply warning

`may_write` is one function over the level and no second threshold: from `Critical` down nothing durable is written except the flight record. The settings blob is a rewrite of an NVS sector that garbage-collects the flash the image runs from, and a burst sagging the cell under it is the failure. The flight record is the exception because it is the one write whose value is highest exactly when the cell is lowest, and it is an append to the external NOR. A unit whose divider is unpopulated reads `Unknown` and keeps its settings page.

A latched supply warning outranks the level entirely. POFCON compares the SoC's own rail, so once it has fired what the divider says about the cell is no longer the question, and every write but the flight record stops. It never drives the level to `Flat`, so it never starts a shutdown: POFCON warns about VDD, well under any healthy cell, so by the time it fires there is no orderly shutdown left to run, the panel park alone is 3 s and the brownout reset is milliseconds away. The one useful thing at that moment is not to be inside a write. The voltage rule keeps the shutdown, where three consecutive samples are the evidence.

## The curve, and the cable

`percent_from_mv` reads one table, and only off the cable. On these boards the divider cannot see the cell while USB is in: both bench units read the USB rail for the whole charge, 4670 to 4700 mV on E68BD9 from a cell switched off at 45 %, and above 4700 mV on 0B1B2C (`docs/bench/2026-10-05-power-run-E68BD9.md`, #114). A cell in constant current reads well under 4.2 V, so that reading is not the cell and no curve can make a state of charge of it. LilyGO's own wiki warns that battery readings are wrong while USB is connected.

So on the cable the gauge shows no cell. `valid` is false, `percent` is 0 and `millivolts` is what the divider read, which a capture keeps as evidence of the rail. `charging` is the cable itself: this board has no charger status pin, so nothing says when the charge ends, and a full cell on the cable still reads `charging`. The status reply leaves `battery_percent` out, the status page reads `-- V --% CHG`, and the first reading off the cable starts the median over on the charged cell, which is the first number worth showing. There is no charge curve: the reading it needed never reaches the device.

The discharge table is the textbook shape for a single pouch cell, not this pack measured on this board. It is right to a few percent in the middle and worst at the ends, which is where a pilot cares. What replaces it is a logged discharge of a fitted unit under the real dwell map, which the power budget needs anyway: the shape of the table does not change, only its points. `battery.h` carries the same warning where it explains why there is no time-remaining estimate, and that estimate stays refused until the same log exists.

## The window, and what a refused reading leaves behind

A reading is the cell only inside `plausible_mv`: above `kImplausibleFloorMv` (1800 mV), which an unpopulated or floating divider sits under, and at most `kImplausibleCeilingMv` (4700 mV), which no 4.2 V cell reaches and a rail does. The platform's read still answers whether a reading is believable, but it hands back what the divider read either way, 0 when the ADC gave nothing, and the board publishes it on every battery pass with VBUS beside it. A refused reading never hides the cable again, so the cutoff monitor's cable override and the charge window both see it.

The gauge counts each refusal, on the cable or off it (`Gauge::refused`, the `implausible` count in the `power` record and the diagnostics reply), and shows the refused value as its `millivolts` with `valid` false, so a capture says which bound a reading crossed. Off the cable a refused reading is no step on the ladder: the cutoff monitor never sees it, so it neither counts towards a level nor breaks a run.

## The trim, set on the line

`kCalibrationLimitMv` and `settings.battery_offset_mv` exist because two 1% resistors and the SAADC's own gain error can put a reading out by more than 80 mV, which is tens of percentage points in the flat middle. The answer is one number measured against a supply on the line, and it is the only trim there is.

The device cannot trim itself. A charger in constant voltage holds the cell's terminal at its float, so a reading taken there would have a known true value, but the divider reads the USB rail while the cable is in, so no float plateau ever reaches it. The learner that waited for one never learned on either bench unit, and a rail that happened to sit within `kCalibrationLimitMv` of 4.2 V would have taught it the rail's error and moved the cutoff by as much, so it is gone. `settings.battery_offset_manual` stays in the blob and in the `Config` record: every trim since is one a person set.

## What the glass says about a flat cell, and when it says it

A device that reaches `kFlatMv` in the air says so before the rails go: `FLAT BATTERY` under the mark, pushed by the shutdown the cutoff asked for. Nobody pressed anything, so the frame is the only thing that can tell a pilot what happened, and the alternatives they would otherwise pick between are a crash and a dead unit. The button stays armed, as it does after every switch-off. A press that follows is refused by the lockout (`wake.h`) and leaves the frame already on the glass, so the cell pays for one bring-up (`../../products/skyblip_go/README.md`) and not for a refresh or a flight it cannot finish.

The other way a cell arrives empty is a winter on a shelf. That unit ran no shutdown and painted nothing, so it is the refused boot that names it: `refused_frame` pushes the same `FLAT BATTERY` for the press that gets no device, and the button stays armed after it. Both roads end at the same glass, and every later press on that cell is refused and leaves it there: anything but a new word is `Leave`, because a full refresh is seconds of panel rail off a cell with none to spare.

The cable is the way out of both, with a press. The lockout spares external power, so a press on the cable gets a device whatever the cell reads, and its first frame takes the word off the glass. The cable alone wakes nothing: VBUS wakes the SoC, and the factory bootloader puts it straight back into SYSTEM OFF before MCUboot or this firmware runs. So `FLAT BATTERY` stays on the glass for the whole charge, until the press, and the charger's own red LED is what says the cell is filling.

That holds by the bootloader, not by this firmware. The factory bootloader reads the device's one button as its DFU button, so every SYSTEM OFF writes `0x6d`, `DFU_MAGIC_SKIP`, to GPREGRET (`boot_magic_for_system_off` in `shutdown.h`), or the press that wakes the unit would land in the UF2 drive, and LilyGO's build of it sends a VBUS wake back to SYSTEM OFF whenever that byte is set (`../../boards/lilygo/t_echo_plus/factory/README.md`). So "a charger plugged into a device in a flight bag must not wake it" holds before `boot_path` is asked. `boot_path` refuses a charger wake again, as the defence for a bootloader that would let one through, and on such a bootloader `refused_frame` would take a word off the glass and put back the wordmark, the frame of every armed device.

Arming the button means waiting for it, because the wake pin senses a level and a button still down would wake the unit the instant the rails dropped. The cutoff waits in `AwaitRelease` like every other shutdown, and a refused press waits in `park_refusal` for the finger it refused to come up. Nothing bounds either wait, so a bag holding the button down keeps the unit awake with its rails up until it lets go. That is the price of a button that always wakes: the cutoff and a refused press used to withhold it against exactly that bag, and with the cable swallowed as well, a unit the cutoff switched off could then be woken by nothing but the reset pin (#113).

A device switched off while the cell needs charge, `Low` or `Critical`, wears a milder word, `CHARGE BATTERY`. The cable does not take it off either, and the next press gets a device. It asks for the cable and names nothing that happened, so a press on a cell that has drained past the lockout since does not leave it there: that refusal pushes `FLAT BATTERY` over it.

What the panel wears has to outlive the rails for that comparison to exist, so it does: `ports::SystemPower::cell_on_glass`, one of `None`, `Low` and `Flat`. A platform with nowhere to keep it answers `None`, which costs a repeated frame and nothing else.

The glass forgets once a press on the cable gets a device, and a support case must not. A second bit, `went_dark_flat`, is set wherever the cutoff or a refused boot parks the flat frame, whether or not the panel could draw it. The boot that runs reads it, prints `WAS FLAT` beside the cell's voltage on the self-test page and answers `went_dark_flat` in the status reply, then drops it, so it names the one boot that followed the flat cell and no other.

The cable leaving changes nothing on the glass either, because VBUS falling does not wake this SoC. A unit unplugged with `FLAT BATTERY` on it keeps the word, and the next press off the cable is answered by the cell: refused against the same frame while it is under the lockout, a device once it has charged past it.

## Charge mode, considered and refused

A charger plugged into a switched-off unit wakes the SoC, and the factory bootloader sends it straight back to SYSTEM OFF, as `boot_path` would. SoftRF-moshe-braner does something else there (`src/platform/nRF52.cpp:2512-2560`): it beeps the battery level out of the buzzer, then sleeps. That answers the question a pilot asks, "is it charging?", and it is not done here, for three reasons.

What the device says out loud is one table, `core/indication/lamp.h`, and charge is deliberately not a row of it: this board's charger IC drives its own LED, which already answers the question. A buzzer ladder in the wake path would be a second vocabulary beside that table.

What it would beep is not yet trustworthy. The gauge medians three samples before it states a percentage (`battery.h`) and the trim is a setting, so an honest level exists most of a boot later, on every cable wiggle in a flight bag, which is the case the refusal exists for.

The refusal is complete without it. A charge mode would be a third `BootPath` value beside the other two, with the rule and the sleep path untouched, and on the factory bootloader it would never run: the charger wake ends before this firmware runs.

## Time in state, because there is no current

`duty.h` is one accumulator, `OnTime`: a thing was on, and this is how many milliseconds of it have gone by. It is what the backlight, the annunciator and the companion link are counted with, each by the service that drives that consumer and each published on `bus::State::duty` (`../bus/README.md`).

It takes the span between two observations rather than a pass at its nominal length, for the reason the air-time ring gives: a pass that ran long has to carry its own length, and an unsigned difference is the only arithmetic that survives the 49.7-day wrap of `ports::Clock::millis()`. A state that flips between two observations is credited to the state that was latched, so the error is one pass and it does not accumulate.

What these buy is the second half of a budget nothing else on this board can supply. Milliamps per consumer come from a datasheet or a bench; how long each was on comes from here, and multiplying the two is the only power figure this device can produce.

## What is still unmeasured

No current is sensed anywhere on this board, so nothing here can count coulombs, and no figure in minutes may be published (`battery.h`). The sleep current the shutdown sequence leaves has never been measured either (`shutdown.h`). Both are the same bench day, and until it happens every number in this directory is a voltage or a decision about one.

What the device can do without a current sense is say how long each consumer was on, which is what the `duty` records carry and what `POWER RUN` on the capture page arms. A run from full to cutoff, offloaded and read by `scripts/power_budget.py`, gives the average draw off the pack's own capacity and needs no curve to do it. The same file says which consumer that draw went to, against a table of per-part currents that is still mostly datasheet typicals. `scripts/README.md` says how to take one, and the residual the tool prints is the honest size of what remains unmeasured.
