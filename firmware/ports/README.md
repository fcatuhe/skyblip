# ports

A port is a role the core needs filled, declared in the core's own vocabulary. `hardware/` fills it with a part or a platform.

Ports wrap no chip, so they are not a hardware abstraction layer. `Rf::arm()` takes a dwell with receive and optional transmit sync words, a chip rate and the burst's planned and latest key instants. The latest instant can precede the end of the receive dwell, since ADS-L 4 SRD-860 §C.5 bounds the transmission to its slot. Keep register code and Zephyr headers in `hardware/parts/` and `hardware/platform/`. Nothing at or above this layer includes a framework, so the host suite and the WASM simulator build.

## What is a port and what is an event

A port is something the core calls: arm this dwell, send these bytes, paint this frame, read the die. What happens to the device, such as a received frame or a button press, reaches a service as an event on a queue declared in `core/events/`, so it needs no port. `hardware/README.md` lists what a platform owes the board for that half.

`AdsbReceiver` listens full time, so its frames arrive on a `bus::AdsbQueue` and the port keeps its counters and its sleep on the way to SYSTEM OFF. Its statistics carry a `ReceiverProbe`, what the receiving part said about itself at its last probe, in the part's own mode and reset codes. `set_detection_threshold()` takes the level a frame must clear. The adapter that owns the part's bus applies it, so on silicon it lands on the receiver thread's next wake. `Gnss` is a port because the core asks the receiver for a restart and reads its health.

`Link::send()` puts one frame on every subscribed central, because the traffic picture is everyone's, and answers `Unsubscribed` when the connected centrals all left the endpoint unsubscribed. So `payload_bytes()` with no session is the smallest of the connected centrals: a broadcast frame is formatted once and must fit the narrowest. `core/comms/README.md` has the rest.

`Link` keeps one connection for a paired peer whether or not the peer is connected, so a Go never reserves what it cannot keep (#178). Bravo keeps it for its paired Go's address (#192), and `phones()` leaves that Go out so Bravo's lamp shows a phone and not its own Go.

`Central` is the Go's side of that connection. A scan takes a `ScanDuty` because how much of the radio it takes is the product's decision, and a name prefix so the platform drops other advertisers before they reach a queue. `Central` is not a field of `Roles` because only the Go uses it: the board hands `central()` to the product, or `NullCentral` without Bluetooth.

`KeyValueStore` and `FlashRegion` share nothing but the word storage. The key/value store owns its layout on the 32 KB internal partition, where a write stalls the CPU. The flight log is an append-only stream on a 1.16 MB external partition, with its erase sectors exposed.

## The millisecond clock wraps

`Clock::milliseconds()` is a 32-bit counter that wraps every 49.7 days, and the device must fly through the wrap. Hold every deadline by these rules:

1. Measure an interval as the unsigned difference `now - then` and compare it with a duration. Modulo 2^32, it is exact across the wrap for any interval under 49.7 days.
2. Never compare two instants. `now < until` inverts once one operand wraps, and a two-second silence becomes seven weeks.
3. Never cast the difference to a signed type first.
4. Never use an instant as a sentinel. The counter passes through 0 at every wrap, so keep a bool beside the stamp.

The longest deadline is the rolling duty-cycle hour (`core/timing/channel.h`), so nothing keeps a 64-bit millisecond count. `microseconds()` is 64-bit and never wraps in the device's life, so `Rf` arms slot deadlines on it.

## Absent is a role, not a null

Every field of `Roles` is a reference, because a port that does nothing is simpler than a pointer every caller checks. `Indicator`, `DieTemperature` and `Gnss` are concrete, and the base class is the absent part. The other roles have a null in `null.h`.

`capabilities.h` states once what is missing: the self-test page prints the row, a service skips the work. A capability never changes after `roles()` is taken. `inventory.h` names which part the board found, because LilyGO fits different silicon to the same footprint. Code branches on capabilities. A bench reads the inventory.

`Identity` has no capability bit, because an activated unit always has a DEVICEID, a key and a place for its unlock code (`core/identity/README.md`). skyBlip Bravo is never activated and gets `KeylessIdentity`: its FICR identity, no key and no code store, so the activation commands answer `keyless`.

`Random` is its own role because a unit with no key still draws: the browser challenge nonce and the peer link's boot epoch (`core/comms/README.md`).

These capabilities carry a decision:

- `AdsbReceiver` and `Rf` are two bits because Bravo's LR2021 listens full time and the Go's SX1262 is armed per dwell.
- `HapticDriver` says a DRV2605 on I2C makes the pulse rather than a motor on a pin, because the two need different bring-up and a board fitted with the other passes its self-test and stays silent. On the T-Echo Plus, LilyGO's `vibro` pin, P0.08, is the DRV2605's enable.
- `Inclinometer` says a BHI260AP answered, not that it reads. The hub runs nothing until 101 KB of vendor firmware is pushed after boot, which can fail (`hardware/parts/bhi260/`), so the six-pack takes `inclinometer_fitted` from the capability and `lateral_valid` from a recent sample, and draws an empty cage in between.
- `Indicator` says a lamp exists, not which colours it makes. A board with one green LED lights nothing on the red rows, and `core/indication` still tells its states apart by rhythm. E-paper keeps its last image with the rails down, so the lamp is the only outside sign that the device runs.
- `DieTemperature` is the nRF52's own sensor, which OGN reads instead of the SX1262's (nrf52-ogn-tracker src/proc.cpp:1112-1114). It tells a support case whether the unit cooked on a canopy rail.

`indication::Colour` lives in `core/indication/colour.h`, because a colour is what a pilot reads. Only `boards/` and `hardware/` drive an LED.

## The annunciator has no cadence

`tone()` sounds until `silence()`, and `vibrate()` runs until `still()`. The port takes no duration, because a tune a driver plays is a tune nothing can test. Tunes and their release belong to the one service that drives `core/annunciation`. `Indicator::show()` takes a level for the same reason.

The one timer behind the port is a guard. An adapter cuts an output left on longer than `annunciation::kAnnunciatorOnCeilingMs`, because a loop that stalls mid-call would hold a tone or a motor on until the watchdog bites, and a running motor flattens the battery. The ceiling is well above the longest call, so a cut is a bug: the Zephyr adapter prints it and the host one counts it. On Zephyr the guards are delayed work items, because the DRV2605 is stopped over I2C and never from an interrupt. Turning an output off cancels its guard in sync, so no cut writes the part after the service lets it go. A loop stalled inside an I2C transfer also holds the guard's work queue, and the watchdog ends that.

`Loudness` is `Soft` or `Loud`, not a number: the core says how loud it wants to be heard and the platform says what that costs in duty cycle. Off is `silence()`.
