# ports

The roles a product needs filled, declared in the core's own vocabulary. `hardware/` is where a part or a platform fills one.

This is not a hardware abstraction layer, which is why it stopped being called one. A HAL wraps a chip: GPIO, SPI, timers. These types wrap nothing. `Rf::arm()` takes a dwell with a sync word, a chip rate and an instant the burst has to key at, because that is what ADS-L 4 SRD-860 §C.5 requires of a slot, not because an SX1262 has a register that shape. `Link` speaks in endpoints and sessions. `Annunciator` is asked for a pitch at a loudness, not for a PWM duty cycle. The vocabulary belongs to `core/`, and a port is where the core states what it needs.

Register code belongs in `hardware/parts/` and `hardware/platform/`. Zephyr headers belong there too: nothing above this line includes a framework, which is what buys the host suite and the WASM simulator.

## What is a port and what is an event

A port is something the core calls: arm this dwell, send these bytes, paint this frame, read the die now. The core decides when, so it needs a name for what it decides against.

A receiver, a barometer, a divider and a button are called by nobody. The world happens to them, the board polls them, and what reaches a service is an `events::BaroSample` or an `events::ButtonEvent` on a queue. The queue is already the interface, declared in `core/events/`, so a port in front of one would be an abstraction with no caller. `hardware/README.md` lists what a platform owes the board for that half.

`Link` is the port with two shapes of send, and the split is the same one: `send()` puts one frame on every subscribed central because the traffic picture is everyone's, `send_to()` answers the session that asked. `payload_bytes()` with no session is therefore the smallest central connected, since a broadcast frame is formatted once and has to fit the narrowest. `core/comms/README.md` has the rest, including which app may configure the device.

`Rf` shows the line: the core calls `arm()`, so it is a port, and the executor pushes `RfEvent`, so the reception is an event. One device, two directions, two mechanisms. `Gnss` is a port for one reason only - a cold start is a thing the core asks for - and everything else the receiver knows arrives as a solution on the bus, which is why the port has one method on it.

## Absent is a role, not a null

Every field of `Roles` is a reference. A board with no lamp, no die sensor and no radio still fills all eleven, because a port whose methods do nothing is a smaller thing than a pointer every caller has to check. Some ports are their own absent part: `Indicator`, `DieTemperature` and `Gnss` are concrete, and the base class is what a board without one is handed. The rest have a null in `null.h`, next to the role it fills rather than in the loop that steps them.

What is missing is stated once, in `capabilities.h`, and read by the code that has something to say about it: the self-test page prints the row, a service skips the work. Never by dereferencing.

`capabilities.h` is what the board found, `inventory.h` is which part it was. The first is what code branches on; the second is what a bench reads when two footprints ship with different silicon in them.

One word for the thing that shakes, and it is `haptic`. `Capability::Haptic` says a pulse can be made at all; `Capability::HapticDriver` says it is made by a DRV2605 on I2C rather than by a motor on a pin, because the two need different bring-up and a board wired for one and fitted with the other reports PASS and stays silent. `vibro` is LilyGO's and SoftRF's name for the pin, it named one of the two parts, and on the T-Echo Plus it named the wrong one: P0.08 there is a waveform driver's enable, not a motor. `ports::Inventory::haptic` carries the same fact for the bench; the capability is the half the code branches on.

`Capability::Inclinometer` says a BHI260AP answered the bus and named itself, and no more than that. It cannot say there is a reading: the hub is a processor with no program until 101 KB of vendor firmware has been pushed into it, which takes a few seconds of service passes after boot and can still fail (`hardware/parts/bhi260/`). So the six-pack takes two facts and not one - `inclinometer_fitted` from the capability, `lateral_valid` from a sample recent enough to draw - and prints an empty cage in between. A capability that waited for the reading would be a capability that changes after `roles()` is taken, which is the one thing a capability may never do.

`Capability::Indicator` says a lamp exists, not which colours it can make. A board fitted with one green LED reports the capability and lights nothing on the red rows, and that is survivable because `core/indication` tells its states apart by rhythm as well as by colour: the wink rate carries the reading, the colour confirms it. A lamp set in `capabilities.h` would be a bit nothing branches on today, so there is none until a board differs.

The colours themselves are `indication::Lamp`, declared in `core/indication/lamps.h` rather than here. A colour is what a pilot reads, so the vocabulary belongs to the layer that decides what the device is saying; this port is where it is asked for.

## The annunciator has no cadence

`tone()` opens a continuous tone and it runs until `silence()`: there is no cadence in the port, no timer behind it and no duration argument, because a pattern a driver plays is a pattern nothing can test. On times, off times, pitches, repeats and re-announcement are `core/annunciation`'s, driven by exactly one service, which is also the one that has to release the tone when the level falls, the target goes, the pilot switches alarms off or the device goes down.

`vibrate()` is the exception: it is a single pulse of a stated length and the implementation owns its end, because a motor left on is a flat battery. What the pulse reaches is `haptic.h`, a motor on a pin on one board and a DRV2605 over I2C on another, and which of those is fitted is a capability, not something any caller of this port ever sees.

`Loudness` is two words, `Soft` and `Loud`, and not a number: the core says how loud it wants to be heard and the platform says what that costs in duty cycle. There is no `Off`, because off is `silence()`.
