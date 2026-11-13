# core/indication

The one lamp on this device, and what it is saying. `lamp.h` holds the whole vocabulary as one table, in priority order, and `condition_for()` is that same order written as code.

E-paper holds its last image with the rails down, which is why the wordmark is painted before power off: an off device and a running device look identical, and the panel says nothing at all about whether there is a device there. This is what does.

## Why this is not core/annunciation

Both answer "what is the device telling the pilot", so the case for merging them is real. It loses on four counts.

Annunciation announces EVENTS and every pattern it plays ends by itself, which is the bug that file exists to not have. This announces a LEVEL, it never ends, and the whole design question is what it costs to hold.

The inputs do not overlap. Annunciation's situation is a traffic level, whether it just got worse, and the first fix. This one's is the cell as the cutoff monitor reads it, the fix, and the alarm.

`settings.alarm_enabled` silences the buzzer. It must not darken the lamp: "is this thing on" is not a preference, and a pilot who turned the noise off did not ask to be unable to tell a live device from a dead one.

Arbitration is the opposite shape. Annunciation hands one voice to another and has to decide who may interrupt whom. Here every condition is true or not true at the same instant and exactly one wins, which is a priority order over a closed set: `kTable`, top row first.

So: two files, and this one depends on the other's output (the alarm level) and on nothing else of it.

## What a row may spend

Every row declares whose budget it spends, and a static_assert refuses a row that exceeds it. `Steady` is held for hours on an 850 mAh pack and has to be very nearly free (2%). `Transient` lasts minutes at most, an alarm that stands or a cell about to go (30%). Nothing is held solid: a lamp left on is a real power term, and `every_lit_row_blinks()` is what keeps it that way.

SoftRF is the reference vocabulary and we keep its distinction and not its duty: solid above the low threshold, a 300 ms toggle below it (`src/driver/LED.cpp:204-219`). Healthy is a wink at 1%, and low keeps SoftRF's rate, a 600 ms period, at a tenth of its duty.

## Two red rows, one for each step of the ladder

`Low` is the cell past the knee, `core/power`'s `PowerLevel::Low` under `kLowMv` (`core/power/README.md`). It is red at the alive cadence: one wink every 3 s, the same 1% the green and blue rows spend, because unlike `Critical` it can stand for an hour and a row held that long has to be free. Red says which kind of news it is, the cadence says how urgent, and `Critical`, which `Flat` shares on its way down, keeps the 600 ms blink for the step where a pilot is meant to act. `condition_for` switches on the level with no default, so a level added to the ladder does not build until the lamp says what it looks like.

Blue was the obvious colour for a first battery step and it is the wrong one. It already means no fix, the lamp shows one thing at a time, and a colour with two meanings is a colour with none. The step is also a voltage and not a percentage: the percentage comes off a textbook curve (`core/power/README.md`), so keying the lamp to it would make the lamp exactly as trustworthy as a table nobody has measured yet.

## Six rows for a device with no glass

`Prompt`, `Pairing`, `NoFixLinked`, `AliveLinked`, `NoFixCapturing` and `AliveCapturing` are lit only for a product that claims `common::Feature::LampShowsLink`, skyBlip Bravo, whose lamp is the only thing it can show a phone's prompt, a Go's pairing request, a connection or a running capture on. The Go makes no claim, so `Situation::prompting`, `Situation::pairing`, `Situation::linked` and `Situation::capturing` stay false there and its lamp is the one above.

`Prompt` is a fast green blink, 60 ms in every 250, a `Transient` row because a prompt expires in thirty seconds. It sits below `Alarm`, so an advisory keeps the lamp, and above the cell, because a pilot answering a phone is standing beside the unit. Bravo arms the two-press gesture only while this row is showing (`products/skyblip_bravo/README.md`).

`Pairing` is a fast blue blink, the prompt's cadence in the colour of no fix, a `Transient` row because a request expires in thirty seconds. Blue at the steady wink means no fix, and blue at the fast blink means a Go asks to pair: the rhythm tells them apart, as it tells a prompt from a green wink. It sits below `Prompt`, so a phone's prompt keeps the press, and above the cell, because a pilot pairing is standing beside the unit. Bravo arms its one-press pairing gesture only while this row is showing.

A connected phone keeps the fix colour and doubles the wink rate: one wink every 1.5 s instead of 3 s, which is 2% and the edge of the `Steady` budget. The colour still says the fix and the rhythm says the link, so the lamp shows one thing at a time and a pilot reads both.

An armed capture keeps the fix colour too, and winks twice every 3 s: two 30 ms flashes 150 ms apart, then dark until the cycle ends. That is 2%, the connected rows' spend, and a capture runs for hours, so it is a `Steady` row. A row's `flashes` and `between_ms` say how many flashes a cycle holds and how far apart, and `cycle_ms()` and `duty_permille()` count them all. The capture rows sit above the connected ones: a pilot arms a capture from a page, so a phone is connected at the moment the lamp has to say the capture runs (#247).

## Every flash is on the UTC minute's grid

Every row's flash starts on second 00 of the UTC minute, on every unit, paired or alone. A Go and its Bravo sit side by side in one cockpit, and two lamps winking at unrelated offsets read as two devices disagreeing.

`Policy::update()` takes the `timing::ClockState` with the situation, so no caller can light the lamp without the clock. It reads the UTC millisecond from `timing::utc_ms()`, which answers only while the PPS is locked and the clock holds a UTC second. Its place in the cycle is then the millisecond into the UTC minute, modulo the row's cycle. A `static_assert` holds every row's cycle to a divisor of the minute, so every cycle restarts at second 00 and no flash drifts across the minute. That is why the advisory flicker is 50 ms in every 200 and not 45 in 180: 180 ms does not divide a minute.

Every unit with a lock lights the same flash in the same pass, whichever moment its condition began.

A change still shows itself at once, on the unit's own clock, because the flash a pilot is owed is the one that follows the event. The policy joins the grid at the first pass where its own phase and the grid's are both dark, so neither the change's flash nor the grid's first flash is cut short. A lost lock keeps the phase the grid left, and the lamp runs free on it until the lock comes back.

The buzzer and the motor stay off the grid. They announce an event, and waiting for the next grid line would delay a traffic call by up to a cycle.

## The signs in the hand

The table says whether the device runs. It cannot say that the device has just come on or is going off: the alive row winks once every 3 s, so a unit that has just gone dark looks like one between two winks for up to 3 s. `signature.h` holds the two signs the lamp and the motor make together. Each pulse is lit and felt at once, and neither sign sounds.

| Sign | Lamp and motor | When |
|---|---|---|
| on | one green pulse of 400 ms | the loop's first pass, after a press woke the unit from SYSTEM OFF |
| off | three red pulses of 150 ms, 150 ms apart | the first pass of a shutdown that leaves the unit off |

One long pulse for on is the cue phones and watches give at power-on. It is easier to catch than three short ones when the thumb has already left the button. The three short pulses for off come under the thumb that holds the button, so they read as "done, let go". Traffic is felt as two calls, and `common::AlarmService` asserts that neither sign has two pulses.

The sign on waits for the loop's first pass, so it costs bring-up nothing, and it says that the device runs, not that the button went down. A restart signs nothing: a watchdog, a software reset or an install can come in flight, where a pulse in a pocket would read as traffic. A first power-on signs nothing either. A refused boot signs nothing, because it runs on a cell too low to spend on it.

The sign off plays inside `power::kParkSettleMs`, the wait every shutdown already spends parking, and `signature.h` asserts that it fits, so no switch-off lasts longer. The product parks the lamp, the buzzer and the motor once the sign ends. A sign stays off the UTC grid, as the buzzer does, because it answers a press or a shutdown at once. An install and a recovery that reboots come straight back, so they go down unsigned (`power::ends_switched_off`).

A sign is not an alarm. `settings.alarm_enabled` and a volume of off silence the buzzer and the motor for traffic, but a pilot who turned the noise off still needs to know that the unit came on. A sign is an event that ends by itself, which the section above calls annunciation's kind. It lives here because it makes no sound and answers this lamp's own question, whether the device is on. A traffic call outranks a sign on and cancels it, though a sign on ends long before a first fix can bring traffic.

## Why charge is not a row

The board already answers it in hardware. The charger IC drives its own LED, documented in LilyGO's T-Echo pin table: lit while charging, blinking with the cell missing or faulty, off when full. It reports while the SoC is asleep, which nothing here can do, and it reads the charger rather than guessing from a terminal voltage.

Until 2026-09-18 this table had `Charging` and `Charged` rows, held solid for as long as a cable was in. They cost the thing the lamp exists for: on a powered install the only indicator of a running device and a valid fix was replaced by a second opinion about the charger. `Charged` was worse than redundant, since it came from a float-voltage threshold (`core/power/battery.h`) and could contradict the LED beside it.

Their priority rule was already dead code. `power::CutoffMonitor::apply` forces `PowerLevel::Normal` whenever external power is present, because on the cable the divider reads the USB rail and not the cell, so "charging outranks low" arbitrated a conflict that cannot occur. A cable in is never `Low` or `Critical`, and that is decided in `core/power`, once.
