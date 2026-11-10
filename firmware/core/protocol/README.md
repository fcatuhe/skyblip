# core/protocol

The wire formats, in both directions and with no I/O: `adsl.{h,cpp}` and `adsl_uplink.{h,cpp}` are the ADS-L 4 SRD-860 packet the radio carries, `air.{h,cpp}` the shared air-frame plumbing, `alptas.{h,cpp}` and `nmea_out.{h,cpp}` what a tablet reads over the companion link, `adsb_category.{h,cpp}` what an ADS-B emitter category is in ADS-L's terms.

Both air frames are read and written field by field. A field is a `BitField`: an offset and a width counted from bit 0 of the first byte. ADS-L B.3 numbers bits in that order, and the ALP-TAS frame shares it. So `adsl.h` and `alptas.cpp` hold tables of offsets rather than shift sequences, and the ADS-L offsets are the ones G.1 prints. The variable-resolution fields are an `Exponential`: the encoding of ADS-L G.1.6, which the ALP-TAS frame also uses with other widths. An ADS-L field reads back the middle of the step its code names, and an ALP-TAS field the bottom of it.

`from_own` and `from_own_callsign` take the address as `settings::AirAddress`, and answer false with the packet untouched when there is none: a unit the server has not activated builds no frame, whatever the layers above it decided (`core/settings/README.md`).

## The name ADS-L has no field for

An iConspicuity payload carries no callsign, so a target on the glass is six hex digits and a pilot's radio call is not. The name comes from the one payload the specification hands to somebody else: F.2.1 assigns value 66 to "OGN Diagnostics" and footnotes it "payload definition is outside the scope of this document", which makes the layout Jalocha's (`oss/nrf52-ogn-tracker/src/adsl.h:120-128`) and ours to follow rather than to invent.

`from_own_callsign` writes the one record of it we speak. Type 66, the same 6+24 sender address our position frames carry, then a header byte of telemetry type 1 and info type 5 (OGN's `TelemType` and `InfoType`), then 14 characters. Everything around it is the frame we already build: the same sync word, Manchester coding, scramble and CRC24, which is why the radio needed no new dwell and the receiver no new configuration.

`callsign_of` is the other direction, and it is a trust boundary: text off the air is refused whole when any byte of it is not a callsign character, rather than drawn with a control character in it or spliced into a `$PFLAA` ID field as a comma. `is_callsign_character` is the set the radar menu types (blank, dash, `A`-`Z`, `0`-`9`), and the pilot's own callsign is held to the same one. What it returns goes to `core/traffic/callsigns.h` keyed on the sender's table and address, never into an `AircraftObservation` - a name is an attribute of an address, a position is an observation, and the two have different lifetimes.

An OGN tracker reads what we send: `ProcessRxADSL` takes any telemetry frame, pulls `getInfo(Call, 5)` out of it and hangs the name on the target of that address (`oss/nrf52-ogn-tracker/src/proc.cpp:715-733`), with no opinion about our AMT 58. It transmits the same record itself every 40 to 60 seconds, hopping between four channels, so we hear one of theirs perhaps once in four.

## Where the ALP-TAS frame comes from

ALP-TAS has no published specification. Its field layout, the two cipher stages, their keys and the longitude step table follow the community's description of the 2024 protocol, [pastebin.com/YB1ppAbt](https://pastebin.com/raw/YB1ppAbt). The frames `test/core/test_alptas.cpp` pins guard the encoder against regressions.

## What ALP-TAS costs us

`$PFLAA` carries an `IDType` with three values, and ADS-L's address mapping table has 64. The mapping is `address_mapping_table_to_id_type`, and it is a lie chosen from a short list of lies.

SkyDemon refuses the sentence for anything but 1 or 2 (`oss/SoftRF-moshe-braner/.../libraries/OGN/ads-l.h:657-658`: "if (AddrType==5) AddrType=1; else AddrType=2; // SkyDemon only accepts 1 or 2"). Leaving an address at IDType 0 draws nothing on that app at all, which is the one failure worth never causing again. Between the two values left, 1 (ICAO) claims a permanent, registry-issued identity; 2 (FLARM) claims a device-class kinship that is at least true of the mechanism, self-assigned and transient. So ICAO is reported as ICAO and everything else becomes FLARM.

What that costs: this device's own table, 58 (`settings::kAddressMappingTable`), and an OGN-Tracker address (7) both draw on the tablet as if they were FLARM. That is a lie about provenance too, and a cheaper one than claiming ICAO, because nothing downstream correlates a FLARM ID against an aircraft register the way it might an ICAO one.

## A target with no altitude

`format_pflaa` sends nothing for a target whose altitude is unknown. XCSoar drops a `$PFLAA` with an empty RelativeVertical ("Relative Altitude is required !", `ParsePFLAA` in `src/Device/Driver/FLARM/StaticParser.cpp` at XCSoar `6e0c8328`), and a 0 there is drawn level with us, which is the one thing the target has not said. `format_pflau` still names it as the threat when it is the advisory, with RelativeVertical left empty: the same parser reads an empty field as no vertical (`has_vertical`) and keeps the bearing and the distance.

## The category ADS-B sends

`adsb_emitter_to_adsl_category` reads the emitter category of an ADS-B identification message, a type code from 1 to 4 and a 3-bit category, into ADS-L G.1.3, which is the one category the tree holds. `adsl_category_to_alptas_type` takes it on to `$PFLAA` from there. The type code and category pairs are those of The 1090 Megahertz Riddle, table "Wake vortex in ADS-B identification and category message" ([mode-s.org](https://mode-s.org/1090mhz/content/ads-b/2-identification.html)). The rows below use the usual A0 to D7 shorthand, a letter for the type code (A is 4, B is 3, C is 2, D is 1, which is all reserved) and a digit for the category. Neither the Riddle nor the GDL 90 table below prints those letters. The Riddle rounds the type code 4 weights to kilograms. The pounds below are Table 11 of the GDL 90 Data Interface Specification, 560-1058-00 Rev A, which lists the same three groups in order ([faa.gov](https://www.faa.gov/sites/faa.gov/files/air_traffic/technology/adsb/archival/GDL90_Public_ICD_RevA.PDF)).

| ADS-B | Kind | G.1.3 | Why |
|---|---|---|---|
| A1 | light, under 15 500 lb | 1 | G.1.3 draws its line between 1 and 2 at the same 7031 kg, 15 500 lb |
| A2 to A5 | small, large, high vortex large, heavy | 2 | at or above that line |
| A6 | high performance, over 5 g and 400 kt | 2 | ADS-B gives it no weight. The tree already reads a FLARM jet, type 9, as 2 (`alptas_type_to_adsl_category`) |
| A7 | rotorcraft | 3 | ADS-B gives it no weight either. Issue 1 named 3 plain rotorcraft before Issue 2 split it into light, 3, and heavy, 15. A FLARM helicopter is read as 3 too, and 15 would reach `$PFLAA` as type 0 |
| B1 | glider, sailplane | 4 | |
| B2 | lighter-than-air | 5 | |
| B3 | parachutist, skydiver | 8 | |
| B4 | ultralight, hang-glider, paraglider | 6 | it spans G.1.3's 6, 7, 16 and 17, and 6 is the one that names an ultralight. `adsl_category_to_alptas_type` sends 6 to `$PFLAA` as C, an airship, until #216 is fixed |
| B6 | unmanned aerial vehicle | 11 | ADS-B carries no EU operational category. A FLARM UAV, type D, is read as 11 too |
| B7 | space vehicle | 0 | G.1.3 has no value for it |
| A0, B0, B5, C0 to C7, D0 to D7 | no information, reserved, surface vehicles and obstacles | 0 | |

## The flight state every decoder owes

Every decoder here writes `AircraftObservation::flight_state` as an ADS-L G.1.2 code, and `Unknown` (0) when its wire carries no state, because that field is all `core/traffic` reads to decide what the ground means (`core/traffic/README.md`). ADS-L and the uplink record carry G.1.2 itself.

ALP-TAS carries a two-bit state of its own, with no public specification and one reference: SoftRF's `latest_encode` (moshe-braner, `Legacy.cpp`) sends 1 on the ground, 2 airborne, 3 airborne and circling, and never 0. `alptas_flight_state` reads 0 as `Unknown`. It used to read it as on the ground, and an aircraft on the ground is never graded, so the one value no sender uses was the one that silenced the alarm.

## Two things an ALP-TAS frame is given before it is refused

`alptas_correct` runs the erasure correction the ADS-L path has always had. §C.2.1 is Manchester, so a chip pair that decoded to neither symbol is an error whose position the receiver already knows: `fec::manchester_decode` marks it, `protocol::Frame` carries the map, and the frame CRC says which combination of those flips was transmitted. Both frames hand that search to `fec::repair` (`core/fec/README.md`), which leaves a frame no combination repairs exactly as it arrived. Up to six marked bits, which is 64 combinations. Beyond that the burst is refused as damaged. The syndromes come out of the CRC's own linearity: flipping a data bit moves the check by `crc16_ccitt(that bit alone, init 0)`, and flipping a carried CRC bit moves it by the bit itself.

Until 2026-09-19 the ALP-TAS path checked the CRC and threw the frame away, so every burst with one dead chip pair was a `CRC` row while the same damage on an ADS-L frame was corrected and drawn. On a bench that difference is invisible, because nothing at -15 dBm has dead chips. At range it is receptions.

`alptas_keyed_second` answers the question a `DEC` row could not. The key stage is `utc_s >> 4`, so the decrypt is correct for every second inside the sender's own 16-second block, and the 4 time bits inside the frame then pin the second to ±1. Both sides of that are cheap to exploit: decrypt once per block the search window reaches, check the two fields every frame carries as constants (the trailing byte is 0, the low nibble of the pattern at bit 56 is 3), and the time bits name the sender's second exactly. The window is `kAlptasKeyWindowS`, 18 seconds, which is GPS-UTC: a receiver reporting GPS time instead of UTC is the one clock fault a device holding a valid fix can plausibly have, and a disagreement wider than that is not a clock, it is noise that framed.

The frame is refused either way. What the search buys is the tape saying `KEY+18` instead of `DEC`, which is the difference between a support case about a radio and one about a receiver's time.
