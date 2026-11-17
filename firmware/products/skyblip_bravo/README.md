# products/skyblip_bravo

skyBlip Bravo: a Seeed SenseCAP MeshTracker X1 that receives 1090 MHz ADS-B full time and feeds one phone EFB over FLARM NMEA, alarming on its own buzzer and motor. It transmits nothing. `product.h` wires it over `boards/seeed/meshtracker_x1/board.h`, and `main.cpp` is the shell on silicon. The host suite runs the same product on the X1's part models (`hardware/platform/host/meshtracker_x1.h`), and the simulator flies it among ADS-B traffic, with a phone reading its NMEA (`../../simulator/README.md`).

It runs on the X1. The factory bootloader it installs under is read in `../../boards/seeed/meshtracker_x1/factory/README.md`, and the pins, timings and part behaviours #169 still benches are listed at the end.

## The services

`products/common/README.md` says what a service is, and the Go's README says what each shared one does. Bravo steps ten, in this order:

| Service | Owns |
|---|---|
| `config` | the companion link, Bravo's settings blob and `state.link` (`common::ConfigLinkService`) |
| `ownship` | `state.own`, from the AG3335 and the SPA06 (`common::OwnshipService`) |
| `power` | `state.power` (`common::PowerService`) |
| `adsb` | `state.traffic`: it drains the 1090 MHz queue, decodes, folds and records (`services/adsb.h`) |
| `alarm` | the buzzer, the motor and the lamp (`common::AlarmService`) |
| `button` | what a press means (`services/button.h`) |
| `nmea` | the FLARM sentences to the phone (`common::NmeaService`) |
| `flight_log` | the flights ring and the Log endpoint (`common::FlightLogService`) |
| `capture` | the diagnostics ring (`common::CaptureService`) |
| `peer` | the pin of one Go, the peer endpoint and the reference Bravo places traffic against (`services/peer.h`) |

`ownship` runs before `adsb`, so a frame is folded against this pass's fix. `alarm` runs before `button`, so a press is read against the lamp the pilot is looking at. `peer` runs last, so the loop codes of the nine before it hold. It sends what `adsb` folded in the same pass, and `adsb` places traffic against the claims `peer` read in the pass before.

Bravo runs no `TrafficService`. 1090 MHz is its only source, so `AdsbService` owns `state.traffic` instead: it hands the table the reference and the aircraft address on every pass, as the Go's traffic service hands its own position and setting (Paired with a Go). Alone, the reference is Bravo's own fix and the aircraft address its own setting. It pops each `events::AdsbFrame` from the queue the radio thread fills, decodes it with `adsb::receive`, and counts a frame it refuses: in `crc()` when its parity fails, and in `unsupported()` when its parity holds but its message is one the decoder does not read. It folds the rest into `adsb::Assembly` against Bravo's own position, with the frame's instant on `ports::Clock` and a stamp on the PPS edge. Each folded observation goes to `common::AdsbCapture::record` before the table, because #183 asks about every aircraft heard and a full table keeps only the nearest (`../common/README.md`). An observation with a position then goes to the table. An identification message's callsign goes to `state.callsigns`, keyed on the address, so a `$PFLAA` and the Go both read it there. `age_out` runs on the assembly every pass, as `protocols/adsb/README.md` requires, and on the table and the callsigns after it.

The queue, `bus::AdsbQueue`, lives in the product beside `bus::Bus`, because only Bravo hears 1090 MHz. The board's receiver pushes into it, from its own thread on silicon and from the board's poll on the host.

## Without a fix

The receiver and the decoder keep running. CPR still pairs an even and an odd position with no reference, so aircraft are folded and recorded. Nothing reaches the phone as `$PFLAA`, because `NmeaService` sends traffic only from a valid own position, and `$PFLAU` says GPS 0. The lamp shows no fix.

A fix that comes back does not make an old observation fresh. Each assembly field keeps the instant it was heard, and the traffic table dates a target by the second it was received. Before UTC that second is boot-relative, and the first fix moves the table onto UTC, so a target heard before the fix is forgotten rather than exported with a new age. It comes back on its next position.

## Settings

`settings.h` is Bravo's struct, sealed by `core/settings/blob.h` as blob version 3. It holds `aircraft_address`, `aircraft_address_from_go`, `alarm`, and `peer`: the Bluetooth address and type of the Go Bravo pinned, and whether one is pinned. Paired with a Go says when the pin is written and cleared, and when `aircraft_address_from_go` is set. The pin is in version 1, so the first image already carried it. Version 2 added `aircraft_address_from_go`, and `from_blob` reads a version 1 blob whole, pin included, with `aircraft_address_from_go` false. Version 3 replaced `alarm_enabled` and `alarm_volume` with `alarm`, and either old off comes back `visual` (`../common/README.md`).

Bravo answers `config` and `defaults` with `aircraft_address` and `alarm` only, and ignores every other key of a `set`, as the Go ignores `address`. `schemas/link.v1.json` says which product stores each key. A `set` on Bravo stands as a prompt like any other, and `describe_changes` writes no rows, because there is no glass to show them on.

Bravo stores no aircraft type, battery trim or frequency trim. Own-ship stamps ADS-L G.1.3 category 0, no emitter category, and the power service reads the divider untrimmed.

## Prompts without a screen

A phone's `set`, `dfu`, `recovery`, `power_off`, `gnss_cold_start`, `capture` or log erase stands as a prompt, as on the Go (`core/comms/README.md`). The lamp shows it as a fast green blink, and one press allows it. Bravo has no second contact to refuse with, so a prompt nobody allows expires after thirty seconds. A browser Bravo trusts changes the settings and starts an update with no press (`core/comms/README.md`, Trusted browsers). Bravo holds no key, and draws the challenge's nonce from its RNG.

`common::PromptAnswer` (`../common/prompt_answer.h`) arms the answer only once the lamp shows the prompt, the button is up, and it has been up for `kAnswerSettleMs` since its last release and since the prompt arrived. A contact that began before it armed is never spent on it, so a button already held when a phone asks cannot answer the prompt with its release. A prompt replaced by another of the same kind disarms it as a new prompt does. A traffic advisory outranks the prompt on the lamp, so while one stands the answer stays disarmed and a press dismisses the advisory.

A prompt is refused only while the flight state is `Airborne`, as on the Go (`core/comms/config.h`, `prompt_allowed_in`), and a take-off cancels a standing one. A Bravo with no fix since boot is `Unknown`, not `Airborne`, so a Bravo indoors can be updated over Bluetooth.

## The lamp

`core/indication` decides the lamp for both products. Bravo claims `common::Feature::LampShowsLink`, so its alarm service also tells the table whether a prompt stands and whether a phone is connected. The Go makes no such claim and its lamp is unchanged.

| State | Lamp |
|---|---|
| on | after a press from off, one green pulse felt on the motor, then the condition below |
| no fix | blue, one wink every 3 s |
| fix | green, one wink every 3 s |
| a phone connected | the same colour, one wink every 1.5 s |
| advisory | red, fast flicker |
| prompt | green, fast blink |
| a Go asks to pair | blue, fast blink |
| a capture armed | the fix colour, two winks every 3 s |
| low battery | red, one wink every 3 s, and a 600 ms blink when critical |
| refused to fly | red, a 600 ms blink, as a critical cell |
| going off | three red pulses felt on the motor, then dark |
| off | dark |

`core/indication/README.md` holds the table, its priority order and what each row may spend.

## The button

| Press | Meaning |
|---|---|
| a press from off | on: the button is the wake pin |
| a press with an advisory standing | dismiss, as the Go's long touch does |
| a press with a prompt on the lamp | allow the prompt |
| one press with a Go's pairing request on the lamp | pair with that Go |
| one press while an upload window is open | close it, once the press that opened it has settled |
| a hold past `power::kLongPressHoldMs` | off, signed with three pulses under the thumb |

The hold is the shutdown sequencer's, read off the pin, so it works as on the Go. A press is a release before the hold, so a hold never answers a prompt or a pairing request.

## Paired with a Go

A Go paired with a Bravo is a Tango. The Go's half is `products/skyblip_go/services/bravo.h` and `core/comms/README.md`, The Go's link to its Bravo. Bravo's half is `services/peer.h`, and three objects it drives: `Pairing` (`pairing.h`), `GateReference` (`gate_reference.h`) and `ObservationPacer` (`observation_pacer.h`).

### The gesture

A Go asks to pair with `peer_claim` frames whose `paired` is false. The first one on a connection stands as a pairing request for `Pairing::kRequestWindowMs`, 30 s, as long as the Go's `kAnswerCeilingMs` waits for it. A Go that gives up drops the link, and the request goes with it. One request stands at a time, and a connection asks once, so a Go left connected cannot hold the lamp. The request goes when its Go's connection goes. The lamp shows it as a fast blue blink, below an advisory and a phone's prompt and above the cell.

One press pairs. The press arms by the prompt's rule, read off the same `common::PromptAnswer` (`../common/README.md`): only once the lamp shows the request, with the button up for `kAnswerSettleMs` since its last release and since the request arrived. A down that began at or before the instant it armed, dated by the debounce, disarms it. So a press begun before the request never pairs, an advisory takes the press to dismiss it, and a hold is the shutdown's. The gesture differs from the others by what stands on the lamp: dismiss needs an advisory, confirmation a prompt, and off a hold.

### The pin

Each side tells the other whether it holds the pin. A `peer_claim` says `paired` when the Go holds Bravo's, and every `health` says `paired` when Bravo holds, or offers, that Go's.

The press does not store Bravo's pin. It offers Bravo to that Go: the Go's `health` says `paired` true, and the Go pins Bravo on the first such `health` it reads, never on one that says false. The Go's next `peer_claim` carries `paired` true, and only that claim, from the offered address within `Pairing::kOfferWindowMs` of the press, stores Bravo's pin: the Go's whole address and type, in the settings blob. A paired Go reads `CONNECTED` only once a `health` on that connection says `paired` true.

A pairing interrupted on either side leaves no pin on one side alone for long:

- before the press, nothing is stored on either side;
- a `health` lost after the press leaves the Go unpinned. It refuses for no answer, and the offer expires. A Go that asks again on a new connection, before the offer expires, drops the offer and needs a new press;
- a Go that pinned and lost the link before Bravo read its confirmation reconnects. Within the offer window its first claim completes Bravo's half;
- after the offer window, or after Bravo restarted, the Go's first claim on the new connection is answered at once with `health` saying `paired` false. The Go drops its pin, never reads `CONNECTED`, and asks again: the lamp blinks blue, and one press pairs both sides. With no press in time the Go frees its phone slot and its glass shows `NOT PAIRED ON BRAVO`.

So a pin on the Go alone lasts until the Go next reaches Bravo, and is cleared on the first `health` after its first claim. Bravo raises no request from a Go it told `paired` false on that connection while its pinned Go is connected (`PeerService::ask`), so a Go another one replaced cannot take Bravo back with a stray press. A pin on Bravo alone needs the Go to restart inside its 3 s settings write after reading Bravo's `health`. Bravo cannot tell that Go from a paired Go switched off, so its pin stays until that Go asks again or another Go replaces it.

A Go that unpairs while connected says so: it writes one `peer_claim` with `paired` false and keeps the link until Bravo's `health` answers `paired` false, at most `BravoService::kFarewellCeilingMs`, 2 s. Bravo drops its pin and frees the kept connection on that claim. A Go unpaired while away from Bravo cannot say so, and Bravo keeps its pin until that Go asks it again or another Go replaces it. A farewell lost on the link ends the same way, since the Go drops the link after 2 s whether Bravo answered or not. A Go that asks to pair while Bravo holds its pin has unpaired, so Bravo drops its pin and shows the request. Another Go's request answered with a press replaces the pin, and the old Go's next claim is answered `paired` false, so it drops its own.

`PeerService::paired()` is true only while the pin is stored, which is after both sides confirmed. Bravo gives the alarm only to that Go (The alarm).

### The connection

While a Go is pinned, `reserve_peer_connection_for()` keeps one of Bravo's three connections for that address (`core/comms/README.md`, Sessions). Phones get two, and a third phone is refused. Bravo keeps advertising while its Go is away, so the Go can find it, and refuses a phone that connects past the ceiling.

### What Bravo serves

`peer_claim` is read from any Go for its `paired` flag. Its own-ship and aircraft address are taken only from the pinned Go. A frame that is not a well-formed claim, or carries half a field, is refused whole and counted in `PeerService::refused()`, and its sender is not taken for a Go.

`health` goes once a second, `PeerService::kHealthPeriodMs`, whatever the traffic, to every Go that has written a valid claim on its connection, and at once to a Go new on a connection, to a Go that unpaired, and to a Go claiming a pin Bravo does not hold. Its `paired` is true only for the pinned Go and an offered one. It says whether the receiver listens, whether Bravo holds UTC, the last observation sequence, whether Bravo's aircraft address is set, the reference, and the transponder Bravo takes for the aircraft's own (Own transponder). It also says what a pilot would read on a glass Bravo does not have: the image it runs as `firmware`, its cell as `battery_percent`, `charging` and `power_level`, the same readings the `status` frame gives a phone, and `frame_rate_hz`, the 1090 MHz frames its receiver handed up in the last second. The Go shows them on its `BRAVO LOG` page and says `BRAVO BAT` on its radar from `Low` down (`../skyblip_go/pages/README.md`). A Go reads a `health` without them, from a Bravo built before them, as no reading.

`observation` goes to every Go that has written a `peer_claim` on its connection, each by `send_to()`, so a phone's narrow payload never refuses it. One target is one frame, from the traffic table, with its callsign and the second the callsign table heard it. A frame goes only when the table took news of the target or its callsign moved, at most once in `ObservationPacer::kTargetUpdateFloorMs`, 500 ms, so two a second. What Bravo hears in between is folded into the next. A frame carries each field's own instant, so a frame sent late, or after the link came back, never makes a target fresher. A callsign moves no time of the position. Each Go is owed each target on its own, so a Go whose link refuses a frame for now keeps it owed and gets it on a later pass, while a Go that took it waits for news and the floor. Slots are served in rotation, not nearest first. With no ADS-B gate a full table owes each Go up to 96 frames a second, 48 targets at two a second, so on a busy sky a near target's frame can wait behind far ones while the link refuses. Nobody has measured that wait yet.

### The reference

`GateReference` picks the position Bravo places its traffic against, for the table's sanity check and its nearest-first eviction, and so for what it exports to the Go. The table holds every ADS-B target it can place, with no range or height gate (`core/traffic/README.md`, No gate). The reference is:

1. the pinned Go's own-ship from a `peer_claim` younger than `GateReference::kClaimMaxAgeMs`, 4 s, while its link is up;
2. else Bravo's own valid fix;
3. else nothing, and nothing is exported.

The position and the height are judged apart: a height older than the ceiling leaves a fresh position, with `model::Ownship::altitude_hae_valid` false, so the table ranks on ground distance unless the pressure altitude is fresh.

`health.export_reference` says which, whatever UTC says. Bravo exports observations only while it also holds UTC. A claim's age is its position age, plus the time from its `at_s` to Bravo's UTC when both hold UTC, plus the time since Bravo read it. Without a phase on either side it counts the claim from the start of its second and Bravo from the end of its own, so an unknown phase ages a claim. A claim written without UTC is never the reference. Each field of the Go's own-ship is valid on its own: a pressure altitude older than the ceiling leaves the reference with no pressure altitude, and Bravo's own barometer never stands in for the Go's.

The CPR reference is the same position, so a Bravo with no fix still decodes locally around its Go. The NMEA stream to a phone is unchanged: it is relative to Bravo's own fix, so a Bravo with no fix sends a phone no `$PFLAA`.

### The alarm

Only Bravo silences itself, and only for a Go+ that sounds for both units (#179). The Go+ takes the alarm with `alarm_requested` true in its claim (`../../core/comms/README.md`, The Go's link to its Bravo). Bravo keeps its buzzer and motor silent while all of these hold:

- it holds the pin of that Go, and that Go's connection is up;
- the last claim it read from that Go said `alarm_requested` true;
- that claim is younger than `PeerService::kAlarmRequestMaxAgeMs`, 3 s, three claim periods, so one lost claim keeps it silent. #194 sets the figure on the bench.

`health.alarm_granted` says so to that Go, and is false to every other Go. A claim from a Go Bravo has not pinned never silences it.

Bravo sounds again by its own `alarm`, on the pass after any of these: the Go's connection drops, a claim says `alarm_requested` false, the Go unpairs, or no requesting claim arrived for 3 s with the link up. An advisory that stands is announced again at once. After a boot Bravo sounds until a requesting claim arrives.

`PeerService` publishes the decision as `bus::State::alarm_granted`, and `common::AlarmService` reads it with the alarm settings, so the traffic call, the first-fix tune and the motor all fall silent the way `visual` silences them. Nothing else changes: the lamp still flickers on an advisory, a press still dismisses it, and the `$PFLAU` and `$PFLAA` to a phone keep their alarm levels, because an EFB's alarm follows its own stream (#184).

Two costs are accepted rather than built around. At a handover, a Go+ that loses its fix stops asking only on its next claim, so up to about a claim period passes with no unit sounding. While the link fails one way, both units may sound. Two longer gaps are bounded by the ages above. A Go+ that switches off or stops in flight leaves Bravo silent until its connection drops or its last claim ages out, at most 3 s. Bravo's frames lost on the way to a Go+ whose claims still arrive leave Bravo silent until the Go+ reads `NO BRAVO`, at most the Go's `kHealthMaxAgeMs` and one claim period, 4 s, while the Go+ keeps alarming on the targets it already holds.

The aircraft address is the pinned Go's `aircraft_address` when its last claim carried one, else Bravo's own setting. The assembly and the table drop that address. Bravo also stores the pinned Go's `aircraft_address` as its own setting when it differs, with `aircraft_address_from_go` set, so it keeps filtering the aircraft's transponder without its Go. A claim with no `aircraft_address` clears the setting only when the Go gave it, so a Go cleared on its glass clears Bravo too, and a Go that never held one leaves what a phone set. A phone's `set` of `aircraft_address` clears `aircraft_address_from_go`.

### Own transponder

`OwnTransponder` names the transponder Bravo takes for the aircraft's own, and `health` carries it to the Go as `own_transponder`, with the callsign and category its identification gave. The Go proposes it on the ground only, and stores it only on the pilot's press (`../skyblip_go/pages/README.md`, aircraft). `AdsbService` also publishes it as `bus::State::own_transponder` while Bravo's own flight state is ground, and the config link carries it to a phone in `status`. So a Briefing page proposes it as Bravo's `aircraft_address`, and a save stores it after one press, like any other `set`.

`AdsbService` hands it every decoded DF17 before the assembly, because the assembly drops the aircraft address and a set address must still read as heard. An emitter's first frame sets its level, and each frame after it moves the level a quarter of the way to the frame's, so one fade or one spike decides nothing. Address 000000, which a misconfigured transponder squitters, is never an emitter: as an aircraft address it means unset. An emitter qualifies when it is the loudest of those heard in the last `kEmitterMaxAgeMs`, 5 s, it is at or above `kLoudFloorDbm`, -30 dBm, and it leads every other one by `kLeadDb`, 10 dB. It is named once it has qualified for `kLeadHoldMs`, 10 s, and a change of leader starts the hold again. A frame with no level is no evidence of loudness, and it never adds an emitter. It holds `kTrackedEmitters`, 8: a full table gives up an emitter gone quiet first, then the faintest. Every tick frees an emitter quiet past `kEmitterMaxAgeMs`, so its age never wraps round to live after 49.7 days of uptime.

The three figures are unmeasured. Free space alone puts a 250 W transponder at about -19 dBm at 100 m and about +15 dBm at 2 m, but the fuselage sits between a belly antenna and the cockpit, and nobody has measured what it takes, nor where the LR2021 saturates. A neighbour 20 m off on the apron may be as loud as our own transponder. That is why the lead must hold and why the pilot confirms the registration. A capture of a Bravo in a cockpit, beside another aircraft on the apron, sets all three.

## The name

Bravo advertises `skyBlip Bravo 3F2A`, where `3F2A` is the low 16 bits of FICR `DEVICEADDR`. The name rule is the product's, `bravo::bluetooth_name` in `name.h`, handed to the shared config link as an `identity::NameRule`. The Go hands it `identity::bluetooth_name`, its activation rule. The tag is not an activation identity, and Bravo is never activated: its platform fills `ports::KeylessIdentity` with the DEVICEID and the tag, so `challenge` answers `keyless` and `identity` names the unit as every Go does.

## The way down

A long press, a phone's `power_off`, a cutoff, an install and a recovery all go through `core/power`'s shutdown sequencer, as on the Go. Parking puts the LR2021 to sleep through its receiver and silences the buzzer and the motor. A shutdown that leaves the unit off then signs off: three red pulses felt on the motor, inside the park's wait (`../../core/indication/README.md`). Once the sign ends, or at once for an install or a recovery that reboots, it darkens the lamp and puts the DRV2605 in standby. The settings are flushed and a running capture parks. The X1's platform then walks `core/power`'s power-down order on its own lines before SYSTEM OFF (`../../boards/seeed/meshtracker_x1/README.md`).

A receiver that never confirms its sleep is held in reset after `kReceiverParkCeilingMs`. That ceiling is the LR2021's worst sleep, a stuck standby and the reset after it, 4 s.

A cable plugged into a switched-off X1 wakes the SoC. The factory bootloader, Meshtastic's, does not send that wake back to sleep (`../../boards/seeed/meshtracker_x1/factory/README.md`), so this firmware does: `core/power/wake.h` refuses a VBUS wake from SYSTEM OFF with no button held. The nRF52840 reports that wake as VBUS without OFF, which `read_reset_causes()` names `UsbVbus` with no `LowPowerWake`, and the rule refuses VBUS alone (`../../core/power/README.md`). #169's bench is to check that a cable leaves a switched-off unit off. The boot path is decided before bring-up, from the reset causes, the button and the cell, which `begin_wake()` reads, so a refused wake starts no receiver, no link and no NVS. The board probes nothing until the wake is accepted, so a refused boot sends the LR2021 no SPI command and writes no haptic enable. It does hold the LR2021 in reset through its NRESET line, a GPIO write: after any reset of the SoC the radio's state is unknown, and a cold start, on a low cell or on a battery just connected, leaves it in standby. `power::boot_path()` refuses a cable wake from SYSTEM OFF, any boot on a cell under the lockout, and a restart on a flat cell, and all of them take the same road. `park_refusal()` then waits for the button and switches off, with NRESET still held low through SYSTEM OFF. What that costs against a cold sleep is #169's to measure. A press with the cable in boots. The factory bootloader holds the start while a press under 3 s is down, so `boot_path` knows a button wake by its reset cause, `LowPowerWake` without `UsbVbus`.

The receiver reports itself parked only once the LR2021 is asleep or held in reset. A part whose probe failed, or whose bring-up never ran, has no receiver thread, so `sleep()` parks it directly: a SetSleep if the part takes one, and NRESET held low if not.

## A unit that refuses to fly

A Bravo whose receiver or GNSS does not answer runs no service, and with no service the lamp would stay dark while the SoC stays awake on the cell. So the product blinks the lamp's critical row itself, red every 600 ms, from the first pass. The cutoff still guards the cell, and a 2 s hold still switches the unit off.

## Bring-up and the watchdog

`main.cpp` arms the watchdog before `setup()`, as on the Go. With BUSY stuck, the receiver's `begin()` blocks about 7 s (`../../hardware/parts/lr2021/README.md`). It runs inside `setup()`, before the loop's 5 s service supervision applies, and the software channel allows `CONFIG_TASK_WDT_MIN_TIMEOUT` less a second, 9 s, so a stuck radio leaves 2 s for the rest of the bring-up. A bring-up past that bites, and the boot after it comes up as a watchdog restart.

## When flash may be touched

Bravo keys no transmitter, so flash waits for nothing on air. It waits for the 1090 MHz queue instead: the loop drains it, and a pass spent on an erase is a pass the queue fills. `book_flash_beside_adsb` in `flash_window.h` books at most `kFlashPassCeilingMs` of flash work in a pass, half the queue's drain window, and a `static_assert` holds a sector claim to it. The erase cost it books is the T-Echo's MX25R figure until #169 times the X1's GD25Q64E.

## Captures

`AdsbService` calls `AdsbCapture::record` on every folded observation, so a capture armed on Bravo carries the ADS-B record #203 defined.

Bravo has no glass, so a page or `scripts/blip.py capture` arms one over the link (#247). The `capture` command names the profile, `full`, `power_run` or `flight_run`, or `stop`, and stands as a prompt like every other: one press allows it, thirty seconds expire it, and it is refused while airborne. `ConfigService` refuses it at the door with `no_storage` when the flash holds no capture ring, `capturing` for an arm while a capture runs and `not_capturing` for a stop while none does, reading what `ConfigLinkService` hands it each pass from `state.capture` and the recorder. The press answers `capture` and latches the order. `Product::take_link_requests()` then arms `diagnostics::Recorder` with the profile, or disarms it, as the Go's glass does, and `CaptureService` drains the recorder as on the Go. While a capture is armed the lamp winks twice every 3 s in the fix colour, above the connected rows, so a pilot who armed it from a page sees it run.

A capture keeps running through a flight, and stopping it is refused in the air like every prompt. The power-off parks it as on the Go. The Go answers the same command, asking on its glass (`../skyblip_go/README.md`, Which capture is armed), and both call `ConfigService::serve_captures()` and `common::order_capture()`.

## Reading the adsb group

The `adsb` group of the status dump follows a frame from the antenna to the table. `core/comms/README.md` (The 1090 MHz group) says what each key counts.

| Key | A frame counted there |
|---|---|
| `frames` | the LR2021 heard a preamble, decoded a DF16 or DF17 frame behind it, and handed it up as chips |
| `dropped` | was lost before the decoder |
| `crc` | failed its parity, and the one repair allowed, a single doubtful chip pair, did not clear it |
| `unsupported` | passed its parity and carries a DF17 message the decoder does not read |
| `folded` | was decoded and folded into an aircraft by `adsb::Assembly` |
| `aircraft` | not a frame: the aircraft the assembly holds now, with or without a position |

`tracked`, in the `traffic` group, counts the aircraft that reached the table, each with a position. So `crc` close to `frames` is a reception problem, and `aircraft` above 0 with `tracked` at 0 is aircraft heard without a decoded position.

## The cell

`PowerService` reads Bravo's 1100 mAh high-voltage cell through the Go's LiPo curve and voltage ladder (`../../core/power/README.md`). No discharge curve of the X1's cell is published, so Bravo keeps the Go's values until #169 measures one from full to cutoff.

## Untested on silicon

The host suite runs every service above on the part models. On an X1, a Go has paired: its request blinked the lamp blue and one press paired both sides. The bench (#169) checks the rest, in this order:

- the image boots from the `.uf2`;
- the lamp winks blue, then green once the AG3335 has a fix, and the button switches the unit off and on, and the hand feels three pulses going off and one coming on;
- the AG3335 acknowledges its configuration and its PPS edge reaches the platform;
- the LR2021 comes up, its version reads 1.24, and frames reach the `adsb` diagnostics group;
- XCSoar shows ADS-B traffic from Bravo with Bravo's own position;
- an advisory sounds the buzzer and the motor, and one press dismisses it;
- a `set` and a `dfu` from the manage page stand on the lamp and one press allows them, and a Bluetooth update installs and confirms;
- a capture armed from its Briefing device page winks the lamp twice every 3 s, records ADS-B through a flight, stops from the page, and `blip.py fetch --log diagnostics` reads it back;
- a switch-off reaches SYSTEM OFF with the off current measured, and a cable plugged in leaves it off;
- after a switch-off, the button held while the cable goes in opens the `X1` drive with the LED white, and released within 3 s it boots the unit, which depends on what the reset circuit makes of the cable;
- both sides of a pairing keep the pin across a power cycle of either;
- with two phones on Bravo and its Go away, a third phone is refused, and the Go reconnects;
- the Go's BRAVO page reads `CONNECTED`, and an aircraft Bravo hears reaches the Go as `observation` frames, at most two a second;
- paired with a Go+ that has a fix, an advisory sounds on the Go+ alone, and Bravo sounds again within 3 s of the Go+ switching off;
- endurance from full to cutoff, with the conditions, published as measured.
