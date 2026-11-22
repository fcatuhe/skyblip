# core/bus

`bus.h` holds what happened. Each queue has one producer and one consumer, and its owner drains it to exhaustion. An event nobody popped is lost and counted in `Queue::dropped`. A queue holds a power of two of slots and fills every one: its head and tail are free-running 16-bit counters, masked into the slots, so it needs no spare slot to tell full from empty. `bus::Bus` holds the queues both products drain, and `bus::GoBus` adds the Go's radio, central and IMU queues, so Bravo holds no slot for parts it does not have. Producers push straight onto their queue and wake the loop.

`state.h` holds what is true now: a blackboard every service can read, with no history. Each field has exactly one writer. The compiler does not enforce it, so the table below does, and a second writer in it is a bug. A group holds one subject, so it may list a writer for each of its parts.

## Who writes what

| Field | Writer |
|---|---|
| `own`, `flight`, `barometer`, `slip`, `indicated`, `gload` | `ownship` |
| `clock` | `ownship` for UTC and the PPS edge, the board for `pps_locked` and `since_edge_ms` |
| `traffic`, `callsigns` | `traffic` on the Go, and `bravo` on a paired Go, `adsb` on Bravo, which runs no `traffic` service |
| `radio_log` | `traffic` for what was received, `radio` for what was sent |
| `rf` | `radio`, except `rf.timing_statistics`, whose PPS half is the board's |
| `air` | `traffic` |
| `power` | `power` |
| `gnss` | `ownship` for the stage, the fix mode and the solution phase, `screen` for `levels_wanted`, the board for the sky view, `levels_live`, `reject` and `rejected` |
| `imu` | the board, which owns the sensor hub |
| `formation`, `alarm_level`, `alarm_undismissed`, `alarm_call` | `alarm` |
| `capture` | `capture`, the diagnostics writer |
| `duty` | `screen` for the panel counts and the backlight, `radio` for the receiver and the transmitter, `alarm` for the annunciator, `config` for the BLE link |
| `link`, `activation` | `config` |
| `bravo` | `bravo` |
| `own_transponder` | `bravo` on a paired Go, `adsb` on Bravo: the transponder Bravo names on the ground. On the Go an aircraft address typed or `set` must match it (`../settings/README.md`, The ICAO address) |
| `aircraft_transponder` | `bravo` on a paired Go: the transponder confirmed at the entered address. Never written on Bravo |
| `followed_call` | `peer` on Bravo, while it plays its Go's call. Never written on the Go |
| `panel_presented` | `screen` |
| `started` | the product |

The board writes part of `clock`, `gnss` and `rf.timing_statistics` because only the code holding the part can say when the PPS edge arrived or why the receiver refused a solution. On a paired Go, `bravo` writes `traffic` and `callsigns` through the table's own `update` and `learn`, in the same loop as `traffic`. The `bravo` field is what the Go makes of its Bravo's health (`../comms/README.md`, What the Go makes of its Bravo).

`rf` carries the `TxDone` instants because `traffic` is the single reader of `bus.rf`, and `radio` must not open a second drain of the same queue.

## The duty counters

No current is sensed on this board (`../power/README.md`), so the cost of a flight is terminal voltage against time in each state. This group is the time-in-state half.

| Field | Counts |
|---|---|
| `panel_partial_refreshes` | frames asked for on the partial waveform, about 460 ms each, including the black wipe of a page swap, an install or a recovery |
| `panel_full_refreshes` | frames asked for on the full waveform, about 2.5 s each |
| `backlight_ms` | time lit |
| `rx_armed_ms` | time inside an armed receive dwell, the overlap of each pass with the executor's window |
| `tx_keyed_ms` | nominal air time of each burst reported sent, from the `timing::AirTime` the hourly budget is spent from |
| `annunciator_ms` | time the buzzer or the haptic motor ran |
| `ble_connected_ms` | time at least one central was connected |

Never sum the two panel counts. A full refresh costs about five partials. Both count what was asked for: the SSD1681 driver promotes a partial to a full after `begin()`, a busy timeout or an aborted refresh (`../../hardware/parts/ssd1681/ssd1681.cpp`), and `ports::Display::present()` does not report it.

The spans are elapsed time, not passes at a nominal length, so a long pass carries its own length. `power::OnTime` does the arithmetic for all but the radio, which follows the executor's window because a dwell edge falls anywhere inside a pass. `tx_keyed_ms` is nominal because no pass can watch a 5 ms burst end, and it must agree with the hourly allowance. `rx_armed_ms` is not read off the slot map's 989 ms (`../timing/slot.h`), because a missed dwell, a refused plan or a missing radio each read lower. There is no GNSS counter: the receiver is on whenever the device is.

Only the service loop counts. The self-test page of a unit that refuses to fly is painted outside it.
