# core/events

What happened, as a value small enough to cross a queue. One header per producer, because the producer is what decides the shape.

| File | Raised by | Drained by |
|---|---|---|
| `rf.h` | the RF executor | `traffic` |
| `adsb.h` | the 1090 MHz receiver's executor | Bravo's ADS-B service (#176) |
| `link.h` | the BLE adapter | `config`, and `log_rx` by `flight_log` |
| `central.h` | the Go's BLE central, off its link to a paired Bravo, with Bravo's frames on `peer_rx` | `bravo` |
| `sensor.h` | the board, off the barometer and the divider | `ownship`, `power` |
| `input.h` | the board, off the button and the pad | `screen` |
| `stamp.h` | nobody: it is how the others say *when* | everybody |

`Stamp` is a UTC second, the milliseconds into it, and whether the phase is trustworthy at all. It is derived from the PPS edge at the point of use, never sampled once a pass, because a pass takes milliseconds and the phase is what a slot is measured in.

An event that carries an instant carries a `Stamp`. An event that does not need one does not get one: `ContactEvent` is answered on the pass it arrives on, and a field nobody reads is a field that goes stale silently. What is forbidden is a second encoding of the same instant, which is what `rx_utc` and `rx_ms` were on `AircraftObservation` until they became the stamp the dwell already knew.

The raw `uint64_t at_us` on `RfEvent` and `AdsbFrame` is not a second encoding: it is the executor's own clock reading, the input `stamp_of` turns into a `Stamp` once the PPS edge is known.

`AdsbFrame` is 40 bytes where `RfEvent` is about 300: 27 chip bytes, the level of their pulses and when the executor woke for them. A busy sky hands up hundreds a second, so the queue is deeper than a dwell's. `kAdsbQueueFrames` is sized for the average load: the 100 aircraft of DO-260B Table 2-105 class A0 at the 6.2 squitters a second of DO-260B 2.2.3.3.1.3, over a 100 ms drain window, is 62 frames. 2.2.3.3.1.3 averages its 6.2 over any 60 s, so it bounds no burst, and the drain window is chosen, not yet measured. `bus::Queue` counts what it drops, and the receiver reports that count as `dropped`, so the bench reads whether 62 is enough.
