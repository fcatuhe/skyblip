# products/common

The services more than one product runs, and the vocabulary they read. Why they live here and not in `runtime/` is in [`../../README.md`](../../README.md). What a service is, and the order a product steps its services in, is in [`../skyblip_go/README.md`](../skyblip_go/README.md), "What a service is".

`services/` holds three services today, in namespace `common`:

- `TrafficService` owns `state.traffic`: it drains `bus.rf`, decodes each burst and feeds the table and the callsigns.
- `AlarmService` owns everything a product says out loud or shows with a light: it grades each target, then drives the buzzer, the motor and the lamp through `core/annunciation` and `core/indication`.
- `NmeaService` is the single writer of `events::Endpoint::Nmea`: the FLARM sentences a phone's map draws, once a second.

No file here includes `products/<product>/*`. What a service needs from a product reaches it at construction, like everything else a service needs.

## Claims

`features.h` lists the claims a shared service branches on, and the capability each claim needs of the board. Which claims a build makes is the product's `kFeatures`, in its own `features.h`. A service asks `supported(declared, capabilities)` in its `setup()`, so a claim the hardware cannot serve is off rather than broken.

A feature with no reader is a line of marketing inside a header. The list said `CompanionLink` for as long as the tree had a NMEA characteristic nothing ever wrote to, and nothing anywhere could tell. So a bit joins the list on the commit that reads it, and leaves it with its last reader.

## Settings

A product owns its settings struct, its blob and its link keys. A shared service reads the few fields it needs through a small struct the product fills at construction. `AlarmSettings` holds references to the product's alarm on/off and alarm volume, so a change from the link or the menu reaches the alarm on its next tick, as it did when the alarm read the Go's `Settings` directly.

`alarm_volume.h` is the volume's three steps. They are here because the shared alarm reads them, and the Go's `Settings` stores them. Their wire codes are the link registry's `alarm_volume` enum, which `test/core/test_link_mirrors.cpp` checks against the Go's `go::AlarmVolume`, the same type.
