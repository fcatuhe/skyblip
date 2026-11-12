# products/skyblip_bravo

skyBlip Bravo: a Seeed SenseCAP MeshTracker X1 that receives 1090 MHz ADS-B full time and feeds one phone EFB over FLARM NMEA, alarming on its own buzzer and motor. It transmits nothing. `product.h` wires it over `boards/seeed/meshtracker_x1/board.h`, and `main.cpp` is the shell on silicon. The host suite runs the same product on the X1's part models (`hardware/platform/host/meshtracker_x1.h`), and the simulator flies it among ADS-B traffic, with a phone reading its NMEA (`../../simulator/README.md`).

Nothing here has run on an X1. The units have not arrived, so every pin, timing and part behaviour below the product is unverified until #169 benches one, and the bench list is at the end.

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
| `peer` | the pin of one Go, the peer endpoint and the gate reference (`services/peer.h`) |

`ownship` runs before `adsb`, so a frame is folded against this pass's fix. `alarm` runs before `button`, so a press is read against the lamp the pilot is looking at. `peer` runs last, so the loop codes of the nine before it hold. It sends what `adsb` folded in the same pass, and `adsb` gates on the claims `peer` read in the pass before.

Bravo runs no `TrafficService`. 1090 MHz is its only source, so `AdsbService` owns `state.traffic` instead: it hands the table the gate reference and the own ICAO on every pass, as the Go's traffic service hands its own position and setting (Paired with a Go). Alone, the reference is Bravo's own fix and the own ICAO its own setting. It pops each `events::AdsbFrame` from the queue the radio thread fills, decodes it with `protocol::adsb_receive`, and counts a frame that fails parity in `invalid()`. It folds the rest into `protocol::AdsbAssembly` against Bravo's own position, with the frame's instant on `ports::Clock` and a stamp on the PPS edge. Each folded observation goes to `common::AdsbCapture::record` before the table, because #183 asks about every aircraft heard and the table's gate keeps only the near ones (`../common/README.md`). An observation with a position then goes to the table. An identification message's callsign goes to `state.callsigns`, keyed on the address, so a `$PFLAA` and the Go both read it there. `age_out` runs on the assembly every pass, as `core/protocol/README.md` requires, and on the table and the callsigns after it.

The queue, `bus::AdsbQueue`, lives in the product beside `bus::Bus`, because only Bravo hears 1090 MHz. The board's receiver pushes into it, from its own thread on silicon and from the board's poll on the host.

## Without a fix

The receiver and the decoder keep running. CPR still pairs an even and an odd position with no reference, so aircraft are folded and recorded. Nothing reaches the phone as `$PFLAA`, because `NmeaService` sends traffic only from a valid own position, and `$PFLAU` says GPS 0. The lamp shows no fix.

A fix that comes back does not make an old observation fresh. Each assembly field keeps the instant it was heard, and the traffic table dates a target by the second it was received. Before UTC that second is boot-relative, and the first fix moves the table onto UTC, so a target heard before the fix is forgotten rather than exported with a new age. It comes back on its next position.

## Settings

`settings.h` is Bravo's struct, sealed by `core/settings/blob.h` as blob version 1. It holds `own_icao`, `alarm_enabled`, `alarm_volume`, and `peer`: the Bluetooth address and type of the Go Bravo pinned, and whether one is pinned. Paired with a Go says when the pin is written and cleared. The field is in version 1 so the first image already carries it and needed no migration.

Bravo answers `config` and `defaults` with `alarm_enabled`, `alarm_volume` and `own_icao` only, and ignores every other key of a `set`, as the Go ignores `address`. `schemas/link.v1.json` says which product stores each key. A `set` on Bravo stands as a prompt like any other, and `describe_changes` writes no rows, because there is no glass to show them on.

Bravo stores no aircraft type, battery trim or frequency trim. Own-ship stamps ADS-L G.1.3 category 0, no emitter category, and the power service reads the divider untrimmed.

## Prompts without a screen

A phone's `set`, `dfu`, `recovery`, `power_off`, `gnss_cold_start`, `capture` or log erase stands as a prompt, as on the Go (`core/comms/README.md`). The lamp shows it as a fast green blink, and two presses inside `common::ConfirmGesture::kDoublePressWindowMs` allow it. One press refuses it, and thirty seconds expire it.

`common::PromptAnswer` (`../common/prompt_answer.h`) arms the gesture only once the lamp shows the prompt, the button is up, and it has been up for one double-press window since its last release and since the prompt arrived. A contact that began before the gesture armed is never spent on it, so a button already held when a phone asks cannot answer the prompt with its release and one more press. A prompt replaced by another of the same kind disarms the gesture as a new prompt does. A traffic advisory outranks the prompt on the lamp, so while one stands the gesture stays disarmed and a press dismisses the advisory.

A prompt is refused only while the flight state is `Airborne`, as on the Go (`core/comms/config.h`, `prompt_allowed_in`), and a take-off cancels a standing one. A Bravo with no fix since boot is `Unknown`, not `Airborne`, so a Bravo indoors can be updated over Bluetooth.

## The lamp

`core/indication` decides the lamp for both products. Bravo claims `common::Feature::LampShowsLink`, so its alarm service also tells the table whether a prompt stands and whether a phone is connected. The Go makes no such claim and its lamp is unchanged.

| State | Lamp |
|---|---|
| on | the condition below lights from the first pass of the loop |
| no fix | blue, one wink every 3 s |
| fix | green, one wink every 3 s |
| a phone connected | the same colour, one wink every 1.5 s |
| advisory | red, fast flicker |
| prompt | green, fast blink |
| a Go asks to pair | blue, fast blink |
| a capture armed | the fix colour, two winks every 3 s |
| low battery | red, one wink every 3 s, and a 600 ms blink when critical |
| refused to fly | red, a 600 ms blink, as a critical cell |
| off | dark |

`core/indication/README.md` holds the table, its priority order and what each row may spend.

## The button

| Press | Meaning |
|---|---|
| a press from off | on: the button is the wake pin |
| a press with an advisory standing | dismiss, as the Go's long touch does |
| two presses with a prompt on the lamp | allow the prompt |
| one press with a prompt on the lamp | refuse the prompt |
| one press with a Go's pairing request on the lamp | pair with that Go |
| one press while an upload window is open | close it, once the press that opened it has settled |
| a hold past `power::kLongPressHoldMs` | off |

The hold is the shutdown sequencer's, read off the pin, so it works as on the Go. A press is a release before the hold, so a hold never answers a prompt or a pairing request.

## Paired with a Go

A Go paired with a Bravo is a Tango. The Go's half is `products/skyblip_go/services/bravo.h` and `core/comms/README.md`, The Go's link to its Bravo. Bravo's half is `services/peer.h`, and three objects it drives: `Pairing` (`pairing.h`), `GateReference` (`gate_reference.h`) and `ObservationPacer` (`observation_pacer.h`).

### The gesture

A Go asks to pair with `peer_claim` frames whose `paired` is false. The first one on a connection stands as a pairing request for `Pairing::kRequestWindowMs`, 30 s, inside the Go's `kAnswerCeilingMs` of 35 s. One request stands at a time, and a connection asks once, so a Go left connected cannot hold the lamp. The request goes when its Go's connection goes. The lamp shows it as a fast blue blink, below an advisory and a phone's prompt and above the cell.

One press pairs. The press arms by the prompt's rule, read off the same `common::PromptAnswer` (`../common/README.md`): only once the lamp shows the request, with the button up for one double-press window since its last release and since the request arrived. A down that began at or before the instant it armed, dated by the debounce, disarms it. So a press begun before the request never pairs, an advisory takes the press to dismiss it, and a hold is the shutdown's. The gesture differs from the others by what stands on the lamp: dismiss needs an advisory, confirmation a prompt and two presses, and off a hold.

### The pin

Each side tells the other whether it holds the pin. A `peer_claim` says `paired` when the Go holds Bravo's, and every `health` says `paired` when Bravo holds, or offers, that Go's.

The press does not store Bravo's pin. It offers Bravo to that Go: the Go's `health` says `paired` true, and the Go pins Bravo on the first such `health` it reads, never on one that says false. The Go's next `peer_claim` carries `paired` true, and only that claim, from the offered address within `Pairing::kOfferWindowMs` of the press, stores Bravo's pin: the Go's whole address and type, in the settings blob. A paired Go reads `CONNECTED` only once a `health` on that connection says `paired` true.

A pairing interrupted on either side leaves no pin on one side alone for long:

- before the press, nothing is stored on either side;
- a `health` lost after the press leaves the Go unpinned. It refuses for no answer, and the offer expires. A Go that asks again on a new connection, before the offer expires, drops the offer and needs a new press;
- a Go that pinned and lost the link before Bravo read its confirmation reconnects. Within the offer window its first claim completes Bravo's half;
- after the offer window, or after Bravo restarted, the Go's first claim on the new connection is answered at once with `health` saying `paired` false. The Go drops its pin, frees its phone slot, never reads `CONNECTED`, and its glass shows `NOT PAIRED ON BRAVO`.

So a pin on the Go alone lasts until the Go next reaches Bravo, and is cleared on the first `health` after its first claim. A pin on Bravo alone needs the Go to restart inside its 3 s settings write after reading Bravo's `health`. Bravo cannot tell that Go from a paired Go switched off, so its pin stays until that Go asks again or another Go replaces it.

A Go that unpairs while connected says so: it writes one `peer_claim` with `paired` false and keeps the link until Bravo's `health` answers `paired` false, at most `BravoService::kFarewellCeilingMs`, 2 s. Bravo drops its pin and frees the kept connection on that claim. A Go unpaired while away from Bravo cannot say so, and Bravo keeps its pin until that Go asks it again or another Go replaces it. A farewell lost on the link ends the same way, since the Go drops the link after 2 s whether Bravo answered or not. A Go that asks to pair while Bravo holds its pin has unpaired, so Bravo drops its pin and shows the request. Another Go's request answered with a press replaces the pin, and the old Go's next claim is answered `paired` false, so it drops its own.

`PeerService::paired()` is true only while the pin is stored, which is after both sides confirmed. #179 grants the alarm only to that Go. Until then `health` says `alarm_granted` false.

### The connection

While a Go is pinned, `reserve_peer_connection_for()` keeps one of Bravo's three connections for that address (`core/comms/README.md`, Sessions). Phones get two, and a third phone is refused. Bravo keeps advertising while its Go is away, so the Go can find it, and refuses a phone that connects past the ceiling.

### What Bravo serves

`peer_claim` is read from any Go for its `paired` flag. Its own-ship and own ICAO are taken only from the pinned Go. A frame that is not a well-formed claim, or carries half a field, is refused whole and counted in `PeerService::refused()`, and its sender is not taken for a Go.

`health` goes once a second, `PeerService::kHealthPeriodMs`, whatever the traffic, to every Go that has written a valid claim on its connection, and at once to a Go new on a connection, to a Go that unpaired, and to a Go claiming a pin Bravo does not hold. Its `paired` is true only for the pinned Go and an offered one. It says whether the receiver listens, whether Bravo holds UTC, the last observation sequence, whether Bravo's own ICAO is set, and the gate reference.

`observation` goes to every Go that has written a `peer_claim` on its connection, each by `send_to()`, so a phone's narrow payload never refuses it. One target is one frame, from the traffic table, with its callsign and the second the callsign table heard it. A frame goes only when the table took news of the target or its callsign moved, at most once in `ObservationPacer::kTargetUpdateFloorMs`, 500 ms, so two a second. What Bravo hears in between is folded into the next. A frame carries each field's own instant, so a frame sent late, or after the link came back, never makes a target fresher. A callsign moves no time of the position. Each Go is owed each target on its own, so a Go whose link refuses a frame for now keeps it owed and gets it on a later pass, while a Go that took it waits for news and the floor.

### The gate reference

`GateReference` picks the position Bravo gates its exports on, to the Go and to the phone:

1. the pinned Go's own-ship from a `peer_claim` younger than `GateReference::kClaimMaxAgeMs`, 4 s, while its link is up;
2. else Bravo's own valid fix;
3. else nothing, and nothing is exported.

The position and the height are judged apart: a height older than the ceiling leaves a fresh position, with `model::Ownship::altitude_hae_valid` false, so the gate compares no GNSS height and stays horizontal unless the pressure altitude is fresh.

`health.export_reference` says which, whatever UTC says. Bravo exports observations only while it also holds UTC. A claim's age is its position age, plus the time from its `at_s` to Bravo's UTC when both hold UTC, plus the time since Bravo read it. Without a phase on either side it counts the claim from the start of its second and Bravo from the end of its own, so an unknown phase ages a claim. A claim written without UTC never gates. Each field of the Go's own-ship is valid on its own: a pressure altitude older than the ceiling leaves the reference with no pressure altitude, and Bravo's own barometer never stands in for the Go's. With no vertical reference the gate is horizontal only, so no target is dropped on a height it cannot compare.

The CPR reference is the same position, so a Bravo with no fix still decodes locally around its Go. The NMEA stream to a phone is unchanged: it is relative to Bravo's own fix, so a Bravo with no fix sends a phone no `$PFLAA`.

The own ICAO is the pinned Go's `own_icao` when its last claim carried one, else Bravo's own setting. The assembly and the table drop that address.

## The name and the USB identity

Bravo advertises `skyBlip Bravo 3F2A`, where `3F2A` is the low 16 bits of FICR `DEVICEADDR`. The name rule is the product's, `bravo::bluetooth_name` in `name.h`, handed to the shared config link as an `identity::NameRule`. The Go hands it `identity::bluetooth_name`, its activation rule. The tag is not an activation identity, and Bravo is never activated: its platform fills `ports::KeylessIdentity`, so `challenge` answers `keyless`.

USB is VID 0x1209 and PID 0x5AFE, with the product string `skyBlip Bravo` (`prj.conf`).

## The way down

A long press, a phone's `power_off`, a cutoff, an install and a recovery all go through `core/power`'s shutdown sequencer, as on the Go. Parking puts the LR2021 to sleep through its receiver, silences the buzzer and the motor, darkens the lamp, and puts the DRV2605 in standby. The settings are flushed and a running capture parks. The X1's platform then walks `core/power`'s power-down order on its own lines before SYSTEM OFF (`../../boards/seeed/meshtracker_x1/README.md`).

A receiver that never confirms its sleep is held in reset after `kReceiverParkCeilingMs`. That ceiling is the LR2021's worst sleep, a stuck standby and the reset after it, 4 s.

A cable plugged into a switched-off X1 wakes the SoC. Meshtastic's bootloader does not send that wake back to sleep, so this firmware does: `core/power/wake.h` refuses a VBUS wake from SYSTEM OFF with no button held. The boot path is decided before bring-up, from the reset causes, the button and the cell, which `begin_wake()` reads, so a refused wake starts no receiver, no link and no NVS. The board probes nothing until the wake is accepted, so a refused boot sends the LR2021 no SPI command and writes no haptic enable. It does hold the LR2021 in reset through its NRESET line, a GPIO write: after any reset of the SoC the radio's state is unknown, and a cold start, on a low cell or on a battery just connected, leaves it in standby. `power::boot_path()` refuses a cable wake from SYSTEM OFF, any boot on a cell under the lockout, and a restart on a flat cell, and all of them take the same road. `park_refusal()` then waits for the button and switches off, with NRESET still held low through SYSTEM OFF. What that costs against a cold sleep is #169's to measure. A press with the cable in boots.

The receiver reports itself parked only once the LR2021 is asleep or held in reset. A part whose probe failed, or whose bring-up never ran, has no receiver thread, so `sleep()` parks it directly: a SetSleep if the part takes one, and NRESET held low if not.

## A unit that refuses to fly

A Bravo whose receiver or GNSS does not answer runs no service, and with no service the lamp would stay dark while the SoC stays awake on the cell. So the product blinks the lamp's critical row itself, red every 600 ms, from the first pass. The cutoff still guards the cell, and a 2 s hold still switches the unit off. The console says which part is missing.

## Bring-up and the watchdog

`main.cpp` arms the watchdog before `setup()`, as on the Go. With BUSY stuck, the receiver's `begin()` blocks about 7 s (`../../hardware/parts/lr2021/README.md`). It runs inside `setup()`, before the loop's 5 s service supervision applies, and the software channel allows `CONFIG_TASK_WDT_MIN_TIMEOUT` less a second, 9 s, so a stuck radio leaves 2 s for the rest of the bring-up. A bring-up past that bites, and the boot after it comes up as a watchdog restart.

## When flash may be touched

Bravo keys no transmitter, so flash waits for nothing on air. It waits for the 1090 MHz queue instead: the loop drains it, and a pass spent on an erase is a pass the queue fills. `book_flash_beside_adsb` in `flash_window.h` books at most `kFlashPassCeilingMs` of flash work in a pass, half the queue's drain window, and a `static_assert` holds a sector claim to it. The erase cost it books is the T-Echo's MX25R figure until #169 times the X1's GD25Q64E.

## Captures

`AdsbService` calls `AdsbCapture::record` on every folded observation, so a capture armed on Bravo carries the ADS-B record #203 defined.

Bravo has no glass, so a page or `scripts/blip.py capture` arms one over the link (#247). The `capture` command names the profile, `full`, `power_run` or `flight_run`, or `stop`, and stands as a prompt like every other: two presses allow it, one refuses it, thirty seconds expire it, and it is refused while airborne. `ConfigService` refuses it at the door with `no_storage` when the flash holds no capture ring, `capturing` for an arm while a capture runs and `not_capturing` for a stop while none does, reading what `ConfigLinkService` hands it each pass from `state.capture` and the recorder. Two presses answer `capture` and latch the order. `Product::take_link_requests()` then arms `diagnostics::Recorder` with the profile, or disarms it, as the Go's glass does, and `CaptureService` drains the recorder as on the Go. While a capture is armed the lamp winks twice every 3 s in the fix colour, above the connected rows, so a pilot who armed it from a page sees it run.

A capture keeps running through a flight, and stopping it is refused in the air like every prompt. The power-off parks it as on the Go. The Go answers the same command `unknown_command`, because its captures are armed on its glass only (`../skyblip_go/README.md`, Which capture is armed). Only Bravo calls `ConfigService::serve_captures()`.

## The cell

`PowerService` reads Bravo's 1100 mAh high-voltage cell through the Go's LiPo curve and voltage ladder (`../../core/power/README.md`). No discharge curve of the X1's cell is published, so Bravo keeps the Go's values until #169 measures one from full to cutoff.

## Untested on silicon

The host suite runs every service above on the part models. On an X1 nothing has run. The bench (#169) checks, in this order:

- the image boots from the `.uf2` and enumerates as `1209:5AFE` `skyBlip Bravo`, with the inventory on the console;
- the lamp winks blue, then green once the AG3335 has a fix, and the button switches the unit off and on;
- the AG3335 acknowledges its configuration and its PPS edge reaches the platform;
- the LR2021 comes up, its version reads 1.24, and frames reach the `adsb` diagnostics group;
- XCSoar shows ADS-B traffic from Bravo with Bravo's own position;
- an advisory sounds the buzzer and the motor, and one press dismisses it;
- a `set` and a `dfu` from the manage page stand on the lamp and two presses allow them, and a Bluetooth update installs and confirms;
- a capture armed from the settings page winks the lamp twice every 3 s, records ADS-B through a flight, stops from the page, and `blip.py fetch --log diagnostics` reads it back;
- a switch-off reaches SYSTEM OFF with the off current measured, and a cable plugged in leaves it off;
- a Go pairs: its request blinks the lamp blue, one press pairs both sides, and both keep the pin across a power cycle of either;
- with two phones on Bravo and its Go away, a third phone is refused, and the Go reconnects;
- the Go's BRAVO page reads `CONNECTED`, and an aircraft Bravo hears reaches the Go as `observation` frames, at most two a second;
- endurance from full to cutoff, with the conditions, published as measured.
