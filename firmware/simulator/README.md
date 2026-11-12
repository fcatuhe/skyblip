# simulator

The simulator runs a real product on the host platform, inside a world that drives the part models. `simulator.h` flies a skyBlip Go on the T-Echo's models, and `bravo_simulator.h` flies a skyBlip Bravo on the MeshTracker X1's (`hardware/platform/host/meshtracker_x1.h`). `frontends/terminal.cpp` and `frontends/browser.cpp` draw them, and the host suite asserts on them. None of them adds behaviour.

## The world

`world/world.h` holds two classes:

- `Sky` is the aircraft and the bands they reach a unit on. ADS-L, ALP-TAS and the skyPost uplink go out on 868 MHz through `Air` into the Sx1262 model. ADS-B goes out on 1090 MHz into the LR2021 model (`world/world_adsb.cpp`).
- `World<Platform>` is a `Sky` carrying one unit: its GNSS, barometer, button, pad, cell and phone link. A Go hears 868 MHz and a Bravo hears 1090 MHz, so an aircraft with both systems is heard by each on its own band.

Each aircraft is placed north and east of where own-ship was when it was added, then flies over the ground on its own.

## ADS-B Out

`world/adsb_out.h` builds what an aircraft's ADS-B Out transmits: sealed DF17 frames, their chips, and the preamble the LR2021 detects. skyBlip never transmits ADS-B, so this half lives in the world, not in `core/protocol`. The host suite's `test/support/adsb_frames.h` and `lr2021_rig.h` re-export it.

An aircraft with ADS-B Out sends:

| Message | Airborne | On the ground, moving | On the ground, still |
|---|---|---|---|
| position, even and odd in turn | 2 Hz, TC 11 | 2 Hz, TC 7 | 0.2 Hz, TC 7 |
| airborne velocity, TC 19 subtype 1 | 2 Hz | none | none |
| identification, with its emitter category | 0.2 Hz | 0.2 Hz | 0.1 Hz |

The rates are The 1090MHz Riddle (Junzi Sun, 2nd edition), Table 1.4, for ADS-B version 2. The capability field is 5 airborne and 4 on the ground, the values `core/protocol/adsb_assembly.cpp` reads as flight state. Each aircraft picks its phase in the period from its address, and a velocity message falls half a period after a position.

Airborne, the position carries the pressure altitude the world's airmass gives that height, and the velocity message carries the GNSS-baro difference, so a receiver gets the GNSS height (HAE) too. An aircraft with `pressure_only` leaves that difference at 0, which the decoder reads as none sent (`core/protocol/README.md`, after RTCA 1090-WP-14-09R1, Figures A-6 and A-7). A surface position carries no altitude.

The world does not model:

- **path loss at 1090 MHz:** every squitter lands at -70 dBm, the level the host suite hears at;
- **garbling:** two squitters that overlap are both heard;
- **DF18:** Bravo receives DF17 only (`core/protocol/README.md`).
- **aircraft status, target state and operational status,** TC 28, 29 and 31: none is sent, so Bravo never learns an aircraft's ADS-B version or GVA.

## Scenarios

A scenario in `../scenarios` is one JSON file, read by `world/scenario.cpp`, that the terminal, the browser and the suite all load. `scripts/simulator_scenarios.rb` bundles every file for the browser.

- `"product": "bravo"` flies a Bravo. A file without it flies a Go.
- An aircraft's `"system"` is `adsl_direct`, `alptas`, `adsl_uplink`, or `adsb` for ADS-B Out alone.
- `"adsb": 1` adds ADS-B Out to an 868 MHz system. Such an aircraft sends its ICAO address under table 5 on ADS-L too, as ADS-L F.2.3 asks of a unit beside a transponder.
- `"on_ground": 1` sends surface positions, and `"pressure_only": 1` sends no GNSS-baro difference.
- `"category"` is the emitter category, `A0` to `C7`, in `core/protocol/README.md`'s shorthand: the letter is the identification message's type code, A for 4, B for 3 and C for 2, and the digit is the category. The type code and category pairs are The 1090MHz Riddle's wake vortex table, which prints no letters. Any other value refuses the file.
- The event `"companion": 1` connects a phone, and `0` drops it. Bravo sends its FLARM NMEA only to a connected phone.

## The browser

`frontends/browser.cpp` builds one WASM module with both products. Loading a scenario picks the product it names, and every export that both products share reads that one. The Go's glass, its 868 MHz tape and its radio counters stay the Go's. `simulator_product`, `simulator_nmea_count`, `simulator_nmea_line`, `simulator_adsb_heard` and `simulator_adsb_aircraft` give the workbench what Bravo has instead of a glass: the stream a phone EFB reads. `scripts/check_simulator_exports.py` checks that `web/embed.js` binds only exported names.

A paired Go and Bravo, with ADS-B on the Go's glass, is #196.
