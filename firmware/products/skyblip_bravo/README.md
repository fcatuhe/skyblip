# products/skyblip_bravo

skyBlip Bravo: a Seeed SenseCAP MeshTracker X1 that receives 1090 MHz ADS-B full time and feeds one phone EFB over FLARM NMEA, alarming on its own buzzer and motor. It transmits nothing. `product.h` wires it over `boards/seeed/meshtracker_x1/board.h`, and `main.cpp` is the shell on silicon. The host suite runs the same product on the X1's part models (`hardware/platform/host/meshtracker_x1.h`).

Nothing here has run on an X1. The units have not arrived, so every pin, timing and part behaviour below the product is unverified until #169 benches one, and the bench list is at the end.

## The services

`products/common/README.md` says what a service is, and the Go's README says what each shared one does. Bravo steps nine, in this order:

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

`ownship` runs before `adsb`, so a frame is folded against this pass's fix. `alarm` runs before `button`, so a press is read against the lamp the pilot is looking at.

Bravo runs no `TrafficService`. 1090 MHz is its only source, so `AdsbService` owns `state.traffic` instead: it hands the table its own position and the own-ICAO setting on every pass, as the Go's traffic service does. It pops each `events::AdsbFrame` from the queue the radio thread fills, decodes it with `protocol::adsb_receive`, and counts a frame that fails parity in `invalid()`. It folds the rest into `protocol::AdsbAssembly` against Bravo's own position, with the frame's instant on `ports::Clock` and a stamp on the PPS edge. Each folded observation goes to `common::AdsbCapture::record` before the table, because #183 asks about every aircraft heard and the table's gate keeps only the near ones (`../common/README.md`). An observation with a position then goes to the table. `age_out` runs on the assembly every pass, as `core/protocol/README.md` requires, and on the table and the callsigns after it.

The queue, `bus::AdsbQueue`, lives in the product beside `bus::Bus`, because only Bravo hears 1090 MHz. The board's receiver pushes into it, from its own thread on silicon and from the board's poll on the host.

## Without a fix

The receiver and the decoder keep running. CPR still pairs an even and an odd position with no reference, so aircraft are folded and recorded. Nothing reaches the phone as `$PFLAA`, because `NmeaService` sends traffic only from a valid own position, and `$PFLAU` says GPS 0. The lamp shows no fix.

A fix that comes back does not make an old observation fresh. Each assembly field keeps the instant it was heard, and the traffic table dates a target by the second it was received. Before UTC that second is boot-relative, and the first fix moves the table onto UTC, so a target heard before the fix is forgotten rather than exported with a new age. It comes back on its next position.

## Settings

`settings.h` is Bravo's struct, sealed by `core/settings/blob.h` as blob version 1. It holds `own_icao`, `alarm_enabled`, `alarm_volume`, and `peer`: the Bluetooth address and type of the Go #192 pins, and whether one is pinned. #192 decides when the pin is written, cleared and read. The field is in version 1 so the first image already carries it and #192 needs no migration.

Bravo answers `config` and `defaults` with `alarm_enabled`, `alarm_volume` and `own_icao` only, and ignores every other key of a `set`, as the Go ignores `address`. `schemas/link.v1.json` says which product stores each key. A `set` on Bravo stands as a prompt like any other, and `describe_changes` writes no rows, because there is no glass to show them on.

Bravo stores no aircraft type, battery trim or frequency trim. Own-ship stamps ADS-L G.1.3 category 0, no emitter category, and the power service reads the divider untrimmed.

## Prompts without a screen

A phone's `set`, `dfu`, `recovery`, `power_off`, `gnss_cold_start` or log erase stands as a prompt, as on the Go (`core/comms/README.md`). The lamp shows it as a fast green blink, and two presses inside `common::ConfirmGesture::kDoublePressWindowMs` allow it. One press refuses it, and thirty seconds expire it.

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
| one press while an upload window is open | close it, once the press that opened it has settled |
| a hold past `power::kLongPressHoldMs` | off |

The hold is the shutdown sequencer's, read off the pin, so it works as on the Go. A press is a release before the hold, so a hold never answers a prompt.

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

`AdsbService` calls `AdsbCapture::record` on every folded observation, so a capture armed on Bravo carries the ADS-B record #203 defined. Nothing can arm a capture on Bravo yet: the Go arms one on its glass and refuses to over Bluetooth, and Bravo has neither a glass nor a gesture for it. How a Bravo capture is armed is open in #176.

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
- a switch-off reaches SYSTEM OFF with the off current measured, and a cable plugged in leaves it off;
- endurance from full to cutoff, with the conditions, published as measured.
