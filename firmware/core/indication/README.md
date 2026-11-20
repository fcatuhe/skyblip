# core/indication

The lamp says whether the device runs, since e-paper holds its last image with the rails down and cannot tell a live device from an off one. `lamp.h` holds every lamp state in one table, `kTable`, in priority order, and `condition_for()` is that order written as code.

## Why this is not core/annunciation

Annunciation plays events that end by themselves and hands one voice to another. The lamp shows a level that never ends, and the top matching row of `kTable` wins. The inputs differ too: annunciation reads the traffic level and the first fix, the lamp reads the cell, the fix and the alarm level. An alarm setting of off or visual silences the buzzer but never darkens the lamp, because a pilot who turned the noise off still needs to tell a live device from a dead one. The lamp depends on annunciation's alarm level and on nothing else of it.

## What a row may spend

Every row declares a budget, and a static_assert refuses a row that exceeds it. `Steady` rows are held for hours on an 850 mAh pack and may spend 2% duty. `Transient` rows last minutes, an alarm that stands or a cell about to go, and may spend 30%. `every_lit_row_blinks()` refuses a lamp held solid.

SoftRF is the reference: solid above its low threshold and a 300 ms toggle below it (`src/driver/LED.cpp:204-219`). We keep the distinction, not the duty. Healthy is a 1% wink, and `Critical` keeps SoftRF's 600 ms period with a 60 ms flash.

## Two red rows, one for each step of the ladder

`Low` is `core/power`'s `PowerLevel::Low`, the cell under `kLowMv` (`core/power/README.md`). It winks red once every 3 s at 1%, because it can stand for an hour. `Critical`, which `Flat` shares, blinks every 600 ms, for the step where the pilot must act. `condition_for` switches on the level with no default, so a new level does not build until the lamp has a look for it.

Blue already means no fix, so low battery is red. The step is a voltage, not a percentage, because the percentage comes off an unmeasured textbook curve.

## Six rows for a device with no glass

`Prompt`, `Pairing`, `NoFixLinked` and `AliveLinked` light only on a product that hands `common::AlarmService` `lamp_shows_link`, skyBlip Bravo, whose lamp is its only way to show a phone's prompt, a Go's pairing request or a connection. `NoFixCapturing` and `AliveCapturing` light on every product, since a page arms a capture on a Go too.

| Row | Look | Budget | Placed |
|---|---|---|---|
| `Prompt` | green, 60 ms in every 250 | `Transient`, a prompt expires in 30 s | below `Alarm`, above the cell, since the pilot is beside the unit |
| `Pairing` | blue, 60 ms in every 250 | `Transient`, a request expires in 30 s | below `Prompt`, above the cell |
| `NoFixCapturing`, `AliveCapturing` | two 30 ms flashes 150 ms apart every 3 s, fix colour | `Steady`, 2% | above the linked rows, since a phone is connected when a capture is armed (#247) |
| `NoFixLinked`, `AliveLinked` | one 30 ms wink every 1.5 s, fix colour | `Steady`, 2% | above the plain fix rows |

The colour says the fix and the rhythm says the rest. Bravo arms its two-press gesture only while `Prompt` shows and its pairing press only while `Pairing` shows (`products/skyblip_bravo/README.md`).

## Every flash is on the UTC minute's grid

Every row's flash starts on second 00 of the UTC minute, on every unit, because a Go and its Bravo side by side winking at unrelated offsets read as two devices disagreeing.

`Policy::update()` takes the `timing::ClockState`, and `timing::utc_ms()` answers only while the PPS is locked. The place in the cycle is the millisecond into the UTC minute, modulo the row's cycle. A `static_assert` holds every cycle to a divisor of the minute, which is why the advisory flicker is 50 ms in every 200 and not 45 in 180.

A change shows at once on the unit's own clock, because the pilot is owed the flash that follows the event. The policy joins the grid at the first pass where both phases are dark, so no flash is cut short. A lost lock keeps the last phase.

The buzzer, the motor and the signs stay off the grid, since waiting for a grid line would delay them by up to a cycle.

## The signs in the hand

`sign.h` holds two signs the lamp and the motor make together, lit and felt at once and never sounded, because a wink every 3 s cannot say that the device came on or is going off.

| Sign | Lamp and motor | When |
|---|---|---|
| on | one green pulse of 400 ms | the loop's first pass, after a press woke the unit from SYSTEM OFF |
| off | three red pulses of 150 ms, 150 ms apart, after 150 ms dark | the pass after a shutdown that leaves the unit off starts |

One long pulse is easy to catch after the thumb has left the button. Three short pulses under a held button read as "done, let go". Traffic is felt as two calls, and `common::AlarmService` asserts that neither sign has two pulses.

A restart signs nothing, because it can come in flight, where a pulse would read as traffic. A first power-on signs nothing, and a refused boot has too little cell to spend on one.

The sign off starts on the pass after the shutdown's, because that pass flushes the settings and drains a capture, and a stall would merge two pulses. Its leading gap keeps a cut traffic call from running into the first pulse. It plays inside `power::kParkSettleMs`, as `sign.h` asserts. The product parks the lamp, the buzzer and the motor once it ends. An install and a recovery that reboots go down unsigned (`power::ends_switched_off`).

An alarm setting of off or visual does not silence a sign, since the pilot still needs to know the unit came on. A traffic call cancels a sign on.

## Why charge is not a row

The charger IC drives its own LED, per LilyGO's T-Echo pin table: lit while charging, blinking with the cell missing or faulty, off when full. It works while the SoC sleeps, and a charge row would hide the running and fix rows on every powered install. `power::CutoffMonitor::apply` forces `PowerLevel::Normal` on external power, because the divider then reads the USB rail, so a cable in is never `Low` or `Critical`.
