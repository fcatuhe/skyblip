# products/skyblip_go

One board, one service list, one set of pages. `product.h` wires it; the shell around it (`main.cpp` on silicon, the simulator on the host) only decides how often `step()` is called and where the pixels go.

`pages/` is the glass half of the product and carries its own README, `input/` is what a press means here. `glass.h` is where the panel this device draws on is named: the size comes from the board (`boards/lilygo/t_echo_plus/glass.h`), and `go::Glass` is the `ui::Panel` every page and the screen service is written against.

`settings.h` is the list of values a pilot of this device can change, its defaults, what it refuses and how an older flash layout migrates forward. `kBlobVersion` moves whenever the struct's bytes move, and `from_blob` still reads every layout before it: version 1 carried `region`, `rotation` and `power_save` between `aircraft_type` and `callsign`; version 2 dropped those three and had no `battery_offset_mv`; version 3 is the same length as version 2 and a different layout, which is exactly the case a length check cannot catch and the version byte must; version 4 added `freq_trim_e1_ppm`; version 5 dropped `page_mask`, which lost its last reader when the pad's walk became three pages and the two that are doors stopped being hideable (`pages/README.md`); version 6 added `gyro_enabled`; version 7 dropped `stealth` and `gyro_enabled` with the features themselves, and is the length version 3 was, which is the collision the version byte exists for; version 8 dropped `device_addr` and `addr_table`, because the identity is the silicon's and a stored copy of it is a copy that can disagree; version 9 added `battery_offset_manual`, so a trim a person measured is told apart from one the device learned off a charger (`core/power/README.md`), and the migration reads any stored version-8 trim as hand-set, because until then there was no other way to have one.

That is the rule the struct states and the reason it is versioned: a setting with no reader leaves the struct, the JSON and the schema together, rather than being stored and ignored. The framing under it is `core/settings/blob.h` and the identity rules are `core/settings/address.h`, because neither is this product's to decide. `settings_store.h` is the two calls `comms::ConfigService` needs to answer a phone, and it is the only thing that couples the companion link to this struct. It is also where the address reaches that reply: the store is handed `roles.device_addr` because the struct no longer carries one, so `addr` and `addr_table` are reported and nothing can set them. The settings do not live on `bus::State`: the blackboard is core's and a product's field list cannot sit on it, so the product owns them and hands a reference to the services that read them.

Migration only runs forward, and a layout cannot be read backwards: fields were removed and inserted mid-struct, so an older layout is not a prefix of a newer one. The image the bootloader reverts to meets exactly that, a blob its successor wrote. `ConfigLinkService::load()` tells a newer layout from a torn sector by the CRC over the whole stored length (`settings::sealed`), and keeps the newer one untouched: from then on this image reads and writes `settings.prior` instead, so the successor finds its blob again if it is installed a second time. `record_update()` writes the settings in use to `settings.prior` on the way into a swap, so a revert comes back up on the settings the pilot had, not on defaults. A torn sector falls back to defaults and is written over by the next change.

## What a service is

A service is one unit of work the loop steps, under the watchdog, in dataflow order. It owns one thing: a queue it drains, a slice of `bus::State` it writes, or an output port it drives. Never two of the same kind, and never a queue somebody else also drains.

That is why `nmea` is a service and not a helper, though it drains nothing and writes nothing: it drives `ports::Link` on a one-second cadence that has to defer around an armed burst, which needs its own place in the order and its own watchdog row. Something that consumes the finished product rather than being stepped by it is a reader, and `DiagnosticsDump` is the one the tree has.

Everything a service needs arrives at construction: the `runtime::Context` with the roles, the bus, the state and the diagnostics recorder, plus whatever else it is handed as a reference. There is no `attach_` after the fact and no member that starts null, because a service that can be half-built is a service every method has to check.

The recorder is in the Context rather than on either of the other two because it is neither kind of thing they hold: it has many producers and one consumer, where `core/bus`'s queues have one producer each, and the blackboard's one-writer rule forbids a field every service writes. A tap is one call behind `armed()`, and a disarmed device pays one branch for it (`../../core/diag/README.md`).

## The partition, and the two logs on it

`record_store.h` is the flash half of a log, and it is not a service: `RecordPool` is the partition - one `store::SectorAllocator`, one boot scan of the labels, one set of reply buffers, one drop counter for the link - and `RecordStore` is one ring on it: recovery, the prepared sector, append, the session index, the erase, and the answers to the Log endpoint. The product owns one pool and two stores, and hands them to the two services that bracket sessions differently.

`FlightLogService` is `flight::LogSession` on top of the flights store: a takeoff opens, a landing closes, and the records come from own-ship. `CaptureService` is `diag::Recorder` on top of the diagnostics store: the pilot arms, power off or a refused sector closes, and the records come from every tap in the tree.

### When flash may be touched

Every claim, erase, header and slot program goes through `RecordPool::window_open()`, which is `timing::DurableWriteWindow::free_now()` against the plan and the dwell view the radio publishes: inside a dwell that is already running, finishing a guard's width before it must be re-armed, and never where own-ship may key the PA. Standing off `tx_allowed` alone was not enough - it leaves the erase that opens a session, the erase of a prepared spare and the erase-all outside any window, and a pass that spends 200 ms on flash misses the next dwell edge whether or not the PA was keyed in it.

The budgets the window is asked for are `kSectorEraseCostMs` and `kSlotWriteCostMs` in `record_store.h`. They are budgets for the external NOR on spi1, not datasheet figures, and the bench is what settles them; a `static_assert` holds a claim (erase, header, first record) to the narrowest dwell the map offers. Work is accounted per pass rather than per operation, because the phase the radio published does not advance inside a pass: `RecordStore::room_in_window()` adds up what the pass has already spent and asks for the sum, so a drain, a spare erase and a bulk erase cannot each spend the same window. On top of that the capture writes at most `CaptureService::kDrainCeilingRecords` records in one pass, and a bulk erase at most `kEraseCeilingSectors` sectors.

Opening a session touches no flash at all: `begin_session()` names the session and the first `append()` claims the sector, so the erase and the header land in a window like everything else.

What that costs is a rate, and the rate follows the dwell map. Since own-ship's callsign took slot 1's tail (`core/timing/README.md`), the uplink dwell is the one stretch the second offers, so erasing the whole partition on a pilot's request takes around 45 seconds rather than the three it took when a sector went out on every pass. The page reports progress throughout, and the alternative was a pass that spends its time on flash and hands the radio a dwell it can no longer arm.

### Which capture is armed

A capture is `Full` or `PowerRun` (`../../core/diag/README.md`), and the glass is the only place either is chosen: nothing arms a capture over Bluetooth. The focus a pilot moves with the pad lives in `ScreenService`, the profile that is running lives in `diag::Recorder` and is read back from there, so a boot comes up armed with nothing and remembering nothing.

`CaptureService::keeps_s(profile)` is the span the page quotes before arming: the slots the claim buys over `diag::Recorder::records_per_hour(profile)`. The screen service holds the capture service by const reference for that one figure, because `bus::CaptureState` carries a single span and it belongs to the capture that is running, measured at the rate that capture is producing. Two captures at rest need two figures, and only the service that owns the partition knows how many slots a sector holds.

### How a capture ends

A capture session is named the way a flight is, by the UTC second it opened, and it ends in one of three ways. The pilot stops it, the device powers off, or the allocator refuses a sector because the flights floor blocks the claim. Whichever it is, the last record written is a `diag::End`, and that record is the only thing that makes the session read `closed` - a diagnostics slot has no CRC, so a session without one is a session whose tail may be torn (`../../core/diag/README.md`). The store keeps the last two slots of the frontier sector in hand: one for the `diag::Gap` that names the records still queued when the partition refused, one for the `End`.

Power off is the case that needs the rest of the product. The shutdown sequencer parks the capture after `rf.abort()` and `rf.sleep()`, and it is `Product::publish_radio_asleep()` that makes the drain possible: the plan and the dwell view the radio last published still said a burst was coming, so a drain that respects them would defer until the rails dropped. With nothing armed the park drains to exhaustion, then closes - never the other way round, because a session closed over records still in hand is a corpus missing its last second. Just before it, `PowerService::record_last_pass()` queues one more `Power` and `Duty` off the cadence gate, so a session parked for any reason ends on that pair and then its `End`, and a run to cutoff ends on a `Power` whose level says so (`../../core/diag/README.md`).

A write the part refuses is not the same thing as a sector the allocator refuses. The first is a storage fault: it is counted on the pool, published to the capture page, and the record stays in the RAM ring for the next pass. The second is the end of the capture, and it is written down.

A long capture rotates rather than stopping, so it can lose the sector it opened in. The allocator counts that at the moment it decides it, the capture turns it into a `Gap` plus a fresh `Boot` and `Config`, and the `list` reply marks the session `truncated` (`../../core/store/README.md`).

One service drains `bus.log_rx`, and it is the flight log, because the gates it applies are the flights gates: the link claim, the ground state, and the prompt a destructive erase takes. It routes by `request.store` and refuses by name - `no_diagnostics` for a store this build has no service for, `flights_only` for an erase aimed at the capture, whose ring the allocator recycles on its own. Erasing is per ring: the flights erase clears every sector that is not the capture's, one a pass, and the capture's sectors are left where they are.

The order in `services_` is the dataflow, not a preference. `ownship` publishes the fix before `traffic` measures against it; `traffic` drains `bus.rf` and stamps the transmit before `radio` closes its deadline against it; `screen` goes last because it is the only service allowed to spend a whole pass on pixels.

## Features and capabilities

A capability is what the board has, probed at bring-up and granted from the devicetree (`ports/capabilities.h`). A feature is what this build claims to do (`features.h`), and every feature names the capabilities it needs in `kFeatureSpecs`. A service asks `supported(declared, capabilities)` in its `setup()`, so a claim the hardware cannot serve is off rather than broken.

A feature bit with no reader does not exist. The list is exactly the claims a service branches on, and a bit is added on the commit that reads it.

## The parts of a boot

`setup()` probes the board, classifies the reset, and answers one question before anything else: is this boot a device at all. A charger plugged into a unit in a flight bag goes straight back to SYSTEM OFF with no service started (`core/power/wake.h`).

A refused boot is the second place the flat cell is named, and `park_refusal()` is the shell's second loop: it settles the one frame `core/power`'s `refused_frame` owes the glass, then the rails go. `kRefusalParkCeilingMs` bounds it, because a panel that never reports ready must not hold a flat cell awake.

What survives a failed self test is a device that still shows why: the page stays on the glass, the button still works, and the loop refuses to fly. That is `flyable_`, and it is false when a required capability is missing.

## What the glass wears while the device is off

E-paper holds its last image with the rails down, so the frame pushed immediately before the rails drop is what the device wears in a flight bag for as long as it sits there. There are four, and `product.h` picks between them by why the device is going down.

The wordmark is the ordinary one: a long press, or the companion link asking. A stow, which is the long press with the pad held, leaves the glass blank instead, because months of one image is ghosting an e-paper never fully loses. An install says so, because the bootloader is about to take the device and a pilot watching a blank panel would think it had died.

The fourth is the cell, `FLAT BATTERY` under the mark, and it is the only one that says something happened rather than naming a state. It is pushed by the cutoff, on the way down, because nobody asked for that power-off and the frame is the only thing left that can say why the device stopped. The same shutdown withholds the wake pin (`button_wake_after`), so the press that follows does nothing at all, and the frame it would have answered is already on the glass.

A cell that empties itself on a shelf over a winter runs no shutdown and writes nothing, and that unit is the one a refused boot is for: the press gets no device, so the refusal pushes the same frame instead (`core/power/README.md`). Both roads end on the same glass and the same withheld button.

The cable is the way back, and it is one act. VBUS wakes this SoC out of SYSTEM OFF and the reset cause carries the bit, but `core/power/wake.h` refuses that boot on purpose: a charger found in a flight bag must not switch a device on. What the cable does is charge the cell and, through the refusal, leave the button armed - so that refusal also takes `FLAT BATTERY` back off the glass and puts the wordmark there, the frame every armed, switched-off device wears. The press after it is the pilot asking for a device, and it gets one even while the cell is still filling.

What the frame does not carry is the percentage. It would be the reading the device died at, frozen at zero, standing there unchanged through the whole charge that follows, and a figure that cannot update is a figure that lies the moment it matters.
