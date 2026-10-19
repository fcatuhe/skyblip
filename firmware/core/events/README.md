# core/events

What happened, as a value small enough to cross a queue. One header per producer, because the producer is what decides the shape.

| File | Raised by | Drained by |
|---|---|---|
| `rf.h` | the RF executor | `traffic` |
| `link.h` | the BLE adapter | `config`, and `log_rx` by `flight_log` |
| `sensor.h` | the board, off the barometer and the divider | `ownship`, `power` |
| `input.h` | the board, off the button and the pad | `screen` |
| `stamp.h` | nobody: it is how the others say *when* | everybody |

`Stamp` is a UTC second, the milliseconds into it, and whether the phase is trustworthy at all. It is derived from the PPS edge at the point of use, never sampled once a pass, because a pass takes milliseconds and the phase is what a slot is measured in.

An event that carries an instant carries a `Stamp`. An event that does not need one does not get one: `ButtonEvent` is answered on the pass it arrives on, and a field nobody reads is a field that goes stale silently. What is forbidden is a second encoding of the same instant, which is what `rx_utc` and `rx_ms` were on `AircraftObservation` until they became the stamp the dwell already knew.

The raw `uint64_t at_us` on `RfEvent` is not a second encoding: it is the executor's own clock reading, the input `stamp_of` turns into a `Stamp` once the PPS edge is known.
