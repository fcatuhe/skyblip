# core/model

The nouns of the air, and nothing else: what an aircraft is, what own ship is, which half of the second a burst arrived in. No behaviour, no policy, no I/O.

These types are what the tree agrees on. A protocol decodes into one, a table stores one, a page draws one, a link publishes one, and none of the four owns the definition.

| File | Holds |
|---|---|
| `aircraft.h` | `AircraftObservation`, one aircraft as one reception saw it, and `Source`, which of the four ways it reached us |
| `ownship.h` | `Ownship`, this aircraft as the receiver and the barometer currently describe it |
| `band.h` | `Band`, the M and O halves the single radio is shared between |

`AircraftObservation` carries its `events::Stamp` rather than a pair of loose time fields, so the instant a position was heard is the same shape everywhere in the tree. `at_ms` beside it is a different fact: the instant the position was *true*, on `ports::Clock`, which is what the relative geometry aligns on.

The quantity suffixes (`lat_1e7`, `speed_q`, `track_c9`) are the tree's convention and are listed in `firmware/README.md`. `core/units/` carries the same units as types, for the code that converts rather than transports.
