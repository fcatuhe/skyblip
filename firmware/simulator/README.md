# simulator

The simulator runs a real product on the host platform, inside a world that drives the part models. `simulator.h` flies a skyBlip Go on the T-Echo's models, and `bravo_simulator.h` flies a skyBlip Bravo on the MeshTracker X1's (`hardware/platform/host/meshtracker_x1.h`). `paired_simulator.h` flies both in one aircraft, a Go paired with a Bravo. `frontends/terminal.cpp` and `frontends/browser.cpp` draw them, and the host suite asserts on them. None of them adds behaviour.

## The world

`world/world.h` holds two classes:

- `Sky` is the aircraft and the bands they reach a unit on. ADS-L, ALP-TAS and the skyPost uplink go out on 868 MHz through `Air` into the Sx1262 model. ADS-B goes out on 1090 MHz into the LR2021 model (`world/world_adsb.cpp`).
- `World<Platform>` is a `Sky` carrying one unit: its GNSS, barometer, button, pad, cell and phone link. A Go hears 868 MHz and a Bravo hears 1090 MHz, so an aircraft with both systems is heard by each on its own band.

Each aircraft is placed north and east of where own-ship was when it was added, then flies over the ground on its own.

## A Go paired with a Bravo

`PairedSimulator` holds a `Simulator`, whose world is the one sky, and a Bravo with `world/bravo_aboard.h` beside it. Both boot already paired, as a Tango leaves the hangar: before `setup()`, each settings blob on flash pins the other, at `PairedLink`'s two addresses. The Go's blob also sets `PairedSimulator::kOwnIcao`, because a pilot flying a Tango sets one, and without it the Go shows the own-ICAO notice once a boot (#191).

`BravoAboard` puts Bravo in the Go's aircraft. Each step, after the Go's world:

- Bravo's AG3335 takes the Go's L76K truth: the same place, height, motion and UTC. Its fix flag stays its own, so a scenario can take Bravo's fix alone.
- Its SPA06 reads the same air as the Go's BME280, through `World::feel_air`.
- The sky squitters to its LR2021, through `World::squitter_to`. A sky has one 1090 MHz receiver, and a Go never listens on that band.
- `world/paired_link.h` carries the `peer` frames between Bravo's link and the Go's central. The host suite's `test/support/paired_link.h` re-exports it.

The paired Go is a Go+: its host platform fits a buzzer and a motor (`platform::host::Platform::kFullyFitted`). Once it has a fix and reads Bravo `Live`, its claims take the alarm, and Bravo keeps its buzzer and motor silent while it holds such a claim from its connected Go (#179, `products/skyblip_bravo/README.md`). `paired.json` flies a converging ADS-B aircraft that both units hear: they both sound for at most one claim period after the Go reads Bravo `Live`, then only the Go+ sounds. A second ADS-B aircraft reaches advisory after that, and only the Go+ sounds for it. `paired_link_lost.json` keeps an ADS-B aircraft in advisory, so Bravo sounds again from its own settings on the pass the link drops, and keeps silent once the link is back and the next claim arrives. A second ADS-B aircraft reaches advisory after the link is back, and only the Go+ sounds for it.

From boot until Bravo's first `health` with UTC, the Go reads Bravo `Waiting` and its radar says `BRAVO WAITING`, with no `BRAVO LOST` tone (`../products/skyblip_go/pages/README.md`). `paired_bravo_without_fix.json` takes Bravo's fix alone. The AG3335 model still sends the time and date without a fix, so Bravo keeps UTC and the Go keeps reading it `Live` and showing its targets under `B`, with no `BRAVO WAITING` or `BRAVO LOST`. Bravo places its traffic against the position in the Go's claims, and its own phone gets no `$PFLAA` and a `$PFLAU` with GPS 0.

Bravo's phone, when a scenario connects one, takes the session after the Go's. `BravoAboard::set_link(false)` drops the connection at once, as a supervision timeout ends it, and no reconnect comes up until `set_link(true)`.

## ADS-B Out

`world/adsb_out.h` builds what an aircraft's ADS-B Out transmits: sealed DF17 frames, their chips, and the preamble the LR2021 detects. skyBlip never transmits ADS-B, so this half lives in the world, not in `core/protocol`. The host suite's `test/support/adsb_frames.h` and `lr2021_rig.h` re-export it.

An aircraft with ADS-B Out sends:

| Message | Airborne | On the ground, moving | On the ground, still |
|---|---|---|---|
| position, even and odd in turn | 2 Hz, TC 11 | 2 Hz, TC 7 | 0.2 Hz, TC 7 |
| airborne velocity, TC 19 subtype 1 | 2 Hz | none | none |
| identification, with its emitter category | 0.2 Hz | 0.2 Hz | 0.1 Hz |

The rates are The 1090MHz Riddle (Junzi Sun, 2nd edition), Table 1.4, for ADS-B version 2. The capability field is 5 airborne and 4 on the ground, the values `protocols/adsb/assembly.cpp` reads as on the ground or airborne. Each aircraft picks its phase in the period from its address, and a velocity message falls half a period after a position.

Airborne, the position carries the pressure altitude the world's airmass gives that height, and the velocity message carries the GNSS-baro difference, so a receiver gets the GNSS height (HAE) too. An aircraft with `pressure_only` leaves that difference at 0, which the decoder reads as none sent (`protocols/adsb/README.md`, after RTCA 1090-WP-14-09R1, Figures A-6 and A-7). A surface position carries no altitude.

The world does not model:

- **path loss at 1090 MHz:** every squitter lands at -70 dBm, the level the host suite hears at;
- **garbling:** two squitters that overlap are both heard;
- **DF18:** Bravo receives DF17 only (`protocols/adsb/README.md`).
- **aircraft status, target state and operational status,** TC 28, 29 and 31: none is sent, so Bravo never learns an aircraft's ADS-B version or GVA.

## Scenarios

A scenario in `../scenarios` is one JSON file, read by `world/scenario.cpp`, that the terminal, the browser and the suite all load. `scripts/simulator_scenarios.rb` bundles every file for the browser.

- `"product": "bravo"` flies a Bravo, and `"product": "paired"` a Go paired with a Bravo. A file without it flies a Go.
- An aircraft's `"system"` is `adsl_direct`, `alptas`, `adsl_uplink`, or `adsb` for ADS-B Out alone.
- `"adsb": 1` adds ADS-B Out to an 868 MHz system. Such an aircraft sends its ICAO address under table 5 on ADS-L too, as ADS-L F.2.3 asks of a unit beside a transponder.
- `"on_ground": 1` sends surface positions, and `"pressure_only": 1` sends no GNSS-baro difference.
- `"category"` is the emitter category, `A0` to `C7`, in the shorthand `core/protocol/README.md` uses under The category ADS-B sends: the letter is the identification message's type code, A for 4, B for 3 and C for 2, and the digit is the category. The type code and category pairs are The 1090MHz Riddle's wake vortex table, which prints no letters. Any other value refuses the file.
- The event `"companion": 1` connects a phone, and `0` drops it. Bravo sends its FLARM NMEA only to a connected phone.
- In a paired file, an event with `"unit": "bravo"` acts on Bravo instead of the Go. Bravo aboard takes `fix` and `companion`. The event `"bravo_link": 0` loses the link between them, and `1` brings it back. A file that uses either outside a pair, or gives Bravo another event, is refused.

## The browser

`frontends/browser.cpp` builds one WASM module with a Go, a Bravo and a pair. Loading a scenario picks the unit it names, and every export that the units share reads that one. The Go's glass, its 868 MHz tape and its radio counters read the Go alone or the paired Go. `simulator_product`, `simulator_nmea_count`, `simulator_nmea_line`, `simulator_adsb_heard`, `simulator_adsb_aircraft` and `simulator_bravo_lamp` give the workbench what Bravo has instead of a glass: its lamp, and the stream a phone EFB reads. Paired, the page shows the Go's glass with Bravo's lamp and stream beside it, `simulator_bravo_state` says what the Go makes of Bravo, `simulator_bravo_silent` whether Bravo keeps silent for the Go+, and `simulator_set_bravo_link` loses the link or brings it back. An aircraft added while a pair flies may carry ADS-B Out alone, heard by Bravo only. `scripts/check_simulator_exports.py` checks that `web/embed.js` binds only exported names.
