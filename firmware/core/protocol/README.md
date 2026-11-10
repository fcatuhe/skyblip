# core/protocol

The wire formats, in both directions and with no I/O: `adsl.{h,cpp}` and `adsl_uplink.{h,cpp}` are the ADS-L 4 SRD-860 packet the radio carries, `air.{h,cpp}` the shared air-frame plumbing, `alptas.{h,cpp}` and `nmea_out.{h,cpp}` what a tablet reads over the companion link. `adsb.{h,cpp}` reads the 1090 MHz extended squitter skyBlip Bravo receives, in one direction only.

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

## The flight state every decoder owes

Every decoder here writes `AircraftObservation::flight_state` as an ADS-L G.1.2 code, and `Unknown` (0) when its wire carries no state, because that field is all `core/traffic` reads to decide what the ground means (`core/traffic/README.md`). ADS-L and the uplink record carry G.1.2 itself.

ALP-TAS carries a two-bit state of its own, with no public specification and one reference: SoftRF's `latest_encode` (moshe-braner, `Legacy.cpp`) sends 1 on the ground, 2 airborne, 3 airborne and circling, and never 0. `alptas_flight_state` reads 0 as `Unknown`. It used to read it as on the ground, and an aircraft on the ground is never graded, so the one value no sender uses was the one that silenced the alarm.

## Two things an ALP-TAS frame is given before it is refused

`alptas_correct` runs the erasure correction the ADS-L path has always had. §C.2.1 is Manchester, so a chip pair that decoded to neither symbol is an error whose position the receiver already knows: `fec::manchester_decode` marks it, `protocol::Frame` carries the map, and the frame CRC says which combination of those flips was transmitted. Both frames hand that search to `fec::repair` (`core/fec/README.md`), which leaves a frame no combination repairs exactly as it arrived. Up to six marked bits, which is 64 combinations. Beyond that the burst is refused as damaged. The syndromes come out of the CRC's own linearity: flipping a data bit moves the check by `crc16_ccitt(that bit alone, init 0)`, and flipping a carried CRC bit moves it by the bit itself.

Until 2026-09-19 the ALP-TAS path checked the CRC and threw the frame away, so every burst with one dead chip pair was a `CRC` row while the same damage on an ADS-L frame was corrected and drawn. On a bench that difference is invisible, because nothing at -15 dBm has dead chips. At range it is receptions.

`alptas_keyed_second` answers the question a `DEC` row could not. The key stage is `utc_s >> 4`, so the decrypt is correct for every second inside the sender's own 16-second block, and the 4 time bits inside the frame then pin the second to ±1. Both sides of that are cheap to exploit: decrypt once per block the search window reaches, check the two fields every frame carries as constants (the trailing byte is 0, the low nibble of the pattern at bit 56 is 3), and the time bits name the sender's second exactly. The window is `kAlptasKeyWindowS`, 18 seconds, which is GPS-UTC: a receiver reporting GPS time instead of UTC is the one clock fault a device holding a valid fix can plausibly have, and a disagreement wider than that is not a clock, it is noise that framed.

The frame is refused either way. What the search buys is the tape saying `KEY+18` instead of `DEC`, which is the difference between a support case about a radio and one about a receiver's time.

## ADS-B: from chips to typed messages

`adsb_receive` takes the 27 chip bytes the LR2021 reports after its 0x95 sync and returns one `AdsbMessage`, stamped with the time and RSSI the radio gave it. It parses DF17 and stops there: CPR decoding, freshness and the assembly into an `AircraftObservation` are #190's, so the flight state this file owes is owed there.

**Chips.** 0x95 is the chip pattern of the bits `1000`, the first four bits of DF16 and DF17, which is why the radio hears those two formats and no other. DF18 starts `1001`, so TIS-B, ADS-R and non-transponder ADS-B never reach this file. 1090 MHz sends a 1 as a pulse then a gap, the chips `10`, and `fec::manchester_decode` reads `01` as a 1 because ADS-L C.2.1 does. So the decoder prepends 0x95, inverts every chip and hands the result to the same function. Inverting maps `00` to `11`, so a chip pair that is neither symbol is still marked as doubtful. SoftRF does the same in `radiolib.cpp:2808-2823`, read for the fact only.

**Parity.** The Mode S generator is 0x1FFF409. Shifted left by 7 it is 0xFFFA0480, the polynomial `fec::adsl_crc_pass` already divides by (`core/fec/crc.h`), so `fec::adsl_crc_check` over the 14 bytes leaves 0 for a sound DF17 frame. There is no second CRC. A frame that fails gets one repair: when exactly one chip pair is doubtful and flipping its bit clears the parity, `fec::repair` flips it. Two doubtful pairs, or a wrong bit no chip marked, are refused as `Crc`. dump1090 and readsb flip one bit anywhere in a DF17 frame. Limiting the flip to the one bit the chips already doubt costs the frames with two damaged pairs, and saves the ghost aircraft a false repair can draw on the glass.

**Messages.** Each one carries the 24-bit address, the capability field and the type code. Units follow `firmware/README.md`:

| TYPE | Kind | What it reads |
|---|---|---|
| 1 to 4 | `Identification` | category set D down to A, category 0 to 7, the callsign with trailing blanks cut. A callsign holding a code outside the character set is refused whole, as `callsign_of` refuses one on ADS-L |
| 5 to 8 | `SurfacePosition` | the movement code as the floor of its band in thousandths of a knot (Doc 9871 A.2.3.3.1), the 7-bit track and its status, T, and the CPR pair |
| 9 to 18 | `AirbornePosition` | pressure altitude, T, and the CPR pair |
| 20 to 22 | `AirbornePosition` | GNSS height above the ellipsoid in the same field, T, and the CPR pair |
| 19 | `Velocity` | subtypes 1 and 2: east and north ground speed. Subtypes 3 and 4: airspeed, its type and the magnetic heading. All four: the vertical rate with its source, and GNSS height minus pressure altitude |
| 31 | `OperationalStatus` | subtype, version, and GVA when the status is airborne and version 2 |

Every other type code, a velocity subtype outside 1 to 4 and a status subtype outside 0 and 1 come back `Unsupported` with the header filled. TYPE 0 carries a pressure altitude with no position and is not parsed yet.

**Zeros and top codes.** In a velocity field, all zeros means the sender has nothing, and the value is (code - 1) steps. The top code, all ones, means that much or more, so `AdsbQuantity` marks it `saturated` (RTCA 1090-WP-14-09R1, the DO-260A draft of Appendix A, Figures A-6 and A-7). The movement code saturates at 124, 175 kt or more, and 125 to 127 are reserved. A version 0 status has no version field. Its bits there are reserved, so it reads as version 0, and GVA exists only from version 2. The value of each GVA code is not checked against DO-260B yet, so the code is passed on raw.

**Altitude.** A 12-bit field with Q = 1 counts 25 ft steps from -1000 ft. With Q = 0 it is a Gillham code in 100 ft steps, laid out C1 A1 C2 A2 C4 A4 B1 Q B2 D2 B4 D4 (DO-260B draft 4.2, Figure 2-4): D2 D4 A1 A2 A4 B1 B2 B4 are a Gray-coded count of 500 ft, C1 C2 C4 a Gray-coded count of 100 ft that runs backwards when the 500 ft count is odd, and C1 C2 C4 of 000, 101 or 111 is no altitude. These steps were derived from the Gillham table the test pins (Wikipedia's "Gillham code", after the Bendix/King KMH 880 installation manual). pyModeS and dump1090 follow the same steps because the code dictates them. All zeros is no altitude. RadioLib (`ADSB.cpp:193-199`) reads Q = 0 as linear 100 ft steps, and libmodes (`mode-s.c:391-403`) returns 0 for it. Both are wrong for an encoder that sends Gillham.

**GNSS height, TYPE 20 to 22, is coded as pressure altitude is, in feet.** ICAO Doc 9871 (2nd ed., 2012) §A.2.3.2.4 says what the field holds, the height above the WGS-84 ellipsoid, and not how it is coded. The RTCA DO-260B draft, version 4.2 (working paper [1090-WP30-18](https://www.anteni.net/adsb/Doc/1090-WP30-18-DRAFT_DO-260B-V42.pdf), DO-260A with Changes 1 and 2 and the agreed changes merged in), says how. Its §2.2.3.2.3.4.2 reports GNSS HAE in the field of TYPE 20 to 22 and codes it "in accordance with §2.2.3.2.3.4.3", the same clause §2.2.3.2.3.4.1 sends pressure altitude to. §2.2.3.2.3.4.3 "Altitude Encoding" then gives Q = 0 in 100 ft increments, Q = 1 in 25 ft increments, and all zeros when no altitude is available. It is a draft, not the published DO-260B, which was not available. The published RTCA DO-260 (2000) has the same structure: §2.2.5.1.5 a fills the field with pressure altitude "as provided in subparagraphs 2.2.3.2.3.4 through 2.2.3.2.3.4.3", §2.2.5.1.5 b(1) with GNSS height "as provided in subparagraphs 2.2.3.2.3.4.2 through 2.2.3.2.3.4.3", and §2.4.3.2.3.4.3 verifies the one coding through the pressure and the GNSS test procedures alike. On the receiving side, §2.2.8.1.4 reports a geometric altitude in the same two's complement format as §2.2.8.1.12 reports a pressure one, MSB 65,536 and LSB 0.015625. readsb, dump1090-fa and rs1090 decode the field this way, and so does adsbee (GPL-3.0, commit d20205a), which corroborates the reading. pyModeS and RadioLib (`ADSB.cpp:200-205`) read it as whole metres, and The 1090MHz Riddle states metres without a source. No real TYPE 20 to 22 frame was found to settle it on the air. The 1090dumpCSV set below holds four. Each passed only after dump1090's one-bit repair, and one decodes to 103000 ft. The test builds its own TYPE 20 frame with Q = 1 and N = 1440, which reads 35000 ft and would read 2896 m as metres.

**Test vectors.** `test/core/test_adsb.cpp` pins frames to decodes their sources publish, and builds the rest. The hex frames and their values are facts, and no code was copied from any source:
- The 1090MHz Riddle (Junzi Sun), worked examples: identification, the airborne pair, the surface frame and both velocity subtypes.
- [IndoorOutdoor/1090dumpCSV](https://huggingface.co/datasets/IndoorOutdoor/1090dumpCSV) (Apache-2.0, listed in `NOTICE.md`), frames recorded with dump1090 and its decode beside each: callsigns and categories, a Q-bit and a Gillham altitude, two velocities and two version 2 statuses. That dump1090 prints the vertical rate source inverted against Figure A-6, so only the rates are pinned from it.
- The Gillham rows come from Wikipedia's "Gillham code" table, which cites the Bendix/King KMH 880 installation manual.
- The GNSS height frames, the empty one included, are built in the test, not copied.
