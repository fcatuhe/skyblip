# core/protocol

Adapters between skyBlip's model and the stacks in `protocols/`, which hold what the specifications decide (`protocols/README.md`). The choices skyBlip made around those stacks live here, with the sentences a tablet reads.

| File | What it is |
|---|---|
| `adsl.{h,cpp}` | own-ship as an ADS-L Traffic payload, the receiver's solution as measured and named by its own quarter second, with accuracy from the receiver's DOP. A received Traffic payload as an `AircraftObservation`. The Status this device declares |
| `callsign.{h,cpp}` | the name ADS-L has no field for (below) |
| `adsl_uplink.{h,cpp}` | G.3's uplink over observations. `decode` drops as stale a record older than 10 s or dated after the second it was heard in. `encode` dates each record when sent, which G.3 allows only for traffic from systems other than ADS-L, so only the simulated skyPost and the tests use it |
| `air.{h,cpp}` | one M-band dwell that hears ADS-L and ALP-TAS behind one shared sync window and names the system from the sync tail. The ADS-L length is read separately from its sync. It rejects a length with more than two doubtful Manchester pairs or no supported size consistent with the clean pairs. It passes the length and its erasure mask to the CRC decoder. It also holds the shared M-band framing helper for simulated peers and tests. `kAdslTxSync` gives the Go the transmit sync register and `adsl_m_band_fifo()` encodes the length and packet for its FIFO. On the O band, a burst is an uplink packet only when its §D.1.1 length byte names one of G.3's packet lengths, under the CRC or the FEC |
| `alptas.{h,cpp}` | an ALP-TAS position as an observation, and `AlptasReceiver`, which follows a random address (below). skyBlip only receives ALP-TAS, so the simulator builds the frames its aircraft send (`simulator/world/alptas_out.h`) |
| `adsb_observation.{h,cpp}` | an ADS-B track as an observation, in ADS-L units |
| `adsb_category.{h,cpp}` | an ADS-B emitter category in ADS-L's terms (below) |
| `nmea_out.{h,cpp}` | `$PFLAU`, `$PFLAA`, `$PGRMZ`, `$LK8EX1`, `$GPRMC` and `$GPGGA` |

`from_own`, `from_own_status` and `from_own_callsign` answer false and leave the packet untouched without a `settings::AirAddress`, so a unit the server has not activated builds no frame (`core/settings/README.md`). `RadioService` hands all three the same `settings::on_air()` table and address, so a receiver files the Traffic, the Status and the name under one target (`core/settings/README.md`, The ICAO address).

## Own-ship's Traffic is the solution as measured

`from_own` writes the newest solution unextrapolated: its position, height, speed and track, with a Timestamp naming its own quarter second from `own.utc_s` and `own.utc_ms` (G.1.1). A stamp more than 10 ms from a quarter builds nothing, since no quarter dates it to G.1.1's 10 ms, and `RadioService` counts that position in `air.tx_held_for_fix`. `from_own` decides nothing about age: `RadioService` arms a position only while it will be at most 500 ms old on air (G.1.16, `core/timing/README.md`, The age of a position). The climb is the one derived field, differenced over a window by own-ship, with its own validity (G.1.9).

## The Status payload

`from_own_status` hands `adsl::encode_status` the values below for §G.2's payload type 3. The layout and the version are the stack's (`protocols/adsl/README.md`).

| Field | Value | Why |
|---|---|---|
| Max. Protocol Version | 1 | this device reads Issue 2 (`protocols/adsl/README.md`, Which issue a payload is) |
| M-band receive | 2, Partial | the dwells hear 868.2 MHz from 450 to 822 ms and 868.4 MHz from 827 to 1000 ms, about half the direct slot over both channels (`timing::Scheduler::direct_heard_permille()`) |
| O-band LDR receive | 0 | not received |
| O-band HDR receive | 0 | heard only in the uplink slot, which §G.2.4 leaves out |
| ADS-L 4 Mobile | 0 | none |
| E-conspicuity receive | bit 0, and bit 4 while the paired Bravo is live | ALP-TAS is decoded here, and a live Bravo hears ADS-B 1090 in the same aircraft. §G.2.6 leaves an unknown capability unset |
| E-conspicuity transmit | bit 4 once the aircraft's transponder is known to send ADS-B 1090, else 0 | the unit sends only ADS-L, and §G.2.7 counts the systems installed in the same vehicle |
| XPDR | 2, Mode S, once the aircraft's transponder is known, else 0 | 0 also means unknown |
| Traffic Uplink client | 1 | the uplink is decoded and drawn |
| FIS-B client | 0 | not decoded |

The aircraft's transponder is known once a paired Bravo on the ground names the transponder it hears loudest at the configured address (F.2.3, `products/skyblip_bravo/README.md`, Own transponder). An ICAO address alone proves nothing, since aircraft with no transponder are assigned one. A 24-bit address heard on 1090 MHz is a Mode S transponder, and an identification from it is an extended squitter, so ADS-B out. `bus::State::aircraft_transponder` keeps the confirmation while the entered address stays the same and the unit runs, so it holds through the flight. `RadioService` checks that it is for the address entered on the pass that builds the Status, since `BravoService` runs after it. Without a confirmation a Go declares neither, which §G.2.7 and §G.2.8 read as unknown.

## Writing for an Issue 1 neighbour

§B.5 asks own Traffic to go out in a version every close neighbour reads (`core/traffic/README.md` finds the lowest). With no close Issue 1 client, a position goes out in the lowest version with the same encoding (§E.1.1), so one with category 14 to 17 or an unknown field goes as version 1.

Beside a close Issue 1 client, `issue1_form` re-codes the payload as version 0 only where Issue 1 has a truthful code for every field (`protocols/adsl/README.md`, Soft versioning). Otherwise the position is held, counted in `air.tx_held_for_b5` and retried like any burst that did not go out (`core/timing/README.md`), so the device never claims a value it does not have. The Issue 1 form keeps the table and address, since Issue 1's F.2.2 also has table 5 for ICAO. §B.5 covers only Traffic, so Status and the name always go out as version 1.

## The name ADS-L has no field for

ADS-L carries no callsign. F.2.1 assigns payload 66 to "OGN Diagnostics" and leaves its definition out of the specification, so skyBlip follows Jalocha's layout (`oss/nrf52-ogn-tracker/src/adsl.h:120-128`): the record in `protocols/adsl_edge/registration.h`, with the 6+24 sender address the position frames carry, telemetry type 1, info type 5, and 14 characters. It rides the same frame as Traffic, so the radio needs no new dwell. `from_own_callsign` cuts the name at the first character the menu does not type.

`callsign_of` is a trust boundary. It refuses text off the air whole when any byte is not a callsign character, so no control character reaches the glass and no comma reaches a `$PFLAA` ID field. `is_callsign_character` is the set the radar menu types (blank, dash, `A`-`Z`, `0`-`9`), and the pilot's own callsign keeps to it. The name goes to `core/traffic/callsigns.h` keyed on the sender's table and address, never into an `AircraftObservation`, because a name belongs to an address and outlives a position.

An OGN tracker reads the name whatever the address table, and sends its own every 40 to 60 s (`ProcessRxADSL`, `oss/nrf52-ogn-tracker/src/proc.cpp:715-733`).

## The IDType a tablet reads

`address_mapping_table_to_id_type` sends an ICAO address (table 5) as IDType 1 and every other as 2, FLARM. SkyDemon draws nothing for any IDType but 1 or 2 (`oss/SoftRF-moshe-braner/.../libraries/OGN/ads-l.h:657-658`). FLARM is the cheaper untruth, because nothing downstream checks a FLARM ID against an aircraft register. So this device's table 58 and an OGN tracker's table 7 draw as FLARM.

## A target with no altitude

`format_pflaa` sends nothing for a target with no altitude, since XCSoar drops a `$PFLAA` with an empty RelativeVertical and a 0 would draw it level with us (`ParsePFLAA` in `src/Device/Driver/FLARM/StaticParser.cpp` at XCSoar `6e0c8328`). `format_pflau` still names it as the threat with RelativeVertical empty, and the same parser keeps its bearing and distance.

## Where a target came from

`format_pflaa` appends NoTrack 0, Source 1 and the RSSI to an ADS-B target only, as `[,<NoTrack>[,<Source>,<RSSI>]]` after AcftType, from data port versions 8 and 9. XCSoar numbers the sources 0 FLARM, 1 ADS-B, 3 ADS-R, 4 TIS-B and 6 Mode-S (`SourceType` in `src/FLARM/Traffic.hpp` at XCSoar `03d7f982`). ADS-B has no no-track flag, so NoTrack is 0.

The RSSI is the level in dBm of the 1090 MHz message that last placed the target. It stays empty, read as unknown, when the LR2021 gave that message no level (`hardware/parts/lr2021/README.md`) and on a Go for a target Bravo relays, since the `observation` frame carries none. A Bluetooth RSSI never goes there.

Other sources keep their eleven fields. Source has no code for ADS-L or the uplink, and SkyDemon drops the connection on a value it does not expect (Tim Dawson, SkyDemon forum topic 32592, 16 December 2020). GA/TAS (rvt/openace `src/lib/dataport/ace/src/dataport.cpp` at `87740c7b`) and both SoftRF trees send the three fields, and no EFB was found to fail on them. #182 checks it on the device.

## 82 characters

NMEA 0183 caps a sentence at 82 characters from the `$` to the CRLF (`kNmeaSentenceCeilingBytes`). In a `$PFLAA` the RSSI gives way first, then the callsign loses characters from its end, and the `!` goes when none fits. No other field gives way. At the widest the parameter types allow, the fields alone, without the RSSI, fit. With alarm level 3 and a 6-character height, a whole callsign fits within any range `core/traffic/sanity.h` keeps (`test/core/test_nmea_length.cpp`). XCSoar steps over the whole ID field, so a callsign of any length leaves the fields after it in place (`CSVLine::Read` in `src/io/CSVLine.cpp`).

Every formatter builds its sentence in its own buffer and copies it out only when the sentence and its NUL fit in `capacity`. Otherwise it returns 0 and writes nothing.

## The category ADS-B sends

`adsb_emitter_to_adsl_category` reads the type code (1 to 4) and 3-bit category of an ADS-B identification message into G.1.3, the one category the tree holds. The pairs are The 1090 Megahertz Riddle's table "Wake vortex in ADS-B identification and category message" ([mode-s.org](https://mode-s.org/1090mhz/content/ads-b/2-identification.html)), and the pounds are Table 11 of the GDL 90 Data Interface Specification, 560-1058-00 Rev A ([faa.gov](https://www.faa.gov/sites/faa.gov/files/air_traffic/technology/adsb/archival/GDL90_Public_ICD_RevA.PDF)). The rows use the usual A0 to D7 shorthand, which neither source prints: a letter for the type code (A is 4, B is 3, C is 2, D is 1, which is all reserved) and a digit for the category.

Where a category covers G.1.3 categories that split one kind of aircraft by weight, ADS-B reads as the heavier and ALP-TAS as the lighter, because ADS-B equipage is typical of heavier aircraft and ALP-TAS of lighter ones. This rule decides only the rotorcraft, 3 and 15, and the UAS, 11 to 13.

| ADS-B | Kind | G.1.3 | Why |
|---|---|---|---|
| A1 | light, under 15 500 lb | 1 | G.1.3 splits 1 from 2 at the same 7031 kg |
| A2 to A5 | small, large, high vortex large, heavy | 2 | |
| A6 | high performance, over 5 g and 400 kt | 2 | an aircraft pulling 5 g at 400 kt is nearly always a jet of 7031 kg or more |
| A7 | rotorcraft | 15 | the rule |
| B1 | glider, sailplane | 4 | |
| B2 | lighter-than-air | 5 | |
| B3 | parachutist, skydiver | 8 | |
| B4 | ultralight, hang-glider, paraglider | 6 | of G.1.3's 6, 7, 16 and 17, the ultralight is the most likely to carry a transponder |
| B6 | unmanned aerial vehicle | 12 | the rule |
| B7 | space vehicle | 0 | |
| A0, B0, B5, C0 to C7, D0 to D7 | no information, reserved, surface vehicles and obstacles | 0 | |

`adsb_emitter_to_own_category` reads the transponder Bravo proposes as the pilot's own aircraft, so it takes the lighter side instead: A6 is 1, A7 is 3 and B6 is 11. A pilot of a heavy helicopter picks `HEAVY HELI` on the glass.

## The type a tablet draws

`adsl_category_to_alptas_type` turns a target's G.1.3 category (`docs/reference/ads-l_4_srd860_issue_2.md`) into `$PFLAA`'s AcftType, 0 to F, which picks the tablet's icon (`AircraftType` in XCSoar's `src/FLARM/Traffic.hpp` at `71cd8bca`). G.1.3 splits fixed wing by weight at 7031 kg and AcftType by engine, so a light jet reaches the tablet as a piston aircraft and a heavy piston aircraft as a jet.

| G.1.3 | Kind | AcftType | Why |
|---|---|---|---|
| 0 | no information | 0 | XCSoar reads 0 as unknown and A as a flying saucer |
| 1 | light fixed wing, under 7031 kg | 8, piston | most are piston. G.1.3 does not mark tow planes (2) or drop planes (5) |
| 2 | fixed wing, 7031 kg and over | 9, jet or turboprop | |
| 3 | light rotorcraft | 3, rotorcraft | |
| 4 | glider, sailplane | 1, glider | |
| 5 | lighter-than-air | B, balloon | balloons outnumber airships (C) |
| 6 | ultralight, motorized hang-glider | 8, piston | it has an engine, and AcftType 6 is a hang glider with none |
| 7 | paraglider | 7, paraglider | |
| 8 | parachutist, skydiver, wingsuit | 4, skydiver | |
| 9 | eVTOL, UAM | 3, rotorcraft | it carries people and hovers. D would call it a drone |
| 10 | gyrocopter | 3, rotorcraft | |
| 11 to 14 | UAS open, specific, certified, model plane | D, UAV | no one on board |
| 15 | heavy rotorcraft | 3, rotorcraft | |
| 16 | hang-glider | 6, hang glider | |
| 17 | paramotor | 7, paraglider | the field has no powered paraglider |
| 18 and over | outside G.1.3 | 0 | |

`alptas_type_to_adsl_category` reads the 5-bit aircraft type of an ALP-TAS frame, 0 to 19, back to G.1.3. It agrees with AcftType from 0 to 9. Past 9 the frame has its own values, listed below. Where the table above sends one category to a type, that category is read back, and where it sends several, the row says which.

| Type | Kind | G.1.3 | Why |
|---|---|---|---|
| 0, 14, 15 | undefined, airfield, static obstacle | 0 | |
| 1 | glider | 4 | |
| 2, 5 | tow plane, drop plane | 1 | nearly all under 7031 kg |
| 3 | rotorcraft | 3 | the rule, and the commonest of 3, 9, 10 and 15 |
| 4 | skydiver | 8 | |
| 6 | hang glider | 16 | |
| 7 | paraglider | 7 | a paramotor (17) is rarer |
| 8 | piston | 1 | most piston aircraft are light fixed wing |
| 9 | jet or turboprop | 2 | |
| 10 | gyrocopter | 10 | |
| 11, 12 | balloon, airship | 5 | |
| 13 | UAV | 11 | the rule |
| 16 | eVTOL or UAM | 9 | |
| 17, 18, 19 | UAS open, specific, certified | 11, 12, 13 | |
| past 19 | outside the frame's table | 0 | |

## What else an ALP-TAS frame says

Pan-Pan and Mayday both read as G.1.4's general emergency, since G.1.4 has nothing short of distress. High urgency, used for a strong sink, reads as none. The reported turn rate goes to the traffic table, which takes it at once instead of waiting for two tracks (`core/traffic/table.cpp`).

A frame goes out up to 1.2 s after its sender's fix. `alptas_fix_age_ms()` counts the quarter seconds from the fix field to the reception, and `TrafficService` dates the position that much earlier. Without a PPS, or past 2.5 s, which is a clock disagreement, the age is 0.

A sender in privacy mode moves its 32-bit address every 2 to 10 s and sends the random byte the next one is made from. `AlptasReceiver` keeps the next address of up to 16 senders for 20 s and shows each under the 24 bits of the first address it heard, so the target does not restart.

A stealth target that is not the alarm reaches `$PFLAA` and the radar with no name and no climb, and keeps the track and speed the collision geometry needs. A no-track target is shown, but the flight log and the radio log keep nothing that names it.

A frame whose SIL, SDA or NIC is set has no zero byte to check its key with (`protocols/alptas/README.md`), so `alptas_decode` refuses one beyond `traffic::plausible_range_m()`. A wrong key lands within 30 km about once in 130 frames.

## The flight state every decoder owes

Every adapter writes `AircraftObservation::flight_state` as a G.1.2 code, and `Unknown` (0) when its wire carries none, because `core/traffic` reads only that field to decide what the ground means (`core/traffic/README.md`). `alptas_flight_state` reads ALP-TAS's movement mode 0, undefined, as `Unknown`, never as on the ground, since the alarm never grades an aircraft on the ground.

## $LK8EX1, $GPRMC and $GPGGA

`format_lk8ex1` sends raw pressure in Pa, altitude in m on 1013.25 hPa, vertical speed in cm/s, temperature in degrees C and the battery, in the LK8000 order SoftRF lyusupov (`src/protocol/data/NMEA.cpp:246-256`) and GXAirCom (`src/main.cpp:4370-4385`) agree on. Each field has a not-available value, and a reading clamps one short of it so it never reads as missing. The altitude bounds, -1000 to 99998 m, and temperature bounds, -99 to 98 C, are SoftRF's. The battery field is volts below 1000 and percent plus 1000 from 1000 up. skyBlip sends percent, as SoftRF MB and GXAirCom do, because the glass shows percent.

`format_gprmc` and `format_gpgga` give own-ship's position to an EFB with no GNSS. Neither is written without both `fix_valid` and `utc_valid`, because a consumer stamps its log with the time.
