# core/bus

Two things travel between services, and they are not the same kind of thing.

`bus.h` is what **happened**: typed queues, one producer and one consumer each, drained to exhaustion by whoever owns them. An event that nobody popped is lost and counted (`Queue::dropped`), which is the honest outcome for a fact with an instant attached to it.

`state.h` is what is **true now**: a blackboard every service can read and exactly one may write. No queue, no history, no ordering. A reader gets the current answer or the default the field was born with.

The rule that makes the blackboard safe is one writer per field. It is not enforced by the compiler, so it is written down here, and a second writer appearing in this table is a bug report.

The groups are subjects, not owners. A reader wants every barometric fact in one place whoever wrote it, so `baro` holds the pressure the sensor gave and the subscale the pilot set, and the table below is what says those two came from different services.

## Who writes what

| Field | Writer |
|---|---|
| `settings` | `config` at boot (defaults, then the stored blob), `screen` when a pilot changes one in a menu. The flash write itself is `config`'s alone. |
| `own` | `ownship` |
| `clock` | `ownship` for UTC and the PPS edge, the board for `pps_locked` and `ms_since_pps` |
| `traffic` | `traffic` |
| `radio_log` | `traffic` for what was received, `radio` for what was sent |
| `rf` | `radio`, except `rf.timing_stats`, whose PPS half is the board's |
| `air` | `traffic` |
| `power` | `power` |
| `flight` | `ownship` |
| `gnss` | `ownship` for the acquisition stage, the fix mode and the phase a solution landed at, `screen` for `levels_wanted`, the board for what only the receiver knows: the sky view, whether levels are live, and the `reject` verdict and `rejected` count behind the solution it just pushed |
| `baro` | `ownship` |
| `slip` | `ownship` |
| `imu` | the board, which owns the sensor hub the ball comes from and is the only code that can see where its bring-up stopped |
| `capture` | `capture`, the diagnostics writer, read by the page that arms it |
| `duty` | `screen` for the two panel counts and the backlight, `radio` for the armed receiver and the keyed transmitter, `alarm` for the annunciator, `config` for the BLE link |
| `alarm_level` | `alarm` |
| `panel_presented` | `screen` |
| `started` | the product |

Four entries have more than one writer and all four are deliberate. `clock`, `gnss` and `rf.timing_stats` are split between the board, which is the only code holding the part and therefore the only code that can say when the PPS edge arrived or why the receiver refused a solution, and the services that decide what to do with either. `radio_log` is one ring with two ends of the same conversation in it.

`duty` is the fifth, and it is four writers rather than two: the group is a subject like the others, and the subject is what the device spent. A reader of the power budget wants the panel, the backlight, the receiver, the annunciator and the phone in one place, and each of those facts is known only to the service that drives that consumer, so the split is the same one `baro` and `gnss` make.

`air.last_tx_done_at_us` looks misfiled and is not: `traffic` is the single reader of `bus.rf`, so it is the only code that sees the executor's `TxDone`, and `radio` reads the instant from here rather than opening a second drain of the same queue.

## The duty counters

No current is sensed anywhere on this board (`../power/README.md`), so what a flight costs can only be terminal voltage against time in state, and this group is the time-in-state half.

| Field | What it counts | How it is accumulated |
|---|---|---|
| `panel_partial_refreshes` | frames the screen service asked for on the partial waveform, about 460 ms each | one per `present()` asked for `Refresh::Partial`, plus the black wipe a page swap costs, which the SSD1681 also drives partial |
| `panel_full_refreshes` | frames asked for on the full waveform, about 2.5 s each | one per `present()` asked for `Refresh::Full`, which on this product is the frame the glass wears parked |
| `backlight_ms` | time lit | the span since the screen service last looked, credited while the lamp was on |
| `rx_armed_ms` | time the radio was in an armed receive dwell | the overlap of each pass with the window the executor was handed, taken in microseconds and published in milliseconds |
| `tx_keyed_ms` | time the transmitter was on air | the nominal length of each burst the executor reported sent, read off the `timing::AirTime` the hour's budget is already spent from |
| `annunciator_ms` | buzzer and haptic in one number | the span while the pattern held the tone, plus the stated length of each haptic pulse, which the driver owns and no service can watch end |
| `ble_connected_ms` | time at least one central was connected | the span since the config service last looked, credited while a session was up |

The two counts are never summed. A full refresh is worth about five partials, and which of the two a frame cost is the only thing a refresh policy can act on.

Both count what was asked for, not what the controller drove. The SSD1681 driver promotes a partial to a full whenever it no longer knows what the glass shows: the first frame after `begin()`, and the first after a busy timeout or an aborted refresh (`../../hardware/parts/ssd1681/ssd1681.cpp`). That is one full a boot booked as a partial, plus one per fault, and `ports::Display::present()` returns nothing a counter could read the difference from.

The four spans are elapsed time rather than passes counted at their nominal length, so a pass that ran long carries its own length, and a state that changed inside a pass is wrong by at most that pass instead of for ever. `power::OnTime` is the arithmetic and it is shared; the radio's is not, because a dwell boundary falls where it likes inside a pass and the executor's own window is what the counter has to follow.

`tx_keyed_ms` is the exception and is nominal: a burst is 5 ms of chips clocked out by the part against an absolute deadline, and no pass can watch it end. The figure is the one the band's hourly allowance is spent from, so a counter that disagreed with it would be a second answer to a question already decided.

That is also why `rx_armed_ms` cannot be taken from the slot map. The map offers 989 ms of every second (`../timing/slot.h`) and the radio gets that only while every dwell is armed on time: a pass that misses a dwell edge, a plan the executor refuses, and a board with no radio at all each read lower. There is no GNSS counter, because the receiver is on whenever the device is and uptime says so.

The counters are the flying device's. A unit that refuses to fly paints its self-test page outside the service loop, and nothing outside the loop counts.
