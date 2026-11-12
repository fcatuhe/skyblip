# core/model

The nouns of the air, and nothing else: what an aircraft is, what own ship is, which half of the second a burst arrived in. No behaviour, no policy, no I/O.

These types are what the tree agrees on. A protocol decodes into one, a table stores one, a page draws one, a link publishes one, and none of the four owns the definition.

| File | Holds |
|---|---|
| `aircraft.h` | `AircraftObservation`, one aircraft as one reception saw it, and `Source`, which of the five ways it reached us |
| `ownship.h` | `Ownship`, this aircraft as the receiver and the barometer currently describe it |
| `band.h` | `Band`, the M and O halves the single radio is shared between |

`AircraftObservation` carries its `events::Stamp` rather than a pair of loose time fields, so the instant a position was heard is the same shape everywhere in the tree. `at_ms` beside it is a different fact: the instant the position was *true*, on `ports::Clock`, which is what the relative geometry aligns on.

The quantity suffixes (`latitude_1e7`, `speed_q`, `track_c9`) are the tree's convention and are listed in `firmware/README.md`. `core/units/` carries the same units as types, for the code that converts rather than transports.

## A field for each message ADS-B sends

`Source::Adsb` is 1090 MHz ADS-B, appended after the other four so that no code the diagnostics and flight logs already hold changes meaning. It is spelled `B` on `nearby` and `ADS-B` in the station log.

ADS-L, ALP-TAS and the uplink record send everything about an aircraft in one message. ADS-B splits it: the position, the velocity, the pressure altitude, the GNSS-baro difference, the identity and the operational status each arrive in a message of their own, at their own rate. So each field that a rule ages carries its own age:

| Fields | Age | What refreshes it on ADS-B |
|---|---|---|
| position (`latitude_1e7`, `longitude_1e7`) | `received` and `at_ms` themselves | an airborne or surface position |
| ground speed and track (`speed_q`, `track_c9`) | `speed_age_ms` | a velocity message of subtype 1 or 2, or a surface position |
| vertical rate (`climb_e8`) | `climb_age_ms` | a velocity message of any subtype that carries one |
| height above the ellipsoid (`altitude_m`) | `altitude_age_ms` | an airborne position with type code 20 to 22, or a derivation, below |
| pressure altitude (`pressure_altitude_ft`) | `pressure_altitude_age_ms` | an airborne position with type code 9 to 18 |
| GNSS-baro difference (`height_difference_ft`) | `height_difference_age_ms` | a velocity message that carries one, not saturated |
| `flight_state` | `flight_state_age_ms` | a surface or airborne position, an airborne velocity, or a DF17 capability of 4 (on the ground) or 5 (airborne) |

Speed and climb are two ages because ADS-B sends them apart. A velocity message of subtype 3 or 4 carries an airspeed and a heading with its vertical rate, and those are not a ground speed and a track, so it refreshes the climb and not the speed. A surface position carries a ground speed and a track and no vertical rate. The message layouts are The 1090 Megahertz Riddle's chapters "Airborne velocity" and "Surface position", and the capability codes its table "Mode S transponder capability (CA)". Capability 6 and 7 say either on the ground or airborne, so they refresh nothing. A velocity message says airborne because RTCA DO-260B (draft 4.2) Table 2-77 broadcasts it only in the air.

An age is how many milliseconds before `at_ms` the field was true, so the field's own instant on `ports::Clock` is `at_ms` minus its age. It is negative when the field was true after `at_ms`. It is zero when one message carried the field and the position, which is every source but ADS-B: none of them writes an age, and the zero a value-initialised observation holds is the right answer for them. A message that is not a position never moves `at_ms` or `received` once a position is held, so a velocity heard alone ages the position instead of refreshing it. An aircraft whose position is not valid yet, such as an ADS-B target with half a CPR pair, still has an `at_ms`: the instant the newest message folded into it was true. Its ages count from there, so every field it holds keeps its own instant, and the first position moves `at_ms` and every age with it.

`climb_source` says what measured `climb_e8`: `Gnss`, `Barometric`, or `Unknown`, which is what a value-initialised observation holds. ADS-B's velocity message carries the source in one bit (The 1090 Megahertz Riddle, "Airborne velocity", `VrSrc`). ADS-L, ALP-TAS and the uplink do not say, so they leave it `Unknown`. It describes `climb_e8` only while `climb_valid` is true, and means nothing otherwise.

`altitude_m` stays the height above the WGS-84 ellipsoid, the datum every other source and own-ship's `altitude_hae_mm` use. An ADS-B target sends a GNSS height directly in an airborne position message with type code 20 to 22, or it is derived from the pressure altitude plus the GNSS-baro difference that the velocity message carries. Whether that GNSS height is on the same datum is settled with flight data (#183), not here. A derived height is only as fresh as the older of its two halves, so its `altitude_age_ms` is that half's age, and `altitude_valid` is false unless both are valid. `pressure_altitude_ft` is the target's altitude on 1013.25 hPa, in the feet ADS-B encodes it in, valid only when `pressure_altitude_valid` says so.

`height_difference_ft` is the GNSS-baro difference itself, GNSS height less pressure altitude in the feet and 25 ft steps the velocity message carries, valid only when `height_difference_valid` says so. A derived `altitude_m` already holds it added to the pressure altitude, and a direct one hides it, so no rule reads it. It is on the observation for the capture's ADS-B record (`../diagnostics/README.md`), which #183 reads to settle which datum each installation's difference is on. It is valid while the assembly holds it, 10 s (`protocol::kHeightDifferenceMaxAgeMs`). The `peer` endpoint's `observation` frame (#177, `schemas/link.v1.json`) does not carry it, so a paired Go's observations never hold one.

The identity and the operational status carry a validity and no age, because no rule reads them by age:

- `aircraft_category` is G.1.3's, where 0 already means no category. `protocol::adsb_emitter_to_adsl_category` maps the ADS-B emitter category into it (`core/protocol/README.md`).
- `adsb_version` is the version a type code 31 message names, valid once one has been heard. Version 0 transmitters send no type code 31 at all (The 1090 Megahertz Riddle, "Identification of the ADS-B Version"), so a target not heard sending one yet may be version 0 or may only be early. Until one is heard the version is unknown, not 0.
- `gva` is the 2-bit geometric vertical accuracy code, valid once an airborne type code 31 from a version 2 transmitter has been heard. Versions 0 and 1 have no such field, and a version 2 surface status message has none either (The 1090 Megahertz Riddle, "Aircraft operation status").

A callsign is not an observation field. It goes to `traffic::CallsignTable` keyed on the address, as an ADS-L registration does (`core/protocol/README.md`).

`Ownship` carries the same pressure altitude for its own side: `pressure_altitude_mm`, in the millimetres its other altitudes use, with `pressure_altitude_valid` and the instant it was true, `pressure_altitude_at_ms`. It is what an ADS-B target that sends no GNSS height is compared with. `altitude_hae_valid` defaults to true, and `OwnshipService` leaves it so: it does not copy the receiver's own height validity, so own-ship's height is trusted wherever `fix_valid` is, though `fix_valid` comes from RMC and the height from GGA. Only Bravo sets it false, gating on a paired Go's claim whose height is older than its position (`products/skyblip_bravo/README.md`). A false one compares no GNSS height, so the vertical falls back to the pressure altitude or to none.

## What the ADS-B fields cost

The fields above grew `AircraftObservation` from 48 to 76 bytes and `Ownship` from 60 to 72, as `arm-zephyr-eabi-g++ -mcpu=cortex-m4` lays them out for the nRF52840. `traffic::TrafficTable` holds 48 observations, and measured 4896 bytes once they were in, and `bus::State`, which holds the table and own-ship, 7672. A 64-bit host build gives the same figures, except 7680 for `bus::State`.

The GNSS-baro difference grew `AircraftObservation` again, from 76 to 84 bytes: `height_difference_ft` fills the two bytes of padding after `track_c9`, and the age, the flag and the padding behind the flag cost the eight. `traffic::TrafficTable` grew from 4896 to 5280 bytes and `bus::State` from 7672 to 8056, measured with the same compiler and flags. A 64-bit host build gives the same figures, except 7680 and 8064 for `bus::State`.
