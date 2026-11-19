# core/protocol

skyBlip's side of the radio protocols. The stacks in `protocols/` decide what the specifications decide, in their own plain types (`protocols/README.md`). This directory holds the adapters that turn those types into skyBlip's model and back, the choices skyBlip made around them, and what a tablet reads over the companion link.

| File | What it is |
|---|---|
| `adsl.{h,cpp}` | own-ship as an ADS-L Traffic payload, extrapolated to the instant the timestamp names, with the accuracy claimed from the receiver's DOP. A received Traffic payload as an `AircraftObservation`. The Status values this device declares |
| `callsign.{h,cpp}` | the name ADS-L has no field for, on the payload 66 record of `protocols/adsl_edge/` (below) |
| `adsl_uplink.{h,cpp}` | `AdslUplink`, the stack's G.3 uplink over observations. `decode` counts a record older than 10 s, or dated ahead of the second it was heard in, as stale and does not draw it. `encode` re-encodes each target and dates it the instant the station sends, which G.3 allows only for traffic from systems other than ADS-L: an ADS-L payload must be forwarded verbatim. It serves the simulated skyPost and the tests, and a real ground station forwarding ADS-L would call `adsl::Uplink` with the payloads as heard |
| `air.{h,cpp}` | one M-band dwell that hears ADS-L and ALP-TAS behind one shared sync window, names the system from the sync tail, and lays out the M-band buffer the SX1262 sends |
| `alptas.{h,cpp}` | an ALP-TAS position as an observation and back: the ADS-L category, address table, flight state and emergency its fields map to, the fix age, and `AlptasReceiver`, which follows a random address from step to step |
| `adsb_observation.{h,cpp}` | an ADS-B track as an observation, in ADS-L units |
| `adsb_category.{h,cpp}` | what an ADS-B emitter category is in ADS-L's terms (below) |
| `nmea_out.{h,cpp}` | the `$PFLAU` and `$PFLAA` sentences a tablet reads |

`from_own`, `from_own_status` and `from_own_callsign` take the address as `settings::AirAddress`, and answer false with the packet untouched when there is none: a unit the server has not activated builds no frame, whatever the layers above it decided (`core/settings/README.md`). They write whatever table and address they are handed. `RadioService` hands all three the same pair, `settings::on_air()`: the aircraft's ICAO address under table 5 while the pilot has set one, which F.2.3 asks for, and table 58 with the serial otherwise, so a receiver files the Traffic, the Status and the name under one target (`core/settings/README.md`, The ICAO address). The Issue 1 form B.5 asks for keeps the pair, since Issue 1's F.2.2 has table 5 for ICAO too.

## The Status payload

`from_own_status` hands `adsl::encode_status` what this device declares in §G.2's payload type 3, and builds nothing without an address. The payload's layout and its version are the stack's (`protocols/adsl/README.md`). The values are skyBlip's:

| Field | Value | Why |
|---|---|---|
| Max. Protocol Version | 1 | this device reads Issue 2 payloads (`protocols/adsl/README.md`, Which issue a payload is) |
| M-band receive | 2, Partial | the dwells hear 868.2 MHz from 450 to 822 ms and 868.4 MHz from 827 to 1000, about half the direct slot averaged over both channels (`timing::Scheduler::direct_heard_permille()`) |
| O-band LDR receive | 0 | not received |
| O-band HDR receive | 0 | heard only in the uplink slot, which §G.2.4 leaves out |
| ADS-L 4 Mobile | 0 | none |
| E-conspicuity receive | bit 0, and bit 4 while the paired Bravo is live | the ALP-TAS wire is decoded here. A live Bravo hears ADS-B 1090 in the same aircraft, and §G.2.6 leaves a capability unset when it is unknown, as it is while the Bravo is out of reach |
| E-conspicuity transmit | bit 4 once the aircraft's transponder is known to send ADS-B 1090, 0 otherwise | the unit sends nothing but ADS-L, and §G.2.7 counts the systems installed in the same vehicle. Bravo heard an identification, an extended squitter, from the address the pilot entered (below) |
| XPDR | 2, Mode S, once the aircraft's transponder is known, 0 otherwise | Bravo heard the address the pilot entered, so the aircraft carries a Mode S transponder (below). 0 also means unknown |
| Traffic Uplink client | 1 | the uplink is decoded and drawn |
| FIS-B client | 0 | not decoded |

The aircraft's transponder is known once two sources agree: the pilot entered the aircraft address (F.2.3), and a paired Bravo, on the ground, names the transponder it hears loudest at that same address (`products/skyblip_bravo/README.md`, Own transponder). Neither is enough alone. An ICAO address is assigned to aircraft that carry no transponder, and the loudest transponder on an apron can be a neighbour's. A 24-bit address heard on 1090 MHz is a Mode S transponder, and an identification heard from it is an extended squitter, so ADS-B out. `BravoService` keeps what it confirmed in `bus::State::aircraft_transponder` for as long as the entered address stays the same and the unit runs, so it holds through the flight after Bravo stops naming it. A Go with no Bravo, or one that never heard the entered address, declares neither, which §G.2.7 and §G.2.8 read as unknown.

## Writing for an Issue 1 neighbour

§B.5 asks own Traffic to go out in a version every close neighbour reads, and `core/traffic/README.md` says how the lowest one is found. With no close Issue 1 client, a position goes out in the lowest version whose encoding is the same (§E.1.1), so a payload carrying category 14 to 17 or an unknown field goes as version 1, exactly as it is.

Beside a close Issue 1 client, `RadioService` hands the packet to `issue1_form`, which passes the VFOM own-ship's VDOP gives to `adsl::issue1_form()`. The stack re-codes it as version 0 only where Issue 1 has a truthful code for every field, and answers false where it has none (`protocols/adsl/README.md`, Soft versioning). A position with no Issue 1 form is held for that second, so this device never claims a value it does not have.

A held position is counted once a second in `air.tx_held_for_b5`, and like any burst that did not go out it is retried 2 s later on the same channel, until it goes out or the next position is due (`core/timing/README.md`). Status and the name are not Traffic, which §B.5 leaves free, so they go out as version 1 whoever is near.

## The name ADS-L has no field for

An iConspicuity payload carries no callsign, so a target on the glass is six hex digits and a pilot's radio call is not. The name comes from the one payload the specification hands to somebody else: F.2.1 assigns value 66 to "OGN Diagnostics" and footnotes it "payload definition is outside the scope of this document", which makes the layout Jalocha's (`oss/nrf52-ogn-tracker/src/adsl.h:120-128`) and ours to follow rather than to invent.

The record itself is `protocols/adsl_edge/`'s (`registration.h`): type 66, the same 6+24 sender address our position frames carry, then a header byte of telemetry type 1 and info type 5 (OGN's `TelemType` and `InfoType`), then 14 characters. `from_own_callsign` in `callsign.{h,cpp}` writes our name into it, cut at the first character the menu does not type. Everything around it is the frame we already build: the same sync word, Manchester coding, scramble and CRC24, which is why the radio needed no new dwell and the receiver no new configuration.

`callsign_of` is the other direction, and it is a trust boundary: text off the air is refused whole when any byte of it is not a callsign character, rather than drawn with a control character in it or spliced into a `$PFLAA` ID field as a comma. `is_callsign_character` is the set the radar menu types (blank, dash, `A`-`Z`, `0`-`9`), and the pilot's own callsign is held to the same one. What it returns goes to `core/traffic/callsigns.h` keyed on the sender's table and address, never into an `AircraftObservation` - a name is an attribute of an address, a position is an observation, and the two have different lifetimes.

An OGN tracker reads what we send: `ProcessRxADSL` takes any telemetry frame, pulls `getInfo(Call, 5)` out of it and hangs the name on the target of that address (`oss/nrf52-ogn-tracker/src/proc.cpp:715-733`), with no opinion about our AMT 58. It transmits the same record itself every 40 to 60 seconds, hopping between four channels, so we hear one of theirs perhaps once in four.

## What ALP-TAS costs us

`$PFLAA` carries an `IDType` with three values, and ADS-L's address mapping table has 64. The mapping is `address_mapping_table_to_id_type`, and it is a lie chosen from a short list of lies.

SkyDemon refuses the sentence for anything but 1 or 2 (`oss/SoftRF-moshe-braner/.../libraries/OGN/ads-l.h:657-658`: "if (AddrType==5) AddrType=1; else AddrType=2; // SkyDemon only accepts 1 or 2"). Leaving an address at IDType 0 draws nothing on that app at all, which is the one failure worth never causing again. Between the two values left, 1 (ICAO) claims a permanent, registry-issued identity; 2 (FLARM) claims a device-class kinship that is at least true of the mechanism, self-assigned and transient. So ICAO is reported as ICAO and everything else becomes FLARM.

What that costs: this device's own table, 58 (`settings::kSkyblipAddressMappingTable`), and an OGN-Tracker address (7) both draw on the tablet as if they were FLARM. A Go that transmits an ICAO address under table 5 draws as ICAO, which is what it is. That is a lie about provenance too, and a cheaper one than claiming ICAO, because nothing downstream correlates a FLARM ID against an aircraft register the way it might an ICAO one.

## A target with no altitude

`format_pflaa` sends nothing for a target whose altitude is unknown. XCSoar drops a `$PFLAA` with an empty RelativeVertical ("Relative Altitude is required !", `ParsePFLAA` in `src/Device/Driver/FLARM/StaticParser.cpp` at XCSoar `6e0c8328`), and a 0 there is drawn level with us, which is the one thing the target has not said. `format_pflau` still names it as the threat when it is the advisory, with RelativeVertical left empty: the same parser reads an empty field as no vertical (`has_vertical`) and keeps the bearing and the distance.

## Where a target came from

`format_pflaa` appends three fields to an ADS-B target and to no other: NoTrack 0, Source 1 and the RSSI. The data port added NoTrack in its protocol version 8 and Source and RSSI in version 9, after AcftType as `[,<NoTrack>[,<Source>,<RSSI>]]`, and XCSoar numbers the sources 0 FLARM, 1 ADS-B, 3 ADS-R, 4 TIS-B and 6 Mode-S (`ParsePFLAA` in `src/Device/Driver/FLARM/StaticParser.cpp` and `SourceType` in `src/FLARM/Traffic.hpp`, at XCSoar `03d7f982`). ADS-B has no no-track flag, so NoTrack is 0. An RSSI left empty is read as unknown. Bravo fills it with the level in dBm of the 1090 MHz message that last placed the target, and leaves it empty when the LR2021 gave that message none, since it levels only the newest frame of a FIFO read (`hardware/parts/lr2021/README.md`). A Go leaves it empty for a target Bravo relays, because the `observation` frame carries no RSSI. A Bluetooth RSSI never goes there. Within 82 characters the RSSI gives way first, then the callsign shortens, and no other field ever does. Lyusupov's SoftRF sends its radio's RSSI in the same field on ESP32 and nRF52 (`PFLAA_EXT1_ARGS`), and nothing on the STM32 of a Retro.

Every other source keeps the eleven fields it has always sent. Source has no code for ADS-L or the uplink. XCSoar's parser reads a missing Source and an empty one the same way, as FLARM, so it shows a FLARM target as FLARM without a 0 (`ParsePFLAA` in `src/Device/Driver/FLARM/StaticParser.cpp` at XCSoar `6e0c8328`). No sender we found writes an empty Source, and SkyDemon drops the connection on a field value it does not expect: "If we encountered a 3 we would indeed abort the connection", of IDType (Tim Dawson, SkyDemon forum topic 32592, 16 December 2020). So the bytes a Go sends for ADS-L, FLARM and uplink targets do not change.

The ADS-B form is the one GA/TAS sends on every `$PFLAA`: NoTrack, Source, and an empty RSSI (`src/lib/dataport/ace/src/dataport.cpp` in rvt/openace at `87740c7b`, GPL, read for the fact). Its README marks SkyDemon as working over Bluetooth and over DataPort (NMEA) Wi-Fi, "Tested Frequently during flight". It marks XTrack over Bluetooth, "Tested for connectivity", and SeeYou Navigator over Bluetooth, "GPS and traffic targets shown". That Bluetooth column is not labelled NMEA, but the same README says GDL90 over Bluetooth is not set up, and the GA/TAS configuration page says Bluetooth carries the DataPort NMEA stream (`src/SystemGUI/src/components/modules.js`). Both SoftRF trees send the three fields too: lyusupov's nRF52 build since release 1.0 (October 2021), and moshe-braner's on every `$PFLAA`. Enroute reads the first eleven fields and ignores the rest (`processFLARMMessagePFLAA`). XCSoar reads the three fields when they are there, whatever the version. No EFB was found to fail on fields past AcftType. #182 checks it on the device.

## 82 characters

NMEA 0183 caps a sentence at 82 characters from the `$` to the CRLF, and `kNmeaSentenceCeilingBytes` keeps that figure.

In a `$PFLAA` only the callsign is free text, so the callsign gives way. When the sentence would pass 82 characters, the callsign loses characters from its end, and the `!` goes when none fits. With alarm level 3, a 6-character height, and speed, climb and track at their widest, a 14-character ADS-L callsign and an 8-character ADS-B one fit whole while north and east each stay under 1000 km. Past that, under the same bounds, ADS-L loses up to 3 characters and ADS-B up to 2, far beyond any range `core/traffic/sanity.h` keeps a target at. At the widest the parameter types allow, an alarm level of 255 and an 11-character height, an ADS-B sentence is 82 characters with no callsign at all, so the fields alone never pass the cap. `test/core/test_nmea_length.cpp` pins each case. XCSoar copies 15 characters of the ID field and parses the hex address out of them, and it still steps over the whole field, so a callsign of any length leaves the fields after it where they were (`CSVLine::Read` in `src/io/CSVLine.cpp`).

Every formatter here builds its sentence in its own buffer and copies it out only when the sentence and its NUL fit in `capacity`. Otherwise it returns 0 and writes nothing.

## The category ADS-B sends

`adsb_emitter_to_adsl_category` reads the emitter category of an ADS-B identification message, a type code from 1 to 4 and a 3-bit category, into ADS-L G.1.3, which is the one category the tree holds. `adsl_category_to_alptas_type` takes it on to `$PFLAA` from there. The type code and category pairs are those of The 1090 Megahertz Riddle, table "Wake vortex in ADS-B identification and category message" ([mode-s.org](https://mode-s.org/1090mhz/content/ads-b/2-identification.html)). The rows below use the usual A0 to D7 shorthand, a letter for the type code (A is 4, B is 3, C is 2, D is 1, which is all reserved) and a digit for the category. Neither the Riddle nor the GDL 90 table below prints those letters. The Riddle rounds the type code 4 weights to kilograms. The pounds below are Table 11 of the GDL 90 Data Interface Specification, 560-1058-00 Rev A, which lists the same three groups in order ([faa.gov](https://www.faa.gov/sites/faa.gov/files/air_traffic/technology/adsb/archival/GDL90_Public_ICD_RevA.PDF)).

Where an ADS-B emitter category or an ALP-TAS aircraft type covers G.1.3 categories that split one kind of aircraft by weight, ADS-B reads as the heavier one and ALP-TAS as the lighter one, because ADS-B equipage is typical of heavier aircraft and ALP-TAS of lighter ones. That rule decides only between the rotorcraft, 3 and 15, and the UAS, 11, 12 and 13, and the rows it decides say so. Every other row has its own reason.

| ADS-B | Kind | G.1.3 | Why |
|---|---|---|---|
| A1 | light, under 15 500 lb | 1 | G.1.3 draws its line between 1 and 2 at the same 7031 kg, 15 500 lb |
| A2 to A5 | small, large, high vortex large, heavy | 2 | at or above that line |
| A6 | high performance, over 5 g and 400 kt | 2 | ADS-B gives it no weight, and an aircraft pulling 5 g at 400 kt is nearly always a jet of 7031 kg or more |
| A7 | rotorcraft | 15 | the rule: Issue 2 split rotorcraft into light, 3, and heavy, 15, and ADS-B reads the heavier |
| B1 | glider, sailplane | 4 | |
| B2 | lighter-than-air | 5 | |
| B3 | parachutist, skydiver | 8 | |
| B4 | ultralight, hang-glider, paraglider | 6 | it spans G.1.3's 6, 7, 16 and 17, and 6 is the one that names an ultralight, the first kind the category lists and the one most likely to carry a transponder |
| B6 | unmanned aerial vehicle | 12 | the rule: ADS-B carries no EU operational category, so the specific category, heavier than ALP-TAS's open 11 |
| B7 | space vehicle | 0 | G.1.3 has no value for it |
| A0, B0, B5, C0 to C7, D0 to D7 | no information, reserved, surface vehicles and obstacles | 0 | |

Bravo's own transponder is read with `adsb_emitter_to_own_category` instead, because the category it proposes becomes the one a Go transmits for the pilot's own aircraft. Where traffic reads the heavier side of a weight split, own-ship takes the lighter: A6 is 1, A7 is 3 and B6 is 11. A pilot who flies a heavy helicopter picks `HEAVY HELI` on the glass.

## The type a tablet draws

`adsl_category_to_alptas_type` turns the G.1.3 category of a target, from any source, into the AcftType field of `$PFLAA`. The field runs from 0 to F (`AircraftType` in XCSoar's `src/FLARM/Traffic.hpp`), and the tablet draws the target with the icon of its type. G.1.3 is the table in `docs/reference/ads-l_4_srd860_issue_2.md`. G.1.3 splits fixed wing by weight at 7031 kg, and AcftType splits it by engine, so a light jet or turboprop reaches the tablet as a piston aircraft and a heavy piston aircraft as a jet. Before #216 the table sent category 2 as a piston aircraft, so every airliner Bravo heard was drawn as one, and it sent category 6 as an airship and 14 to 17 as no type.

| G.1.3 | Kind | AcftType | Why |
|---|---|---|---|
| 0 | no information | 0 | XCSoar reads 0 as unknown and A as a flying saucer (`AircraftType` in `src/FLARM/Traffic.hpp` at XCSoar `71cd8bca`). The tree has always sent 0 |
| 1 | light fixed wing, under 7031 kg | 8, reciprocating engines | most aircraft under 7031 kg have piston engines, though light turboprops and jets fly there too. Tow planes, AcftType 2, and drop planes, AcftType 5, fall here as well, but G.1.3 does not mark them |
| 2 | small to heavy fixed wing, 7031 kg and over | 9, jet or turboprop | nearly every aircraft that heavy is a jet or a turboprop |
| 3 | light rotorcraft | 3, helicopter, gyrocopter, rotorcraft | |
| 4 | glider, sailplane | 1, glider, motor glider | |
| 5 | lighter-than-air | B, balloon | G.1.3 does not tell a balloon from an airship, AcftType C, and balloons are the more common |
| 6 | ultralight and motorized hang-glider | 8, reciprocating engines | an ultralight, and a trike, fly on a reciprocating engine. AcftType 6 is a hang glider with no engine |
| 7 | paraglider | 7, paraglider | |
| 8 | parachutist, skydiver, wingsuit | 4, skydiver | |
| 9 | eVTOL, UAM | 3, rotorcraft | it carries people, takes off on its rotors and hovers like a helicopter. D would tell the pilot it is a drone. An electric aircraft carries little mass, so wherever skyBlip groups categories by weight it goes with light rotorcraft. Today nothing on the glass, the own-ship drawings or the alarm groups them |
| 10 | gyrocopter | 3 | XCSoar names 3 helicopter or rotorcraft, and a gyrocopter is a rotorcraft |
| 11 to 13 | UAS open, specific and certified | D, UAV | |
| 14 | model plane | D, UAV | a model plane flies with no one on board, which is what D names |
| 15 | heavy rotorcraft | 3 | |
| 16 | hang-glider | 6, hang glider | |
| 17 | paramotor | 7, paraglider | it flies a paraglider wing, and the field has no powered paraglider |
| 18 and over | outside G.1.3 | 0 | |

`alptas_type_to_adsl_category` goes the other way, from the aircraft type of an ALP-TAS frame to G.1.3. The frame's type is 5 bits, from 0 to 19, and it agrees with the `$PFLAA` AcftType from 0 to 9. Past 9 they part: the frame names 10 a gyrocopter and 14 an airfield, and adds 16 to 19 for eVTOL and the three UAS classes, which map onto G.1.3's own 9 and 11 to 13. An AcftType names an engine or a kind of wing and gives no weight. Where the table above sends one category to a type, that category is the one read back. Where it sends several, the rows below say which one is read.

| Type | Kind | G.1.3 | Why |
|---|---|---|---|
| 0, 14 | undefined, airfield | 0 | |
| 1 | glider, motor glider | 4 | |
| 2, 5 | tow plane, drop plane | 1 | tow and drop planes nearly all weigh under 7031 kg. A turboprop drop plane, such as a Caravan or a Twin Otter, then reaches `$PFLAA` as AcftType 8 |
| 3 | helicopter, rotorcraft | 3 | the rule decides between 3 and 15, the light and heavy rotorcraft: ALP-TAS reads the lighter. It is also sent for 9 and 10, and light rotorcraft is the commonest of the four |
| 4 | skydiver | 8 | |
| 6 | hang glider | 16 | 16 is the one category sent as 6. Issue 1 put the hang glider with the paraglider in 7 |
| 7 | paraglider | 7 | it is sent for 7 and 17, and a paramotor is the rarer of the two |
| 8 | reciprocating engines | 1 | it is sent for 1 and 6, and most piston aircraft are light fixed wing. An ultralight is a light aircraft too |
| 9 | jet or turboprop | 2 | 2 is the one category sent as 9, so a jet reaches `$PFLAA` as a jet again. A light jet or turboprop lands in the heavier weight class |
| 10 | gyrocopter | 10 | |
| 11, 12 | balloon, airship | 5 | |
| 13 | UAV | 11 | the rule: it is sent for 11 to 14, and ALP-TAS reads the lighter, the open category |
| 15 | static obstacle | 0 | G.1.3 has no value for it |
| 16 | eVTOL or UAM | 9 | |
| 17, 18, 19 | UAS open, specific, certified | 11, 12, 13 | |
| past 19 | outside the frame's table | 0 | |

## What else an ALP-TAS frame says

The frame's urgency becomes G.1.4's emergency: Pan-Pan and Mayday both read as a general emergency, since G.1.4 has nothing short of distress, and a high urgency, used for a strong sink, reads as none. The turn rate the sender reports goes to the traffic table, which takes it as the target's turn at once instead of waiting for two tracks (`core/traffic/table.cpp`).

A frame describes its sender's fix, and goes out up to 1.2 s later, in the second of ALP-TAS's two windows. `alptas_fix_age_ms()` counts the quarter seconds between the fix field and the reception, and `TrafficService` dates the position that much earlier, so the extrapolation starts from the instant it was true. Without a PPS there is no reception instant to count from, and the age is 0. An age past 2.5 s is a clock disagreement, not a late frame, and is read as 0 too.

A sender in privacy mode draws a 32-bit address and moves it every 2 to 10 s, sending in each frame the random byte the next address is made from. `AlptasReceiver` keeps the next address of each of 16 random senders for 20 s, and shows a sender under the 24 bits of the first address it heard, so the target does not restart every few seconds.

The stealth flag asks that the sender's identity and performance not reach airborne receivers. A stealth target that is not the alarm reaches `$PFLAA` with no name and no climb, and the radar draws it with no climb. Its track and speed stay, because the collision geometry needs them. The no-track flag asks that the sender not be stored or forwarded. The target is shown, and the flight log and the radio log keep nothing that names it.

A frame whose SIL, SDA or NIC is set has no byte of zeros to check its key with (`protocols/alptas/README.md`), so `alptas_decode` refuses one that places its sender beyond `traffic::plausible_range_m()`. A wrong key lands within 30 km about once in 130 frames.

## The flight state every decoder owes

Every adapter here writes `AircraftObservation::flight_state` as an ADS-L G.1.2 code, and `Unknown` (0) when its wire carries no state, because that field is all `core/traffic` reads to decide what the ground means (`core/traffic/README.md`). ADS-L and the uplink record carry G.1.2 itself, and an ADS-B track's `AirGround` maps onto it in `adsb_observation.cpp`.

ALP-TAS carries a two-bit movement mode of its own: 0 undefined, 1 on the ground, 2 flying, 3 circling or manoeuvring. SoftRF's `latest_encode` (moshe-braner, `Legacy.cpp`) never sends 0 (`protocols/alptas/frame.h`). `alptas_flight_state` reads 0 as `Unknown`. It used to read it as on the ground, and an aircraft on the ground is never graded, so the one value no sender uses was the one that silenced the alarm.

