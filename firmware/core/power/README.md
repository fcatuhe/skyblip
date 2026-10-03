# core/power

What a terminal voltage means, what the device does about it, and the order it goes dark in. `battery.*` is the gauge a pilot reads, `cutoff.*` the rule that acts, `trim.*` the one calibration the device can perform on itself, `charging.*` the temperature window, `wake.*` whether a boot becomes a device, `shutdown.*` the road out, `reset_reason.*` what the last one was, `duty.h` how long a consumer was on.

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
| `Flat` | `kFlatMv` | red, a blink every 600 ms, then dark | `BAT n%` | `% LOW` | `FLAT BATTERY` | everything: the device takes itself down and leaves the button unarmed |

`needs_charge` is the one predicate the product asks, true from `Low` down, and it is what puts `BAT` in the ring, `LOW` on the status row and `CHARGE BATTERY` on the glass of a long press or a companion switch-off. A stow stays blank at any level. The lamp tells `Low` from `Critical` by cadence, because the first can stand for an hour and has to cost what the alive wink costs, and the second is the step a pilot is meant to act on.

`CutoffMonitor` moves the level on runs of `kLevelSamples` consecutive readings, in both directions. Three under a step go down to it, which is what keeps a transmit burst from acting. Three at or above it come back up, which is what keeps a cell resting on a step from flickering across it: a pack spends minutes within a few millivolts of each line, and a level that rose on one good reading was a lamp, a ring and a switch-off frame that changed their minds every few seconds. A recovery climbs every step its readings cleared at once. The cable is the exception both ways, because the charge current holds the terminal above the cell: a cable puts the level at `Normal` on its first reading and nothing counts until it leaves. `Flat` latches, so a cell relaxing once the radio is quiet cannot cancel the shutdown it started.

An `Unknown` monitor has seen no believable reading yet, which is also what an unpopulated divider reads for ever, and it acts like `Normal`. Its first reading at or above `kLowMv` makes it `Normal`; one under waits for the run like any other step.

The enum's values are the diagnostics record's level byte, so they are not in ladder order: `Low` joined last and took code 4, and `Critical` and `Flat` kept the codes and thresholds they had as `Low` and `Cutoff`, so every capture already on a flash decodes to the same step (`../diag/README.md`). Readers therefore ask `needs_charge` or switch on the level, never compare it.

## The write rule, and the supply warning

`may_write` is one function over the level and no second threshold: from `Critical` down nothing durable is written except the flight record. The settings blob is a rewrite of an NVS sector that garbage-collects the flash the image runs from, and a burst sagging the cell under it is the failure. The flight record is the exception because it is the one write whose value is highest exactly when the cell is lowest, and it is an append to the external NOR. A unit whose divider is unpopulated reads `Unknown` and keeps its settings page.

A latched supply warning outranks the level entirely. POFCON compares the SoC's own rail, so once it has fired what the divider says about the cell is no longer the question, and every write but the flight record stops. It never drives the level to `Flat`, so it never starts a shutdown: POFCON warns about VDD, well under any healthy cell, so by the time it fires there is no orderly shutdown left to run, the panel park alone is 3 s and the brownout reset is milliseconds away. The one useful thing at that moment is not to be inside a write. The voltage rule keeps the shutdown, where three consecutive samples are the evidence.

## The two curves

`percent_from_mv` reads one of two tables, because the same reading means two different states of charge. Off charge, the terminal is the cell's own voltage under our load. On charge, the constant-current phase lifts it by the drop across the cell's internal resistance, and the 4.15 to 4.20 V constant-voltage taper is where the last fifth of the capacity goes in at a voltage that barely moves. LilyGO says the same thing from the other end: their own wiki warns that battery readings are inaccurate while USB is connected.

Both tables are the textbook shape for a single pouch cell, not this pack measured on this board. They are right to a few percent in the middle and worst at the ends, which is where a pilot cares. What replaces them is a logged discharge of a fitted unit under the real dwell map, which the power budget needs anyway: the shape of the table does not change, only its points. `battery.h` carries the same warning where it explains why there is no time-remaining estimate, and that estimate stays refused until the same log exists.

## The trim, and the reference the device can find

`kCalibrationLimitMv` and `settings.battery_offset_mv` exist because two 1% resistors and the SAADC's own gain error can put a reading out by more than 80 mV, which is tens of percentage points in the flat middle. The bench answer is one number measured against a supply on the line.

`trim.h` is the answer for units that never see that bench. While a charger is in its constant-voltage phase it holds the terminal at its float voltage whatever the cell underneath is doing, so a reading taken there has a known true value, and the difference is the unit's error. Three conditions make the reading trustworthy:

The cable is in, so something is holding the rail. The reading climbed into the plateau from under `kClimbCeilingMv` during this same cable session, which is what separates a charger in constant voltage from a topped-off pack drifting slowly downwards under its own load. And the reading then sat inside `kPlateauSpreadMv` for `kPlateauHoldMs`, because a plateau is the claim being made.

What that buys is bounded and worth saying: the learned trim is only as good as the charger's float tolerance, typically 1%, so it replaces an 80 mV unknown with a 40 mV one. It does not replace the bench measurement, it replaces nothing having been measured at all. A trim a person set by hand outranks it for good (`settings.battery_offset_manual`), and the learned value goes to flash through the same window and the same power rule as any other setting.

The device has to be switched on across a charge for any of this to happen, which is a bench, a desk, or a long sit in a window. A unit that is only ever charged while off keeps whatever trim it was given.

## What the glass says about a flat cell, and when it says it

A device that reaches `kFlatMv` in the air says so before the rails go: `FLAT BATTERY` under the mark, pushed by the shutdown the cutoff asked for. Nobody pressed anything, so the frame is the only thing that can tell a pilot what happened, and the alternatives they would otherwise pick between are a crash and a dead unit. The same shutdown withholds the wake pin (`button_wake_after`), so the press that follows is answered by the frame already on the glass and not by a boot the cell cannot pay for.

The other way a cell arrives empty is a winter on a shelf. That unit ran no shutdown and painted nothing, so it is the refused boot that names it: `refused_frame` pushes the same `FLAT BATTERY` for the press that gets no device, and `button_wake_after_refusal` withholds the button after it. Both roads end at the same glass and the same dead button.

The cable is the way out of both. On the cable the device is an ordinary switched-off one again, because VBUS wakes the SoC, the boot is refused, and the refusal re-arms the button - so the wordmark replaces the flat frame, and the wordmark is the whole instruction. Anything else is `Leave`: the glass already says the right thing, and a full refresh is seconds of panel rail off a cell with none to spare.

A device switched off while the cell needs charge, `Low` or `Critical`, wears a milder word, `CHARGE BATTERY`, and the cable takes it off the same way. It asks for the cable and names nothing that happened, so a press on a cell that has drained past the lockout since does not leave it there: that refusal pushes `FLAT BATTERY` over it.

What the panel wears has to outlive the rails for that comparison to exist, so it does: `ports::SystemPower::cell_on_glass`, one of `None`, `Low` and `Flat`. A platform with nowhere to keep it answers `None`, which costs a repeated frame and nothing else.

The glass forgets on the cable, and a support case must not. A second bit, `went_dark_flat`, is set wherever the cutoff or a refused boot parks the flat frame, whether or not the panel could draw it, and it survives the refusal that takes the word off the glass. The boot that runs reads it, prints `WAS FLAT` beside the cell's voltage on the self-test page and answers `went_dark_flat` in the status reply, then drops it, so it names the one boot that followed the flat cell and no other.

The cable leaving cannot be noticed. VBUS rising wakes this SoC and VBUS falling does not, so a device unplugged still flat keeps the wordmark until the next press, which is the moment a pilot asks the question anyway - and that press is a refusal, so it is answered with the flat frame.

## Charge mode, considered and refused

A charger plugged into a switched-off unit wakes it, and `boot_path` sends that boot straight back to SYSTEM OFF. SoftRF-moshe-braner does something else there (`src/platform/nRF52.cpp:2512-2560`): it beeps the battery level out of the buzzer, then sleeps. That answers the question a pilot asks, "is it charging?", and it is not done here, for three reasons.

What the device says out loud is one table, `core/indication/lamp.h`, and charge is deliberately not a row of it: this board's charger IC drives its own LED, which already answers the question. A buzzer ladder in the wake path would be a second vocabulary beside that table.

What it would beep is not yet trustworthy. The gauge medians three samples before it states a percentage (`battery.h`) and the trim is a setting, so an honest level exists most of a boot later, on every cable wiggle in a flight bag, which is the case the refusal exists for.

The refusal is complete without it. A charge mode would be a third `BootPath` value beside the other two, with the rule and the sleep path untouched.

## Time in state, because there is no current

`duty.h` is one accumulator, `OnTime`: a thing was on, and this is how many milliseconds of it have gone by. It is what the backlight, the annunciator and the companion link are counted with, each by the service that drives that consumer and each published on `bus::State::duty` (`../bus/README.md`).

It takes the span between two observations rather than a pass at its nominal length, for the reason the air-time ring gives: a pass that ran long has to carry its own length, and an unsigned difference is the only arithmetic that survives the 49.7-day wrap of `ports::Clock::millis()`. A state that flips between two observations is credited to the state that was latched, so the error is one pass and it does not accumulate.

What these buy is the second half of a budget nothing else on this board can supply. Milliamps per consumer come from a datasheet or a bench; how long each was on comes from here, and multiplying the two is the only power figure this device can produce.

## What is still unmeasured

No current is sensed anywhere on this board, so nothing here can count coulombs, and no figure in minutes may be published (`battery.h`). The sleep current the shutdown sequence leaves has never been measured either (`shutdown.h`). Both are the same bench day, and until it happens every number in this directory is a voltage or a decision about one.

What the device can do without a current sense is say how long each consumer was on, which is what the `duty` records carry and what `POWER RUN` on the capture page arms. A run from full to cutoff, offloaded and read by `scripts/power_budget.py`, gives the average draw off the pack's own capacity and needs none of the curves above to do it. The same file says which consumer that draw went to, against a table of per-part currents that is still mostly datasheet typicals. `scripts/README.md` says how to take one, and the residual the tool prints is the honest size of what remains unmeasured.
