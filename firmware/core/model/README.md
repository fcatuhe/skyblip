# core/model

The nouns of the air, with no behaviour, policy or I/O. A protocol decodes into these types, a table stores them, a page draws them and a link publishes them, and none of the four owns the definition.

| File | Holds |
|---|---|
| `aircraft.h` | `AircraftObservation`, one aircraft as one reception saw it, and `Source`, which of the five ways it reached us |
| `ownship.h` | `Ownship`, this aircraft as the receiver and the barometer currently describe it |
| `band.h` | `Band`, the M and O halves the single radio is shared between: two 200 kHz channels on M (ADS-L SRD-860 issue 2 clause C.2) and one HDR channel on O (clause C.4) |

`received` is the `events::Stamp` of the instant a position was heard. `at_ms` is a different fact: the instant the position was true, on `ports::Clock`, which the relative geometry aligns on.

The quantity suffixes (`latitude_1e7`, `speed_q`, `track_c9`) are listed in `firmware/README.md`. `core/units/` carries the same units as types, for code that converts rather than transports.

## A field for each message ADS-B sends

`Source::Adsb` is 1090 MHz ADS-B. It comes after the other four so that no code already held in diagnostics and flight logs changes meaning.

ADS-L, ALP-TAS and the uplink send everything about an aircraft in one message. ADS-B sends the position, the velocity, the pressure altitude, the GNSS-baro difference, the identity and the operational status in separate messages, at separate rates. So each field that a rule ages carries its own age:

| Fields | Age | What refreshes it on ADS-B |
|---|---|---|
| position (`latitude_1e7`, `longitude_1e7`) | `received` and `at_ms` themselves | an airborne or surface position |
| ground speed and track (`speed_q`, `track_c9`) | `speed_age_ms` | a velocity message of subtype 1 or 2, or a surface position |
| vertical rate (`climb_e8`) | `climb_age_ms` | a velocity message of any subtype that carries one |
| height above the ellipsoid (`altitude_m`) | `altitude_age_ms` | an airborne position with type code 20 to 22, or a derivation, below |
| pressure altitude (`pressure_altitude_ft`) | `pressure_altitude_age_ms` | an airborne position with type code 9 to 18 |
| GNSS-baro difference (`height_difference_ft`) | `height_difference_age_ms` | a velocity message that carries one, not saturated |
| `flight_state` | `flight_state_age_ms` | a surface or airborne position, an airborne velocity, or a DF17 capability of 4 (on the ground) or 5 (airborne) |

A velocity message of subtype 3 or 4 carries an airspeed and a heading, not a ground speed and a track, so it refreshes the climb and not the speed. Capability 6 and 7 mean either on the ground or airborne, so they refresh nothing. A velocity message means airborne because RTCA DO-260B (draft 4.2) Table 2-77 broadcasts it only in the air. The message layouts are in The 1090 Megahertz Riddle, chapters "Airborne velocity" and "Surface position", and the capability codes in its table "Mode S transponder capability (CA)".

An age is how many milliseconds before `at_ms` the field was true, so the field's own instant is `at_ms` minus its age. It is negative when the field was true after `at_ms`. It is zero when one message carried the field and the position, which covers every source but ADS-B. A message that is not a position never moves `at_ms` or `received` once a position is held, so a velocity heard alone ages the position. An aircraft with no valid position yet, such as an ADS-B target with half a CPR pair, still has an `at_ms`: the instant the newest message folded into it was true. The first position moves `at_ms` and every age with it.

`climb_source` says what measured `climb_e8`, and means something only while `climb_valid` is true. ADS-B carries it in the velocity message's `VrSrc` bit. The other sources leave it `Unknown`.

`altitude_m` is height above the WGS-84 ellipsoid, the datum of every other source and of own-ship's `altitude_hae_mm`. ADS-B sends it directly in an airborne position with type code 20 to 22, or it is derived as the pressure altitude plus the GNSS-baro difference. Flight data settles whether that height is on the same datum (#183). A derived height is as old as the older of its two halves, and valid only when both are. `pressure_altitude_ft` is the altitude on 1013.25 hPa, in the feet ADS-B encodes.

`height_difference_ft` is GNSS height less pressure altitude, in 25 ft steps. No rule reads it: a derived `altitude_m` already includes it. It exists for the capture's ADS-B record (`../diagnostics/README.md`), which #183 reads to settle the datum. It stays valid for 10 s (`protocol::kHeightDifferenceMaxAgeMs`). The `peer` endpoint's `observation` frame (`schemas/link.v1.json`) does not carry it, so a paired Go's observations never hold one.

The identity and the operational status carry a validity and no age, because no rule reads them by age:

- `aircraft_category` is G.1.3's, where 0 means no category. `protocol::adsb_emitter_to_adsl_category` maps the ADS-B emitter category into it (`core/protocol/README.md`).
- `adsb_version` is the version a type code 31 message names, valid once one is heard. Version 0 transmitters send no type code 31 (The 1090 Megahertz Riddle, "Identification of the ADS-B Version"), so until one is heard the version is unknown, not 0.
- `gva` is the 2-bit geometric vertical accuracy code, valid once an airborne type code 31 from a version 2 transmitter is heard. Versions 0 and 1 have no such field, nor does a version 2 surface status (The 1090 Megahertz Riddle, "Aircraft operation status").

A callsign is not an observation field. It goes to `traffic::CallsignTable` keyed on the address, as an ADS-L registration does (`core/protocol/README.md`).

`Ownship` carries `pressure_altitude_mm`, with `pressure_altitude_valid` and `pressure_altitude_at_ms`, to compare with an ADS-B target that sends no GNSS height. `altitude_hae_valid` defaults to true and `OwnshipService` never clears it, so own-ship's height is trusted wherever `fix_valid` is, though `fix_valid` comes from RMC and the height from GGA. Only Bravo clears it, when its reference is a paired Go's claim whose height is older than its position (`products/skyblip_bravo/README.md`). Then no GNSS height is compared, and the vertical falls back to the pressure altitude or to none.
