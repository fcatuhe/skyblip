# products/skyblip_bravo

skyBlip Bravo is a Seeed SenseCAP MeshTracker X1 that receives 1090 MHz ADS-B full time and feeds one phone EFB over FLARM NMEA. It alarms on its own buzzer and motor, or plays the alarm of the Go it is paired with, and it transmits nothing. `product.h` wires it over `boards/seeed/meshtracker_x1/board.h`, and `main.cpp` is the shell on silicon. The host suite runs the same product on the X1's part models (`hardware/platform/host/meshtracker_x1.h`), and the simulator flies it among ADS-B traffic (`../../simulator/README.md`). The factory bootloader it installs under is in `../../boards/seeed/meshtracker_x1/factory/README.md`.

## The services

`../common/README.md` says what each shared service does. Bravo steps ten, in this order:

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
| `peer` | the pin of one Go, the peer endpoint, the reference Bravo places traffic against and the Go's call Bravo plays (`services/peer.h`) |

`ownship` runs before `adsb`, so a frame is folded against this pass's fix. `alarm` runs before `button`, so a press is read against the lamp the pilot sees. `peer` runs last, so it sends what `adsb` folded in the same pass, and `adsb` places traffic against the claims `peer` read in the pass before.

Bravo runs no `TrafficService`: 1090 MHz is its only source, so `AdsbService` owns `state.traffic`. It drains `bus::AdsbQueue`, which the radio thread fills, decodes each frame with `adsb::receive`, and folds DF17 messages into `adsb::Assembly`. Each folded observation goes to `common::AdsbCapture::record` before the table, because a capture asks about every aircraft heard and a full table keeps only the nearest. Callsigns go to `state.callsigns`, where `$PFLAA` and the Go both read them. `age_out` runs on the assembly every pass, as `protocols/adsb/README.md` requires.

## The GNSS rate

Bravo asks its AG3335 for one solution a second, `kSolutionRate` in `product.h`, because it transmits nothing and no ADS-L clause dates its position to a quarter second. Its flight log, its EFB sentences and its ADS-B gate read one position a second, as on the Go. The AG3335 driver solves only at that rate, and `common::solution_rate_serves` refuses at compile time a product that asks for more (`../../hardware/parts/ag3335/README.md`).

## Without a fix

The receiver and the decoder keep running, and CPR pairs an even and an odd position with no reference, so aircraft are folded and recorded. The phone gets no `$PFLAA`, because `NmeaService` sends traffic only from a valid own position, and `$PFLAU` says GPS 0. The lamp shows no fix.

A fix that comes back does not make an old observation fresh. Before UTC the table dates a target by a boot-relative second, and the first fix moves the table onto UTC, so a target heard before the fix is forgotten until its next position.

## Settings

`settings.h` is Bravo's struct, blob version 4 sealed by `core/settings/blob.h`: `aircraft_address`, `aircraft_address_from_go`, `alarm`, `go_alarm`, and `peer`, the Bluetooth address and type of the pinned Go. `alarm` is Bravo's own, which plays while it is not paired. `go_alarm` is the last alarm its pinned Go claimed, which plays while paired, link down and restart included. An older blob reads as defaults, because no X1 ever stored one.

Bravo ignores every key of a `set` it does not store. While paired, a `set` that carries `alarm` is refused `set_on_go`, so a phone never leaves Bravo disagreeing with its Go. `schemas/link.v1.json` says which product stores each key. Bravo stores no aircraft type, battery trim or frequency trim: own-ship stamps ADS-L G.1.3 category 0, and the divider is read untrimmed.

## Prompts without a screen

A phone's command stands as a prompt, as on the Go (`../../core/comms/README.md`). The lamp blinks it fast green, one press allows it, and with no second contact to refuse with, it expires after thirty seconds. The press follows `common::PromptAnswer`'s rule (`../common/README.md`, Prompts and the press that answers them). A traffic advisory outranks the prompt on the lamp, so while one stands a press dismisses the advisory. A browser Bravo trusts acts with no press (`../../core/comms/README.md`, Trusted browsers). Bravo holds no key, and draws the challenge's nonce from its RNG.

A prompt is refused only while the flight state is `Airborne`, and a takeoff cancels a standing one. A Bravo with no fix since boot is `Unknown`, so a Bravo indoors can be updated over Bluetooth.

## The lamp

`core/indication/README.md` holds the table, its priority order and what each row may spend. Bravo hands its alarm service `lamp_shows_link`, so its lamp also shows a prompt, a phone, a pairing request and a capture.

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

## The button

| Press | Meaning |
|---|---|
| a press from off | on: the button is the wake pin |
| a press with an advisory standing | dismiss, on its Go too while the link is up |
| a press with a prompt on the lamp | allow the prompt |
| a press with a Go's pairing request on the lamp | pair with that Go |
| a press while an upload window is open | close it, once the press that opened it has settled |
| a hold past `power::kLongPressHoldMs` | off, signed with three pulses under the thumb |

The hold is the shutdown sequencer's, read off the pin. A press is a release before the hold, so a hold never answers a prompt or a pairing request.

## Paired with a Go

A Go paired with a Bravo is a Tango. The Go's half and the peer frames are in `../../core/comms/README.md` (The peer endpoint, The Go's link to its Bravo). Bravo's half is `services/peer.h`, which drives `Pairing` (`pairing.h`), `GateReference` (`gate_reference.h`) and `ObservationPacer` (`observation_pacer.h`).

### The gesture

A Go asks to pair with `peer_claim` frames whose `paired` is false. The first one on a connection stands as a pairing request for `Pairing::kRequestWindowMs`, 30 s, as long as the Go waits. A connection asks once, so a Go left connected cannot hold the lamp.

One press pairs, armed by the prompt's rule on the same `common::PromptAnswer`. The gesture differs from the others by what stands on the lamp: dismiss needs an advisory, confirmation a prompt, and off a hold.

A trusted browser can stand in for the press: its `pair`, naming the Go's tag, arms Bravo for `PeerService::kAcceptanceWindowMs`, 60 s, and that Go's request is answered in the pass it arrives (`../../core/comms/README.md`, Pairing from a browser).

### The pin

Each side tells the other whether it holds the pin: a `peer_claim` says `paired` when the Go holds Bravo's, and every `health` says `paired` when Bravo holds, or offers, that Go's.

The press only offers Bravo to that Go, whose `health` then says `paired` true, and the Go pins Bravo on it. Bravo stores its pin, the Go's whole address and type, only on that Go's next `peer_claim` saying `paired` true within `Pairing::kOfferWindowMs` of the press. So an interrupted pairing never leaves a pin on one side alone for long: a Go that pinned without Bravo's half is answered `paired` false on its next connection, drops its pin and asks again.

A Go that unpairs while connected writes one `peer_claim` with `paired` false, and Bravo drops its pin. A Go unpaired away from Bravo cannot say so, so Bravo keeps its pin until that Go asks to pair again or a press for another Go replaces it. Bravo raises no request from a Go it told `paired` false while its pinned Go is connected (`PeerService::ask`), so a replaced Go cannot take Bravo back with a stray press.

`PeerService::paired()` is true only while the pin is stored, after both sides confirmed. Bravo plays only that Go's calls.

### The connection

While a Go is pinned, `reserve_peer_connection_for()` keeps one of Bravo's three connections for it (`../../core/comms/README.md`, Sessions). Phones get two, and a third is refused. Bravo keeps advertising while its Go is away, so the Go can find it.

### What Bravo serves

`peer_claim` is read from any Go for its `paired` flag. Its own-ship, aircraft address, alarm and call are taken only from the pinned Go. A frame that is not a well-formed claim is refused whole and counted in `PeerService::refused()`, and its sender is not taken for a Go.

`health` goes once a second, `PeerService::kHealthPeriodMs`, to every Go that has written a valid claim, and at once when a Go connects, unpairs, claims a pin Bravo does not hold, or a press dismissed its call. Besides the pin, the receiver, UTC, the reference and its own transponder, it carries what a glass would show, which the Go displays for Bravo (`../skyblip_go/pages/README.md`): `firmware`, `battery_percent`, `charging`, `power_level`, `frame_rate_hz`, the 1090 MHz frames of the last second, `gnss_stage` and, with a fix, `satellites`. A Go reads a `health` from an older Bravo without them as no reading. With UTC it says where the second comes from, `utc_source`: `own` is Bravo's receiver, whose last second the uptime clock carries while the receiver is silent (`../../core/timing/README.md`, How long a silent receiver's second holds). A Go reads a Bravo without `utc_source` whose receiver is silent as without UTC, because an older Bravo's second stops with its receiver and every observation it sends is too old to keep.

`observation` goes to every Go that has written a claim, by `send_to()`, so a phone's narrow payload never refuses it. One target is one frame, sent when the table took news of it, at most once in `ObservationPacer::kTargetUpdateFloorMs`, 500 ms. A frame carries each field's own instant, so a late frame never makes a target fresher, and a Go whose link refuses a frame keeps it owed. Slots are served in rotation, not nearest first, so on a busy sky a near target can wait behind far ones. Nobody has measured that wait yet.

### The reference

`GateReference` picks the position Bravo places its traffic against, for the table's sanity check and its nearest-first eviction, and so for what it exports:

1. the pinned Go's own-ship from a `peer_claim` younger than `GateReference::kClaimMaxAgeMs`, 4 s, while its link is up;
2. else Bravo's own valid fix;
3. else nothing, and nothing is exported.

`health.export_reference` says which, and Bravo exports only while it also holds UTC. An unknown phase ages a claim, and a claim written without UTC is never the reference. Bravo's own barometer never stands in for the Go's pressure altitude. The CPR reference is the same position, so a Bravo with no fix still decodes around its Go, while the NMEA stream to a phone stays relative to Bravo's own fix.

### The alarm

While Bravo is paired, its Go's `ALARM` row sets the alarm for both units (`../../core/comms/README.md`, The alarm). `AlarmSettings::in_force()` reads `go_alarm` while a pin is stored and `alarm` otherwise. `PeerService` stores each claim's `alarm` as `go_alarm` when it differs, under the durable-write window, so Bravo keeps its Go's alarm through a lost link and a restart.

While the pinned Go's connection is up and its last claim is younger than `PeerService::kCallMaxAgeMs`, three claim periods, `PeerService` publishes the claim's call as `bus::State::followed_call`, and the alarm service plays it. Bravo still grades its own ADS-B for its `$PFLAU` and `$PFLAA` (#184), but that grading sounds nothing while Bravo plays the Go's call.

Bravo grades its own ADS-B again, at `go_alarm`, on the pass after the Go's connection drops or no claim arrived for 3 s. A call the Go's long touch dismissed is called once more if Bravo hears that aircraft itself, because a call heard twice is the safer loss. After the Go unpairs, Bravo plays its own `alarm` again. In the air, Bravo plays one low note on the pass it stops playing its Go's calls (`../../core/annunciation/README.md`, A lost peer is one low note).

The aircraft address is the pinned Go's when its last claim carried one, else Bravo's own setting. Bravo stores the Go's address as its own with `aircraft_address_from_go` set, so it keeps filtering the aircraft's transponder without its Go. A claim with no address clears the setting only when the Go gave it, and a phone's `set` of `aircraft_address` clears `aircraft_address_from_go`.

### Own transponder

`OwnTransponder` names the transponder Bravo takes for the aircraft's own, and `health` carries it to the Go as `own_transponder`, with the callsign and category its identification gave. The Go proposes it on the ground only and stores it on the pilot's press (`../skyblip_go/pages/README.md`, aircraft). `AdsbService` also publishes it as `bus::State::own_transponder` while Bravo is on the ground, and the config link carries it to a phone in `status`.

It hears every decoded DF17 before the assembly drops the aircraft address, and every DF11 and reply `Transponders` believes. An emitter qualifies when it is the loudest heard in the last `kEmitterMaxAgeMs`, 5 s, at or above `kLoudFloorDbm`, -30 dBm, and `kLeadDb`, 10 dB, ahead of every other, and is named once it has qualified for `kLeadHoldMs`, 10 s.

The three figures are unmeasured. Free space puts a 250 W transponder at about -19 dBm at 100 m, but the fuselage sits between a belly antenna and the cockpit, and a neighbour 20 m off may be as loud as our own transponder. That is why the lead must hold and the pilot confirms. A capture in a cockpit beside another aircraft sets all three.

## The name

Bravo advertises `skyBlip Bravo 3F2A`, where `3F2A` is the low 16 bits of FICR `DEVICEADDR`, through `bravo::bluetooth_name` (`name.h`). Bravo is never activated: its platform fills `ports::KeylessIdentity` with the DEVICEID and the tag, so `challenge` answers `keyless`.

## The way down

Every shutdown goes through `core/power`'s sequencer, as on the Go: the LR2021 sleeps, the outputs park after the sign-off pulses, and the X1's platform walks `core/power`'s power-down order before SYSTEM OFF (`../../boards/seeed/meshtracker_x1/README.md`).

A receiver that never confirms its sleep is held in reset after `kReceiverParkCeilingMs`, the LR2021's worst sleep, 4 s. A part whose probe failed has no receiver thread, so `sleep()` parks it directly: a SetSleep if the part takes one, and NRESET held low if not.

The factory bootloader does not send a cable wake from SYSTEM OFF back to sleep (`../../boards/seeed/meshtracker_x1/factory/README.md`), so `core/power/wake.h` refuses a VBUS wake with no button held, and `power::boot_path()` refuses a boot on a flat cell too. The path is decided before bring-up, so a refused boot starts no receiver, no link and no NVS. It holds the LR2021 in reset through NRESET, because after any SoC reset the radio's state is unknown, and switches off with NRESET still low.

## A unit that refuses to fly

A Bravo whose receiver or GNSS does not answer runs no service, so the product blinks the lamp's critical row itself, red every 600 ms, from the first pass. The cutoff still guards the cell, and a 2 s hold still switches the unit off.

## Bring-up and the watchdog

`main.cpp` arms the watchdog before `setup()`, as on the Go. With BUSY stuck, the receiver's `begin()` blocks about 7 s (`../../hardware/parts/lr2021/README.md`), and the software channel allows `CONFIG_TASK_WDT_MIN_TIMEOUT` less a second, 9 s. A stuck radio leaves 2 s for the rest of the bring-up, and a bring-up past that boots again as a watchdog restart.

## When flash may be touched

Bravo keys no transmitter, so flash waits only for the 1090 MHz queue, which the loop drains. `book_flash_beside_adsb` in `flash_window.h` books at most `kFlashPassCeilingMs` of flash work in a pass, half the queue's drain window, and a `static_assert` holds a sector claim to it. The erase cost it books is the T-Echo's MX25R figure until #169 times the X1's GD25Q64E.

## Captures

Bravo has no glass, so a page or `scripts/blip.py capture` arms a capture over the link with the `capture` command, which stands as a prompt like every other. The press arms `diagnostics::Recorder`, and `CaptureService` drains it as on the Go, ADS-B records included (`../skyblip_go/README.md`, Which capture is armed).

## Mode S transponders

`Transponders` (`transponders.h`) holds up to `kMaxTransponders` Mode S transponders. A reply overlays its address on its parity, so noise reads as an address too (`../../protocols/adsb/README.md`, Mode S: replies with no position). A transponder is admitted only when heard in clear, in a DF17 or in a DF11 whose parity is zero, and a reply counts only from an address already held. A transponder silent for `kTransponderForgetMs`, 30 s, is freed.

This is detection only: no Mode S transponder reaches the traffic table, the glass, the NMEA or an alarm until #239's bench says how many Bravo hears and how far. That bench also sets the 30 s.

## Reading the adsb group

The `adsb` group of the status dump follows a frame from the antenna to the table. `../../core/comms/README.md` (The 1090 MHz group) says what each key counts.

| Key | A frame counted there |
|---|---|
| `frames` | the LR2021 heard a preamble, decided the 112 bits behind it, and handed them up |
| `dropped` | was lost before the decoder |
| `formats` | each frame decoded, by the downlink format its first five bits name |
| `crc` | a DF17 or DF11 that failed its parity, which nothing repairs |
| `unsupported` | a format or a DF17 message the decoder does not read |
| `unmatched` | a Mode S reply from an address not heard in clear first |
| `folded` | was folded into an aircraft by `adsb::Assembly` |
| `aircraft` | not a frame: the aircraft the assembly holds now |
| `transponders`, `mode_s`, `mode_s_altitude` | not a frame: the transponders held, those never heard in DF17, and those with a recent altitude |

`tracked`, in the `traffic` group, counts the aircraft in the table. So `crc` close to `frames` is a reception problem, and `aircraft` above 0 with `tracked` at 0 is aircraft heard without a decoded position. Noise that passes the preamble detector spreads evenly over formats no transponder sends, DF1 to DF3, DF6 to DF10 and DF12 to DF15, which measures it.

## The cell

`PowerService` reads Bravo's 1100 mAh high-voltage cell through the Go's LiPo curve and voltage ladder (`../../core/power/README.md`). No discharge curve of the X1's cell is published, so Bravo keeps the Go's values until #169 measures one from full to cutoff.

## Untested on silicon

On an X1 a Go has paired with one press. The bench (#169) checks the rest:

- the image boots from the `.uf2`, the lamp winks blue then green on a fix, and the button switches the unit off and on with the pulses felt;
- the AG3335 acknowledges its configuration and its PPS edge reaches the platform;
- the LR2021 reads version 1.24 and frames reach the `adsb` group;
- XCSoar shows ADS-B traffic from Bravo with Bravo's position;
- an advisory sounds the buzzer and the motor, and one press dismisses it;
- a `set` and a `dfu` stand on the lamp, and a Bluetooth update installs and confirms;
- a capture armed from a page winks the lamp, records ADS-B through a flight and reads back with `blip.py fetch --log diagnostics`;
- a switch-off reaches SYSTEM OFF with the off current measured, and a cable leaves it off;
- the button held while the cable goes in opens the `X1` drive, and released within 3 s boots the unit;
- both sides keep the pin across a power cycle, and a third phone is refused while the Go is away;
- paired with a Go+, an advisory sounds on both and a press on either silences both, and Bravo grades its own within 3 s of the Go+ switching off;
- paired with a plain Go on `BRAVO LOUD`, the Go's ADS-L traffic sounds on Bravo;
- endurance from full to cutoff, published as measured.
