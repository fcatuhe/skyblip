# simulator

The simulator runs a real product on the host platform, inside a world that drives the part models. `simulator.h` flies a skyBlip Go on the T-Echo's models, `bravo_simulator.h` a skyBlip Bravo on the MeshTracker X1's (`hardware/platform/host/meshtracker_x1.h`), and `paired_simulator.h` both in one aircraft. The first two share `unit_simulator.h`, which steps one unit through its world. `frontends/browser.cpp` draws them and the host suite asserts on them. Neither adds behaviour.

## The world

`world/world.h` holds two classes:

- `Sky` is the aircraft and the bands they reach a unit on. ADS-L, ALP-TAS and the skyPost uplink go out on 868 MHz through `Air` into the Sx1262 model, as genuine frames, so the production receive path runs. ADS-B goes out on 1090 MHz into the LR2021 model (`world/world_adsb.cpp`).
- `World<Platform>` is a `Sky` carrying one unit: its GNSS, barometer, button, pad, cell and phone link. A Go hears 868 MHz and a Bravo 1090 MHz, so an aircraft with both systems is heard by each on its own band.

Each aircraft is placed north and east of where own-ship was when it was added, then flies over the ground on its own, so two manoeuvring aircraft describe one encounter. `Air` logs every burst, heard or not, and decides whether the radio hears one by where it was tuned when the burst started, so a slot-timing bug shows on the tape as `Deaf`. A step is `UnitSimulator::kStepMs`, 5 ms, finer than the guard on the O to M edge.

The Go's L76K is brought up as on a T-Echo: met at 9600, raised to 115200 and set to solve twice a second, on `.000` and `.500` of its UTC, which the host PPS puts on the clock's whole second. The host GNSS port retunes only once `Simulator::setup()` says a world ticks the receiver, because a product rig feeds fixes onto the bus instead. A case can set `models::L76k`'s latency after a solution, 0 unless it asks for the 70 ms SoftRF measured, read what it `solved()`, stall the wire, have it answer, refuse or hang on a sub-second `$PCAS02`, hang until RESET_N is pulsed, or lose UTC.

## A Go paired with a Bravo

`PairedSimulator` holds a `Simulator`, whose world is the one sky, and a Bravo with `world/bravo_aboard.h` beside it. Both boot already paired, as a Tango leaves the hangar: each settings blob on flash pins the other at `PairedLink`'s two addresses, and the Go's also sets `PairedSimulator::kAircraftAddress`.

`BravoAboard` puts Bravo in the Go's aircraft. Each step, after the Go's world:

- Bravo's AG3335 takes the Go's L76K truth, and keeps its own fix flag, so a scenario can take Bravo's fix alone.
- Its SPA06 reads the same air as the Go's BME280, through `World::feel_air`.
- The sky squitters to its LR2021, through `World::squitter_to`. A sky has one 1090 MHz receiver.
- `world/paired_link.h` carries the `peer` frames between Bravo's link and the Go's central. The host suite's `test/support/paired_link.h` re-exports it.

The paired Go is a Go+, with a buzzer and a motor (`platform::host::Platform::kFullyFitted`). Bravo's phone takes the session after the Go's. `BravoAboard::set_link(false)` drops the connection at once, as a supervision timeout ends it, and no reconnect comes up until `set_link(true)`.

## ADS-B Out

`world/adsb_out.h` builds what an aircraft's ADS-B Out transmits: sealed DF17 frames, their chips and the preamble the LR2021 detects. skyBlip never transmits ADS-B, so this half lives in the world, not in `core/protocol`. The host suite's `test/support/adsb_frames.h` and `lr2021_rig.h` re-export it.

| Message | Airborne | On the ground, moving | On the ground, still |
|---|---|---|---|
| position, even and odd in turn | 2 Hz, TC 11 | 2 Hz, TC 7 | 0.2 Hz, TC 7 |
| airborne velocity, TC 19 subtype 1 | 2 Hz | none | none |
| identification, with its emitter category | 0.2 Hz | 0.2 Hz | 0.1 Hz |

The rates are The 1090MHz Riddle (Junzi Sun, 2nd edition), Table 1.4, for ADS-B version 2. The capability field is 5 airborne and 4 on the ground, as `protocols/adsb/assembly.cpp` reads it. Each aircraft picks its phase from its address, and a velocity message falls half a period after a position.

Airborne, the position carries the pressure altitude the world's airmass gives that height, and the velocity message carries the GNSS-baro difference. An aircraft with `pressure_only` leaves that difference at 0, which the decoder reads as none sent (`protocols/adsb/README.md`). A surface position carries no altitude.

The world does not model:

- **path loss at 1090 MHz:** every squitter lands at -70 dBm;
- **garbling:** two squitters that overlap are both heard;
- **DF18:** Bravo receives DF17 only (`protocols/adsb/README.md`);
- **aircraft status, target state and operational status,** TC 28, 29 and 31, so Bravo never learns an aircraft's ADS-B version or GVA.

## ALP-TAS Out

`world/alptas_out.h` builds the ALP-TAS frame an aircraft sends. skyBlip only receives ALP-TAS, so the encoder lives in the world, not in `core/protocol`. It sends the accuracy codes SoftRF's `latest_encode` sends, and the host suite and `test/fuzz/fuzz_air.cpp` build their frames with it too.

`world/hex.h` reads and writes the hex the link text prints and the test vectors carry. No device prints hex.

## Scenarios

A scenario in `../scenarios` is one JSON file, read by `world/scenario.cpp`, that the browser and the suite both load, so a bug found in flight becomes a committed fixture. `scripts/simulator_scenarios.rb` bundles every file for the browser.

- `"product": "bravo"` flies a Bravo, and `"product": "paired"` a Go paired with a Bravo. A file without it flies a Go.
- An aircraft's `"system"` is `adsl_direct`, `alptas`, `adsl_uplink`, or `adsb` for ADS-B Out alone.
- `"adsb": 1` adds ADS-B Out to an 868 MHz system. Such an aircraft sends its ICAO address under table 5 on ADS-L too, as ADS-L F.2.3 asks of a unit beside a transponder.
- `"on_ground": 1` sends surface positions, and `"pressure_only": 1` sends no GNSS-baro difference.
- `"category"` is the emitter category, `A0` to `C7`, in the shorthand of `core/protocol/README.md`: the letter is the identification's type code, A for 4, B for 3 and C for 2, and the digit is the category. Any other value refuses the file.
- `"phase_ms"` and `"slot"` pin where in the second, and on which M-band channel, an aircraft transmits. Left out, it picks its own instant as a conforming transmitter does.
- The event `"companion": 1` connects a phone, and `0` drops it. Bravo sends its FLARM NMEA only to a connected phone.
- The event `"pps": 0` stops the pulse and `1` brings it back. The Go stops transmitting 1.2 s after the last edge, on the first second without one, shows `NO TX PPS` 10 s later, and keeps hearing traffic on the held edge through the 60 s holdover (`pps_loss.json`, `../core/timing/README.md`, A burst is timed only from a fresh edge). The pulse is the only fault a scenario forces. The suite forces the others from the part models.
- In a paired file, an event with `"unit": "bravo"` acts on Bravo, which takes `fix` and `companion`, and `"bravo_link": 0` or `1` loses or restores the link between them. A file that uses either outside a pair, or gives Bravo another event, is refused.

## The browser

`make -C firmware browser` writes the four files every release carries for the browser into `build/browser/`. They are named `skyblip-simulator*` because the one module flies every product.

| File | What it is |
|---|---|
| `skyblip-simulator.wasm` | the firmware and its world, compiled by Emscripten from `frontends/browser.cpp` |
| `skyblip-simulator.js` | the loader Emscripten generates for the WASM |
| `skyblip-simulator-embed.js` | `web/embed.js`, the module the pages and `server/bin/screenshots` import: it loads the WASM and names its exports |
| `skyblip-simulator-scenarios.json` | every `../scenarios/*.json` in one file, bundled by `scripts/simulator_scenarios.rb` |

`frontends/browser.cpp` builds one WASM module with a Go, a Bravo and a pair. Loading a scenario picks the unit it names, and every shared export reads that one. The Go's glass, its 868 MHz tape and its radio counters read the Go alone or the paired Go. `simulator_product`, `simulator_nmea_count`, `simulator_nmea_line`, `simulator_adsb_heard`, `simulator_adsb_aircraft` and `simulator_bravo_lamp` give the workbench what Bravo has instead of a glass: its lamp and the stream a phone EFB reads. Paired, `simulator_bravo_state` says what the Go makes of Bravo, `simulator_bravo_follows` whether Bravo plays the Go's calls, and `simulator_set_bravo_link` loses the link or brings it back. `scripts/check_simulator_exports.py` checks that `web/embed.js` binds only exported names.

`simulator_draw_prompt` draws the Go's confirm page for the prompt a phone raised, so the briefing shows the question as the glass shows it, with no copy of the page in HTML. `world/prompt_glass.cpp` names each prompt the way the briefing names its task: `set`, `dfu`, `recovery`, `power_off`, `erase_log`, `gnss_cold_start`, `register`, `capture` or `pair`. It words a `dfu`, a `pair` and a `set` with the functions the Go words its own prompts with, and every other prompt takes the Go's own title and detail. `web/embed.js` paints it with `paintPrompt`, which returns the page's words in reading order for the canvas's label, since a screen reader cannot read pixels, or null for a prompt the Go never raises.
