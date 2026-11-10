# products/common

The services more than one product runs, and the vocabulary they read. Why they live here and not in `runtime/` is in [`../../README.md`](../../README.md). What a service is, and the order a product steps its services in, is in [`../skyblip_go/README.md`](../skyblip_go/README.md), "What a service is".

`services/` holds these services, in namespace `common`:

- `TrafficService` owns `state.traffic`: it drains `bus.rf`, decodes each burst and feeds the table and the callsigns.
- `AlarmService` owns everything a product says out loud or shows with a light: it grades each target, then drives the buzzer, the motor and the lamp through `core/annunciation` and `core/indication`.
- `NmeaService` is the single writer of `events::Endpoint::Nmea`: the FLARM sentences a phone's map draws, once a second.
- `FlightLogService` writes the flights ring: a takeoff opens a session and a landing closes it. It also drains `bus.log_rx` and answers the Log endpoint for both rings.
- `CaptureService` drains `diagnostics::Recorder` into the diagnostics ring while a capture is armed.

No file here includes `products/<product>/*`. What a service needs from a product reaches it at construction, like everything else a service needs.

## Claims

`features.h` lists the claims a shared service branches on, and the capability each claim needs of the board. Which claims a build makes is the product's `kFeatures`, in its own `features.h`. A service asks `supported(declared, capabilities)` in its `setup()`, so a claim the hardware cannot serve is off rather than broken.

A feature with no reader is a line of marketing inside a header. The list said `CompanionLink` for as long as the tree had a NMEA characteristic nothing ever wrote to, and nothing anywhere could tell. So a bit joins the list on the commit that reads it, and leaves it with its last reader.

## Settings

A product owns its settings struct, its blob and its link keys. A shared service reads the few fields it needs through a small struct the product fills at construction. `AlarmSettings` holds references to the product's alarm on/off and alarm volume, so a change from the link or the menu reaches the alarm on its next tick, as it did when the alarm read the Go's `Settings` directly.

`alarm_volume.h` is the volume's three steps. They are here because the shared alarm reads them, and the Go's `Settings` stores them. Their wire codes are the link registry's `alarm_volume` enum, which `test/core/test_link_mirrors.cpp` checks against the Go's `go::AlarmVolume`, the same type.

`CaptureSettings` is the one seam that is not a struct of references. A `diagnostics::Config` record carries nine settings fields, and some are worked out from a setting, like `metric` from the Go's `Units`. So the product implements `describe()`, and the capture calls it each time it records a `Config`, which then carries the values of that instant. The Go's is `../skyblip_go/capture_settings.h`.

## The partition, and the two logs on it

`services/record_pool.h` and `services/record_store.h` are the flash half of a log, and neither is a service: `RecordPool` is the partition - one `store::SectorAllocator`, one boot scan of the headers, one set of reply buffers, one drop counter for the link - and `RecordStore` is one ring on it: recovery, the prepared sector, append, the session index, the erase, and the answers to the Log endpoint. The product owns one pool and two stores, and hands them to the two services that bracket sessions differently.

`FlightLogService` is `flight::LogSession` and `flight::LogEvents` on top of the flights store: a takeoff opens, a landing closes, and the records come from own-ship, the cell and the traffic table. `CaptureService` is `diagnostics::Recorder` on top of the diagnostics store: the pilot arms, power off or a refused sector closes, and the records come from every tap in the tree.

### When flash may be touched

`RecordPool::book_window()` books every claim, erase, header and page program through the `FlashWindow` the product hands the pool, because only the product knows what a stalled pass costs it. The window answers true when `cost_ms` of work fits the pass that began at `pass_ms`, and books that work, so the next piece in the same pass starts after it. The Go's rule is in [`../skyblip_go/README.md`](../skyblip_go/README.md), "When flash may be touched".

Records go to the flash a page at a time: `RecordStore::run_slots()` is how many of the next records end inside the 256 B page the first one ends in, at most `kMaxRunSlots`, and `append()` writes them in one call. The driver programs every page a write touches, so a write is booked at one `kSlotWriteCostMs` per page, two when its first record straddles a page edge. A write that claims a sector carries one record, so a rotation's `Gap` lands where it did when every record was its own write. A second of a full capture, about fifteen records, is two writes instead of fifteen.

On top of the window, the capture writes at most `CaptureService::kDrainCeilingRecords` records in one pass, and a bulk erase at most `kEraseCeilingSectors` sectors.

Opening a session touches no flash at all: `begin_session()` names the session and the first `append()` claims the sector, so the erase and the header land in a window like everything else.

### How a capture ends

A capture session is named the way a flight is, by the UTC second it opened, and it ends in one of three ways. The pilot stops it, the device powers off, or the allocator refuses a sector because the flights floor blocks the claim. Whichever it is, the last record written is a `diagnostics::End`, and that record is the only thing that makes the session read `closed` - a diagnostics slot has no CRC, so a session without one is a session whose tail may be torn (`../../core/diagnostics/README.md`). The store keeps the last two slots of the frontier sector in hand: one for the `diagnostics::Gap` that names the records still queued when the partition refused, one for the `End`.

A write the part refuses is not the same thing as a sector the allocator refuses. The first is a storage fault: it is counted on the pool, published on `state.capture`, and the record stays in the RAM ring for the next pass. The second is the end of the capture, and it is written down.

A long capture rotates rather than stopping, so it can lose the sector it opened in. The allocator counts that at the moment it decides it, the capture turns it into a `Gap` plus a fresh `Boot` and `Config`, and the `list` reply marks the session `truncated` (`../../core/store/README.md`).

One service drains `bus.log_rx`, and it is the flight log, because the gates it applies are the flights gates: the link claim, the ground state, and the prompt a destructive erase takes. It routes by `request.store` and refuses by name - `no_diagnostics` for a store this build has no service for, `flights_only` for an erase aimed at the capture, whose ring the allocator recycles on its own. Erasing is per ring: the flights erase clears every sector that is not the capture's, one a pass, and the capture's sectors are left where they are.
