# core/events

What happened, as a value small enough to cross a queue. Each producer has its own header, because the producer decides the shape.

| File | Raised by | Drained by |
|---|---|---|
| `rf.h` | the RF executor | `traffic` |
| `adsb.h` | the 1090 MHz receiver's executor | Bravo's `AdsbService` (`products/skyblip_bravo/services/adsb.h`) |
| `link.h` | the BLE adapter | `config`, and `log_rx` by `flight_log` |
| `central.h` | the Go's BLE central, off its link to a paired Bravo, with Bravo's frames on `peer_rx` | `bravo` |
| `sensor.h` | the board, off the barometer and the divider | `ownship`, `power` |
| `input.h` | the board, off the button and the pad | `screen` |
| `stamp.h` | nobody: it is how the others say *when* | everybody |

`Stamp` is a UTC second, the milliseconds into it, and whether the phase is trustworthy. Derive it from the PPS edge at the point of use, never once per pass, because a pass takes milliseconds and a slot is measured in them.

An event that carries an instant carries a `Stamp`, and no second encoding of it. The raw `at_us` on `RfEvent` and `AdsbFrame` is the executor's clock reading, the input `stamp_of` turns into a `Stamp`. An event answered on the pass it arrives on, such as `ContactEvent`, carries no stamp.

A `LinkEvent` of type Down carries in `payload_bytes` the payload the link settled on.

`kAdsbLoadFrames` is the average load: the 100 aircraft of DO-260B Table 2-105 class A0, at the 6.2 squitters a second of DO-260B 2.2.3.3.1.3, over a 100 ms drain window, is 62 frames. That clause averages over 60 s, so it bounds no burst, and the drain window is not measured yet. `kAdsbQueueFrames` rounds it up to 64, because `bus::Queue` holds a power of two. The queue also carries every other downlink format the LR2021 hears and preamble noise. The receiver reports the queue's drop count as `dropped`, and the bench reads from it whether 64 is enough.
