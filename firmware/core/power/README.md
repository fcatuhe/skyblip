# core/power

What a terminal voltage means, what the device does about it, and the order it goes dark in. `battery.*` is the gauge a pilot reads, `cutoff.*` the rule that acts, `trim.*` the one calibration the device can perform on itself, `charging.*` the temperature window, `wake.*` whether a boot becomes a device, `shutdown.*` the road out, `reset_reason.*` what the last one was.

The cell is a 4.2 V LiPo pouch: 2400 mAh on this board, and LilyGO fits the same footprint with an 850 mAh pack on the plain T-Echo. Nothing below changes between them. A pouch cell's voltage says what fraction of the charge is left; only its capacity says how long each fraction lasts, and capacity is the thing no reading here can see.

## The ladder

Every other number about the cell hangs off these four, so they are declared together in `cutoff.h` and a `static_assert` holds their order.

| Step | mV | What it is for |
|---|---|---|
| float | 4200 | what a charger holds a full pouch at, and `kFullMv`, the gauge's 100% |
| caution | 3600 | the knee: above it the cell spends 100 mV crossing ten points of charge, below it the same 100 mV costs twenty |
| warn | 3500 | land. The panel says `LOW`, the lamp blinks every 600 ms, and no durable write but the flight record is allowed |
| boot lockout | 3400 | a device this flat does not start a flight it cannot finish (`wake.h`) |
| cutoff | 3200 | the device takes itself down, and `kEmptyMv`, the gauge's 0% |

Three properties of that order are worth more than the numbers themselves.

The gauge reads zero where the device stops, not where a datasheet calls the cell empty. A percentage that runs out while the aircraft is still transmitting teaches a pilot to distrust the number, and `battery.cpp` asserts the curve's first point against `kCutoffMv` so the two cannot drift apart.

The lockout sits above the cutoff. A cell relaxes once the load goes away, so a device that shut itself down at 3200 reads about 3400 by the time a thumb reaches the button, and a lockout underneath the cutoff would hand back a device with one minute in it.

The lockout guards a switch-on, not a restart. A watchdog, lockup or software reset means the device was running a moment ago, possibly in flight, so that boot is held to the cutoff the running device answers to, and a fault never leaves a flying unit dark with a cell the cutoff would have kept on air (`wake.cpp`).

The cutoff is a loaded reading, because a loaded reading is the only kind this device ever takes. A pouch datasheet puts the discharge floor at 3.0 V and its protection board trips near 2.5 V; stopping at 3.2 V under a receiver and a 14 dBm radio leaves the pack resting near 3.4 V, which is a voltage it can sit at in a flight bag for months without reaching either.

Caution is not a `PowerLevel`, and that is deliberate. The levels are what the device acts on: a write refused, a refresh withheld, a shutdown started. Nothing acts on the knee. It is what the lamp says while every action is still allowed, so it stays a predicate on the monitor rather than a fifth value every reader of the level would have to learn to ignore. It also keeps the diagnostics record's level byte meaning what it meant in every log already on a flash.

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

A device that reaches `kCutoffMv` in the air says so before the rails go: `FLAT BATTERY` under the mark, pushed by the shutdown the cutoff asked for. Nobody pressed anything, so the frame is the only thing that can tell a pilot what happened, and the alternatives they would otherwise pick between are a crash and a dead unit. The same shutdown withholds the wake pin (`button_wake_after`), so the press that follows is answered by the frame already on the glass and not by a boot the cell cannot pay for.

The other way a cell arrives empty is a winter on a shelf. That unit ran no shutdown and painted nothing, so it is the refused boot that names it: `refused_frame` pushes the same `FLAT BATTERY` for the press that gets no device, and `button_wake_after_refusal` withholds the button after it. Both roads end at the same glass and the same dead button.

The cable is the way out of both. On the cable the device is an ordinary switched-off one again, because VBUS wakes the SoC, the boot is refused, and the refusal re-arms the button - so the wordmark replaces the flat frame, and the wordmark is the whole instruction. Anything else is `Leave`: the glass already says the right thing, and a full refresh is seconds of panel rail off a cell with none to spare.

What the panel wears has to outlive the rails for that comparison to exist, so one bit does: `ports::SystemPower::flat_on_glass`. A platform with nowhere to keep it answers false, which costs a repeated frame and nothing else.

The cable leaving cannot be noticed. VBUS rising wakes this SoC and VBUS falling does not, so a device unplugged still flat keeps the wordmark until the next press, which is the moment a pilot asks the question anyway - and that press is a refusal, so it is answered with the flat frame.

## What is still unmeasured

No current is sensed anywhere on this board, so nothing here can count coulombs, and no figure in minutes may be published (`battery.h`). The sleep current the shutdown sequence leaves has never been measured either (`shutdown.h`). Both are the same bench day, and until it happens every number in this directory is a voltage or a decision about one.
