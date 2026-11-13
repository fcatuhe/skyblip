# core/protocol

skyBlip's side of the radio protocols. The stacks in `protocols/` decide what the specifications decide, in their own plain types (`protocols/README.md`). This directory holds the adapters that turn those types into skyBlip's model and back, the choices skyBlip made around them, and what a tablet reads over the companion link.

| File | What it is |
|---|---|
| `adsl.{h,cpp}` | own-ship as an ADS-L Traffic payload, extrapolated to the instant the timestamp names, with the accuracy claimed from the receiver's DOP. A received Traffic payload as an `AircraftObservation`. The Status values this device declares |
| `callsign.{h,cpp}` | OGN's payload 66, the name ADS-L has no field for (below) |
| `adsl_uplink.{h,cpp}` | `AdslUplink`, the stack's G.3 uplink over observations: each target is dated the instant the station forwards it, and a record older than 10 s or dated ahead of the second it was heard in is counted stale and not drawn |
| `air.{h,cpp}` | one M-band dwell that hears ADS-L and ALP-TAS behind one shared sync window, names the system from the sync tail, and lays out the M-band buffer the SX1262 sends |
| `alptas.{h,cpp}` | an ALP-TAS position as an observation and back: the ADS-L category, address table and flight state its fields map to |
| `adsb_observation.{h,cpp}` | an ADS-B track as an observation, in ADS-L units |
| `adsb_category.{h,cpp}` | what an ADS-B emitter category is in ADS-L's terms (below) |
| `nmea_out.{h,cpp}` | the `$PFLAU` and `$PFLAA` sentences a tablet reads |

`from_own`, `from_own_status` and `from_own_callsign` take the address as `settings::AirAddress`, and answer false with the packet untouched when there is none: a unit the server has not activated builds no frame, whatever the layers above it decided (`core/settings/README.md`).

## The Status payload

`from_own_status` hands `adsl::encode_status` what this device declares in §G.2's payload type 3, and builds nothing without an address. The payload's layout and its version are the stack's (`protocols/adsl/README.md`). The values are skyBlip's:

| Field | Value | Why |
|---|---|---|
| Max. Protocol Version | 1 | this device reads Issue 2 payloads (`protocols/adsl/README.md`, Which issue a payload is) |
| M-band receive | 2, Partial | the dwells hear 868.2 MHz from 450 to 798 ms and 868.4 MHz from 800 to 1000, about half the direct slot averaged over both channels (`timing::Scheduler::direct_heard_permille()`) |
| O-band LDR receive | 0 | not received |
| O-band HDR receive | 0 | heard only in the uplink slot, which §G.2.4 leaves out |
| ADS-L 4 Mobile | 0 | none |
| E-conspicuity receive | bit 0, and bit 4 while the paired Bravo is live | the FLARM-generation wire is decoded here. A live Bravo hears ADS-B 1090 in the same aircraft, and §G.2.6 leaves a capability unset when it is unknown, as it is while the Bravo is out of reach |
| E-conspicuity transmit | 0 | nothing but ADS-L goes out |
| XPDR | 0 | no transponder is known |
| Traffic Uplink client | 1 | the uplink is decoded and drawn |
| FIS-B client | 0 | not decoded |

## Writing for an Issue 1 neighbour

§B.5 asks own Traffic to go out in a version every close neighbour reads, and `core/traffic/README.md` says how the lowest one is found. With no close Issue 1 client, a position goes out in the lowest version whose encoding is the same (§E.1.1), so a payload carrying category 14 to 17 or an unknown field goes as version 1, exactly as it is.

Beside a close Issue 1 client, `RadioService` hands the packet to `issue1_form`, which re-codes it as version 0 only where Issue 1 has a truthful code for every field. This device never claims a value it does not have:

| Issue 2 code | Beside an Issue 1 client | Why |
|---|---|---|
| category 14, model plane | 0, no category information | Issue 1 has no code for it |
| category 15, heavy rotorcraft | 3, rotorcraft | Issue 1's rotorcraft covers every weight |
| category 16, hang-glider | 7, hang-glider or paraglider | Issue 2 split that class |
| category 17, paramotor | 7, hang-glider or paraglider | a powered paraglider, and Issue 1's closest wing |
| position 0x800000, no fix | 0xFFFFFF | Issue 1's own marker for no fix |
| altitude, ground speed or vertical rate unknown | the position is held for that second | Issue 1 has no code for unknown, and 0x3FFF, 0xFF or 0x1FF would read as 61112 m, 238 m/s or a 119 m/s descent |

A held position is counted once a second in `air.tx_held_for_b5`, and the next position the schedule owes tries again. Status and the name are not Traffic, which §B.5 leaves free, so they go out as version 1 whoever is near.

## The name ADS-L has no field for

An iConspicuity payload carries no callsign, so a target on the glass is six hex digits and a pilot's radio call is not. The name comes from the one payload the specification hands to somebody else: F.2.1 assigns value 66 to "OGN Diagnostics" and footnotes it "payload definition is outside the scope of this document", which makes the layout Jalocha's (`oss/nrf52-ogn-tracker/src/adsl.h:120-128`) and ours to follow rather than to invent.

`from_own_callsign` in `callsign.{h,cpp}` writes the one record of it we speak, on the frame layer `adsl::Packet` exposes. Type 66, the same 6+24 sender address our position frames carry, then a header byte of telemetry type 1 and info type 5 (OGN's `TelemType` and `InfoType`), then 14 characters. Everything around it is the frame we already build: the same sync word, Manchester coding, scramble and CRC24, which is why the radio needed no new dwell and the receiver no new configuration.

`callsign_of` is the other direction, and it is a trust boundary: text off the air is refused whole when any byte of it is not a callsign character, rather than drawn with a control character in it or spliced into a `$PFLAA` ID field as a comma. `is_callsign_character` is the set the radar menu types (blank, dash, `A`-`Z`, `0`-`9`), and the pilot's own callsign is held to the same one. What it returns goes to `core/traffic/callsigns.h` keyed on the sender's table and address, never into an `AircraftObservation` - a name is an attribute of an address, a position is an observation, and the two have different lifetimes.

An OGN tracker reads what we send: `ProcessRxADSL` takes any telemetry frame, pulls `getInfo(Call, 5)` out of it and hangs the name on the target of that address (`oss/nrf52-ogn-tracker/src/proc.cpp:715-733`), with no opinion about our AMT 58. It transmits the same record itself every 40 to 60 seconds, hopping between four channels, so we hear one of theirs perhaps once in four.

## What ALP-TAS costs us

`$PFLAA` carries an `IDType` with three values, and ADS-L's address mapping table has 64. The mapping is `address_mapping_table_to_id_type`, and it is a lie chosen from a short list of lies.

SkyDemon refuses the sentence for anything but 1 or 2 (`oss/SoftRF-moshe-braner/.../libraries/OGN/ads-l.h:657-658`: "if (AddrType==5) AddrType=1; else AddrType=2; // SkyDemon only accepts 1 or 2"). Leaving an address at IDType 0 draws nothing on that app at all, which is the one failure worth never causing again. Between the two values left, 1 (ICAO) claims a permanent, registry-issued identity; 2 (FLARM) claims a device-class kinship that is at least true of the mechanism, self-assigned and transient. So ICAO is reported as ICAO and everything else becomes FLARM.

What that costs: this device's own table, 58 (`settings::kSkyblipAddressMappingTable`), and an OGN-Tracker address (7) both draw on the tablet as if they were FLARM. That is a lie about provenance too, and a cheaper one than claiming ICAO, because nothing downstream correlates a FLARM ID against an aircraft register the way it might an ICAO one.

## A target with no altitude

`format_pflaa` sends nothing for a target whose altitude is unknown. XCSoar drops a `$PFLAA` with an empty RelativeVertical ("Relative Altitude is required !", `ParsePFLAA` in `src/Device/Driver/FLARM/StaticParser.cpp` at XCSoar `6e0c8328`), and a 0 there is drawn level with us, which is the one thing the target has not said. `format_pflau` still names it as the threat when it is the advisory, with RelativeVertical left empty: the same parser reads an empty field as no vertical (`has_vertical`) and keeps the bearing and the distance.

## Where a target came from

`format_pflaa` appends three fields to an ADS-B target and to no other: NoTrack 0, Source 1 and the RSSI. FLARM's Data Port ICD, FTD-012 (ICD 7.19, document revision 1.1, [flarm.com](https://www.flarm.com/wp-content/uploads/2024/04/FTD-012-Data-Port-Interface-Control-Document-ICD-7.19.pdf)), adds NoTrack in data port protocol version 8 and Source and RSSI in version 9 (§6.4 and §6.5). Its §8.2 puts them after AcftType as `[,<NoTrack>[,<Source>,<RSSI>]]`, and numbers the sources 0 FLARM, 1 ADS-B, 3 ADS-R, 4 TIS-B and 6 Mode-S. ADS-B has no no-track flag, so NoTrack is 0. RSSI is "Empty if unknown" in §8.2. Bravo fills it with the level in dBm of the 1090 MHz message that last placed the target, and leaves it empty when the LR2021 gave that message none, since it levels only the newest frame of a FIFO read (`hardware/parts/lr2021/README.md`). A Go leaves it empty for a target Bravo relays, because the `observation` frame carries no RSSI. A Bluetooth RSSI never goes there. Within 82 characters the RSSI gives way first, then the callsign shortens, and no other field ever does. Lyusupov's SoftRF sends its radio's RSSI in the same field on ESP32 and nRF52 (`PFLAA_EXT1_ARGS`), and nothing on the STM32 of a Retro.

Every other source keeps the eleven fields it has always sent. Source has no code for ADS-L or the uplink. XCSoar's parser reads a missing Source and an empty one the same way, as FLARM, so it shows a FLARM target as FLARM without a 0 (`ParsePFLAA` in `src/Device/Driver/FLARM/StaticParser.cpp` at XCSoar `6e0c8328`). No sender we found writes an empty Source, and SkyDemon drops the connection on a field value it does not expect: "If we encountered a 3 we would indeed abort the connection", of IDType (Tim Dawson, SkyDemon forum topic 32592, 16 December 2020). So the bytes a Go sends for ADS-L, FLARM and uplink targets do not change.

The ADS-B form is the one GA/TAS sends on every `$PFLAA`: NoTrack, Source, and an empty RSSI (`src/lib/dataport/ace/src/dataport.cpp` in rvt/openace at `87740c7b`, GPL, read for the fact). Its README marks SkyDemon as working over Bluetooth and over DataPort (NMEA) Wi-Fi, "Tested Frequently during flight". It marks XTrack over Bluetooth, "Tested for connectivity", and SeeYou Navigator over Bluetooth, "GPS and traffic targets shown". That Bluetooth column is not labelled NMEA, but the same README says GDL90 over Bluetooth is not set up, and the GA/TAS configuration page says Bluetooth carries the DataPort NMEA stream (`src/SystemGUI/src/components/modules.js`). Both SoftRF trees send the three fields too: lyusupov's nRF52 build since release 1.0 (October 2021), and moshe-braner's on every `$PFLAA`. Enroute reads the first eleven fields and ignores the rest (`processFLARMMessagePFLAA`). XCSoar reads the three fields when they are there, whatever the version. No EFB was found to fail on fields past AcftType. #182 checks it on the device.

## 82 characters

NMEA 0183 caps a sentence at 82 characters from the `$` to the CRLF. FLARM's Configuration Specification says the same (FTD-014 revision 1.18, footnote to §3.1.3 BAUD: "any sentence can consist of 82 characters maximum"). FTD-012 §7 allows 83, with 80 between the `$` and the CRLF. `kNmeaSentenceCeilingBytes` keeps the stricter figure.

In a `$PFLAA` only the callsign is free text, so the callsign gives way. When the sentence would pass 82 characters, the callsign loses characters from its end, and the `!` goes when none fits. With alarm level 3, a 6-character height, and speed, climb and track at their widest, a 14-character ADS-L callsign and an 8-character ADS-B one fit whole while north and east each stay under 1000 km. Past that, under the same bounds, ADS-L loses up to 3 characters and ADS-B up to 2, far beyond any range `core/traffic/sanity.h` keeps a target at. At the widest the parameter types allow, an alarm level of 255 and an 11-character height, an ADS-B sentence is 82 characters with no callsign at all, so the fields alone never pass the cap. `test/core/test_nmea_length.cpp` pins each case. XCSoar copies 15 characters of the ID field and parses the hex address out of them, and it still steps over the whole field, so a callsign of any length leaves the fields after it where they were (`CSVLine::Read` in `src/io/CSVLine.cpp`).

Every formatter here builds its sentence in its own buffer and copies it out only when the sentence and its NUL fit in `capacity`. Otherwise it returns 0 and writes nothing.

## The category ADS-B sends

`adsb_emitter_to_adsl_category` reads the emitter category of an ADS-B identification message, a type code from 1 to 4 and a 3-bit category, into ADS-L G.1.3, which is the one category the tree holds. `adsl_category_to_alptas_type` takes it on to `$PFLAA` from there. The type code and category pairs are those of The 1090 Megahertz Riddle, table "Wake vortex in ADS-B identification and category message" ([mode-s.org](https://mode-s.org/1090mhz/content/ads-b/2-identification.html)). The rows below use the usual A0 to D7 shorthand, a letter for the type code (A is 4, B is 3, C is 2, D is 1, which is all reserved) and a digit for the category. Neither the Riddle nor the GDL 90 table below prints those letters. The Riddle rounds the type code 4 weights to kilograms. The pounds below are Table 11 of the GDL 90 Data Interface Specification, 560-1058-00 Rev A, which lists the same three groups in order ([faa.gov](https://www.faa.gov/sites/faa.gov/files/air_traffic/technology/adsb/archival/GDL90_Public_ICD_RevA.PDF)).

| ADS-B | Kind | G.1.3 | Why |
|---|---|---|---|
| A1 | light, under 15 500 lb | 1 | G.1.3 draws its line between 1 and 2 at the same 7031 kg, 15 500 lb |
| A2 to A5 | small, large, high vortex large, heavy | 2 | at or above that line |
| A6 | high performance, over 5 g and 400 kt | 2 | ADS-B gives it no weight. The tree already reads a FLARM jet, type 9, as 2 (`alptas_type_to_adsl_category`) |
| A7 | rotorcraft | 3 | ADS-B gives it no weight either. Issue 1 named 3 plain rotorcraft before Issue 2 split it into light, 3, and heavy, 15. A FLARM helicopter is read as 3 too |
| B1 | glider, sailplane | 4 | |
| B2 | lighter-than-air | 5 | |
| B3 | parachutist, skydiver | 8 | |
| B4 | ultralight, hang-glider, paraglider | 6 | it spans G.1.3's 6, 7, 16 and 17, and 6 is the one that names an ultralight |
| B6 | unmanned aerial vehicle | 11 | ADS-B carries no EU operational category. A FLARM UAV, type D, is read as 11 too |
| B7 | space vehicle | 0 | G.1.3 has no value for it |
| A0, B0, B5, C0 to C7, D0 to D7 | no information, reserved, surface vehicles and obstacles | 0 | |

## The type a tablet draws

`adsl_category_to_alptas_type` turns the G.1.3 category of a target, from any source, into the AcftType field of `$PFLAA`. FTD-012 §8.2 lists that field from 0 to F, and the tablet draws the target with the icon of its type. G.1.3 is the table in `docs/reference/ads-l_4_srd860_issue_2.md`. G.1.3 splits fixed wing by weight at 7031 kg, and AcftType splits it by engine, so a light jet or turboprop reaches the tablet as a piston aircraft and a heavy piston aircraft as a jet. Before #216 the table sent category 2 as a piston aircraft, so every airliner Bravo heard was drawn as one, and it sent category 6 as an airship and 14 to 17 as no type.

| G.1.3 | Kind | AcftType | Why |
|---|---|---|---|
| 0 | no information | 0 | FTD-012 reserves 0 and names A unknown. XCSoar reads 0 as unknown and A as a flying saucer (`AircraftType` in `src/FLARM/Traffic.hpp` at XCSoar `71cd8bca`). The tree has always sent 0 |
| 1 | light fixed wing, under 7031 kg | 8, reciprocating engines | most aircraft under 7031 kg have piston engines, though light turboprops and jets fly there too. Tow planes, AcftType 2, and drop planes, AcftType 5, fall here as well, but G.1.3 does not mark them |
| 2 | small to heavy fixed wing, 7031 kg and over | 9, jet or turboprop | nearly every aircraft that heavy is a jet or a turboprop |
| 3 | light rotorcraft | 3, helicopter, gyrocopter, rotorcraft | |
| 4 | glider, sailplane | 1, glider, motor glider | |
| 5 | lighter-than-air | B, balloon | G.1.3 does not tell a balloon from an airship, AcftType C, and balloons are the more common |
| 6 | ultralight and motorized hang-glider | 8, reciprocating engines | an ultralight, and a trike, fly on a reciprocating engine. AcftType 6 is a hang glider with no engine |
| 7 | paraglider | 7, paraglider | |
| 8 | parachutist, skydiver, wingsuit | 4, skydiver | |
| 9 | eVTOL, UAM | 3, rotorcraft | it carries people, takes off on its rotors and hovers like a helicopter. D would tell the pilot it is a drone |
| 10 | gyrocopter | 3 | FTD-012 names the gyrocopter in 3 |
| 11 to 13 | UAS open, specific and certified | D, UAV | |
| 14 | model plane | D, UAV | a model plane flies with no one on board, which is what D names |
| 15 | heavy rotorcraft | 3 | |
| 16 | hang-glider | 6, hang glider | |
| 17 | paramotor | 7, paraglider | it flies a paraglider wing, and FTD-012 has no powered paraglider |
| 18 and over | outside G.1.3 | 0 | |

`alptas_type_to_adsl_category` goes the other way, from the AcftType of an ALP-TAS frame to G.1.3. An AcftType names an engine or a kind of wing and gives no weight. Where the table above sends one category to a type, that category is the one read back. Where it sends several, the rows below say which one is read.

| AcftType | Kind | G.1.3 | Why |
|---|---|---|---|
| 0, A, E | reserved, unknown, reserved | 0 | |
| 1 | glider, motor glider | 4 | |
| 2, 5 | tow plane, drop plane | 1 | tow and drop planes nearly all weigh under 7031 kg. A turboprop drop plane, such as a Caravan or a Twin Otter, then reaches `$PFLAA` as AcftType 8 |
| 3 | helicopter, gyrocopter, rotorcraft | 3 | it is sent for 3, 9, 10 and 15. It gives no weight and names no eVTOL, so it is read as light rotorcraft, as Issue 1's plain rotorcraft was |
| 4 | skydiver | 8 | |
| 6 | hang glider | 16 | 16 is the one category sent as 6. Issue 1 put the hang glider with the paraglider in 7 |
| 7 | paraglider | 7 | it is sent for 7 and 17. A paramotor is the rarer of the two |
| 8 | reciprocating engines | 1 | it is sent for 1 and 6. An ultralight is a light aircraft too |
| 9 | jet or turboprop | 2 | 2 is the one category sent as 9, so a jet reaches `$PFLAA` as a jet again. A light jet or turboprop lands in the heavier weight class |
| B, C | balloon, airship | 5 | |
| D | UAV | 11 | it is sent for 11 to 14. FLARM carries no EU operational category, so 11, the open category, is the default |
| F | static obstacle | 0 | G.1.3 has no value for it |
| past F | outside FTD-012's table | 0 | |

## The flight state every decoder owes

Every adapter here writes `AircraftObservation::flight_state` as an ADS-L G.1.2 code, and `Unknown` (0) when its wire carries no state, because that field is all `core/traffic` reads to decide what the ground means (`core/traffic/README.md`). ADS-L and the uplink record carry G.1.2 itself, and an ADS-B track's `AirGround` maps onto it in `adsb_observation.cpp`.

ALP-TAS carries a two-bit state of its own, with no public specification and one reference: SoftRF's `latest_encode` (moshe-braner, `Legacy.cpp`) sends 1 on the ground, 2 airborne, 3 airborne and circling, and never 0 (`protocols/alptas/frame.h`). `alptas_flight_state` reads 0 as `Unknown`. It used to read it as on the ground, and an aircraft on the ground is never graded, so the one value no sender uses was the one that silenced the alarm.

