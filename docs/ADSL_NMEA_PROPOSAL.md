# ADS-L NMEA: a proposal for the link to a display

This proposal carries ADS-L traffic from receiving equipment to a pilot's display without translating it into FLARM's fields. It replaces the position, traffic, alarm, barometer and battery sentences a display needs today.

Draft 1, 13 November 2026. Submitted to EASA and the ADS-L governing body by François Catuhe for skyBlip. Sources: [skyblip.eu](https://skyblip.eu) and [github.com/fcatuhe/skyblip](https://github.com/fcatuhe/skyblip).

## One family replaces the current stack

ADS-L 4 SRD-860 defines the radio link. It does not define the link from receiving equipment to an electronic flight bag (EFB), the navigation display on a pilot's phone or tablet.

To reach today's displays, an ADS-L device sends NMEA 0183 position sentences, FLARM traffic and alarm sentences, Garmin's pressure-altitude sentence and LK8000's sensor sentence. Most of the ADS-L Traffic and Status fields have no place in that stack.

We propose one NMEA 0183 family under a code held by the ADS-L governing body. It has seven device-to-display data sentences, one display-to-device request and one device-to-display answer. The provisional names are `$PDSL` followed by one letter.

A device sends the current FLARM stack by default. A display requests the new family on its connection. Other connections keep their existing output. There is no device setting to change.

## Scope and source clauses

The family covers Traffic payloads (G.1), Status payloads (G.2), traffic received through the relay flag or Traffic Uplink (F.2, G.3), own-ship, device alarms, barometer readings and device status.

FIS-B is not carried. G.4 permits up to 217 payload bytes, which do not fit one 82-character NMEA sentence. Remote Identification is not carried. G.5 reserves its detailed definition for a later issue.

Clause numbers without another document name refer to ADS-L 4 SRD-860 Issue 2. Field tables retain that issue's values. Device alarms, sensors, connection negotiation and the proposed sentence names are defined here, not by ADS-L.

## The current stack loses ADS-L fields

FTD-012 version 7.19, section 7, defines a FLARM unit's output as NMEA 0183 `$GPRMC`, `$GPGGA` and `$GPTXT`, FLARM's proprietary `$PFLA` sentences, and Garmin's proprietary `$PGRMZ`. Section 8.10 adopts `$PGRMZ` for barometric altitude. `FLA` is FLARM's NMEA-assigned manufacturer code.

skyBlip sends these six sentence types on its once-per-second output pass:

| Sentence | Defined by | Carries | Output in skyBlip |
|---|---|---|---|
| `$GPRMC` | NMEA 0183 | own position, ground speed, track, date and time | 1 Hz with a fix and UTC |
| `$GPGGA` | NMEA 0183 | own position, GNSS altitude and fix | 1 Hz with a fix and UTC |
| `$PFLAU` | FLARM, FTD-012 | targets heard, fix, transmitting and the one worst threat | 1 Hz |
| `$PFLAA` | FLARM, FTD-012 | one target relative to own-ship | at most 10 sentences per second |
| `$PGRMZ` | Garmin, adopted by FTD-012 section 8.10 | own pressure altitude on 1013.25 hPa, in feet | 1 Hz, only with a barometer |
| `$LK8EX1` | LK8000, outside FTD-012 | pressure, pressure altitude, vertical speed, temperature and battery | 1 Hz |

skyBlip adds `$LK8EX1` because it is the only sentence XCSoar and LK8000 read a battery level from. Its name breaks NMEA 0183's proprietary naming rule: `P` followed by a registered three-letter manufacturer code. Displays match sentence names as strings, so they still read it.

XCSoar's generic parser reads all six sentence types whatever driver the pilot selects. It does not read the standard `$--XDR` transducer sentence. Source: XCSoar `src/Device/Parser.cpp`, master, read 13 November 2026.

skyBlip holds 48 targets. Each must reach the display within 5 s, the age at which the device stops alarming on it. The output rotates through the table, sending at most 10 `$PFLAA` sentences per second. Sources: `firmware/products/common/services/nmea.cpp` and `firmware/core/protocol/nmea_out.cpp`.

| ADS-L carries | The stack gives it |
|---|---|
| absolute position and altitude above the WGS-84 ellipsoid (G.1.5, G.1.7) | north, east and up in metres from the receiving aircraft |
| time of the target's fix (G.1.1) | nothing, a `$PFLAA` is undated |
| 64 AMT entries (F.2.2) | an ID type of 0 random, 1 ICAO, 2 FLARM: OGN-Tracker at entry 7 and skyBlip at entry 58 both read as FLARM |
| 18 aircraft categories (G.1.3) | 16 FLARM aircraft types, several ADS-L categories merged |
| flight state, emergency status (G.1.2, G.1.4) | no field |
| SIL, design assurance, NIC, horizontal and vertical position accuracy, velocity accuracy (G.1.11 to G.1.16) | no field |
| the Status payload (G.2) | no field |
| relayed or uplinked traffic (F.2 relay flag, G.3) | ordinary traffic |
| a callsign (Issue 3) | a share of the `$PFLAA` ID field, cut first |

Issue 2 has no callsign field. EASA said orally at the 2 October 2026 meeting with skyBlip and FLARM that Issue 3 will carry one. There is no written source for that statement.

## Encoding preserves the payload values

The family carries absolute positions. The display uses own-ship to calculate relative geometry. The device sends targets as heard and never extrapolates them for this output. Each target carries the time of its navigation solution, not the time the NMEA line was written.

Fields follow the Traffic and Status payload layouts. Category and quality indicators are separated into `$PDSLI` because they change less often. Measurements are decoded into metres, metres per second and degrees. Table-valued fields retain their ADS-L values, including their defined "undefined" values.

Latitude and longitude are signed decimal degrees, not NMEA's `ddmm.mmmm` with a separate hemisphere. Each coordinate occupies one field. North and east are positive (G.1.5).

Five decimal places of a degree preserve the G.1.5 coordinates: the maximum rounding error is below half the latitude step of 1/93206 degree and the longitude step of 1/46603 degree. One decimal place preserves ground speed, vertical rate and track: its maximum rounding error is below half the smallest steps of 0.25 m/s (G.1.8), 0.125 m/s (G.1.9) and 360/512 degree (G.1.10).

The timestamp retains G.1.1's number of quarter seconds since the full hour modulo 60. Written in seconds, it ranges from 0.00 to 14.75 in steps of 0.25. The display resolves it against UTC as an ADS-L receiver does. G.3 limits forwarded traffic to an age of 10 s, within the field's 15 s period.

An unavailable measurement is an empty field, never a substituted zero. A table field may carry its table's undefined value. For example, emergency status 0 means undefined (G.1.4), not no emergency.

Every sentence uses the NMEA checksum and ends in CR LF. The checksum is the XOR of the ASCII bytes between `$` and `*`, written as two hexadecimal digits after `*`. The maximum sentence length is 82 characters, including `$`, checksum and CR LF.

Later fields may only be appended. Existing fields do not move. This follows the extension pattern used for `$PFLAA`: NoTrack was appended in data port version 8, then Source and RSSI in version 9 (FTD-012 sections 6.4, 6.5 and 8.2).

## The display opts in on its connection

The device sends the current FLARM stack until the display writes `$PDSLR` on that link. It answers with `$PDSLV`, then sends the new family instead of the stack on that connection. A display that never asks continues to receive the stack. A device that does not know the request ignores it.

The display requests the highest family version it reads. The device answers with the version it will send, the lower of the display's and device's supported versions. This version belongs to the display sentence family. It is separate from ADS-L's radio protocol version (E.1.1, B.5).

## Nine sentence names cover the exchange

Widths below count characters from `$` through CR LF. They are computed at the widest values each field's range allows.

| Sentence | Direction | Carries | Sent | Widest |
|---|---|---|---|---|
| `$PDSLR` | display to device | request: highest family version the display reads | once after connecting | 14 |
| `$PDSLV` | device to display | answer: family version sent, manufacturer, model, firmware | in answer to `$PDSLR` | 65 |
| `$PDSLA` | device to display | a target's Traffic payload | each reception, at most once a second per target | 81 |
| `$PDSLO` | device to display | own-ship's Traffic payload, as transmitted | 1 Hz while the device has a fix | 81 |
| `$PDSLI` | device to display | an address's category, quality indicators and callsign | first reception, on change, every 10 s; own-ship every 10 s | 52 |
| `$PDSLC` | device to display | an address's Status payload | each reception; own-ship every 20 s | 44 |
| `$PDSLW` | device to display | the device's alarm on one target | on change, then 1 Hz while the level is above 0; a level 0 sentence ends it | 23 |
| `$PDSLB` | device to display | own barometer and vertical speed | 1 Hz, only with a barometer | 36 |
| `$PDSLS` | device to display | the device, UTC and its fix | 1 Hz | 56 |

### `$PDSLR` requests the family version

| # | Field | Written as | Source |
|---|---|---|---|
| 1 | family version | unsigned, 1 to 99 | this proposal |

### `$PDSLV` answers with the version and device identity

| # | Field | Written as | Source |
|---|---|---|---|
| 1 | family version | unsigned, 1 to 99 | this proposal |
| 2 | manufacturer | text, up to 16 characters | this proposal |
| 3 | model | text, up to 16 characters | this proposal |
| 4 | firmware version | text, up to 16 characters | this proposal |

Text fields hold printable ASCII except `$`, `*`, `,`, `!`, `\`, `^`, `~`, NMEA's reserved characters.

### `$PDSLA` and `$PDSLO` share one Traffic parser

`$PDSLA` carries a target. `$PDSLO` carries own-ship with the same field order.

| # | Field | Clause | Written as |
|---|---|---|---|
| 1 | source (`A`) / on air (`O`) | F.2, G.3 | `$PDSLA`: `L` ADS-L received directly, `U` ADS-L relayed or uplinked (F.2 relay flag, G.3), `F` FLARM-generation, `P` PilotAware, `N` FANET, `O` OGN, `M` ADS-L 4 Mobile, `A` ADS-B 1090, `R` ADS-R, `T` TIS-B. `$PDSLO`: `L` when own-ship transmitted this payload on ADS-L, empty when the device computed it but did not transmit |
| 2 | Address Mapping Table | F.2.2 | 0 to 63, empty in `$PDSLO` when the device holds no address |
| 3 | address | F.2.2 | 6 hex digits, empty in `$PDSLO` when the device holds no address |
| 4 | timestamp | G.1.1 | seconds, 0.00 to 14.75 in quarters |
| 5 | flight state | G.1.2 | table value |
| 6 | emergency status | G.1.4 | table value |
| 7 | latitude | G.1.5 | degrees, 5 places, north positive |
| 8 | longitude | G.1.5 | degrees, 5 places, east positive |
| 9 | ground speed | G.1.8 | m/s, 1 place, 0 to 236 |
| 10 | altitude | G.1.7 | m, -320 to 61104 |
| 11 | altitude reference | G.1.7 | `G` above the WGS-84 ellipsoid, `P` pressure altitude on 1013.25 hPa |
| 12 | vertical rate | G.1.9 | m/s, 1 place, climb positive, -118 to 119 |
| 13 | ground track | G.1.10 | degrees true, 1 place, empty when the ground speed is zero or invalid (G.1.10) |

In `$PDSLO`, fields 1 to 3 are empty when the device holds no address. Field 1 is also empty when the device computed the payload but did not transmit it, for example because its transmit budget was spent.

Flight state is 0 undefined, 1 on ground or 2 airborne. Value 3 is reserved (G.1.2).

Emergency status is 0 undefined, 1 no emergency, 2 general emergency, 3 lifeguard/medical emergency, 4 no communications, 5 unlawful interference or 6 downed aircraft. Value 7 is reserved (G.1.4).

The address is the pair of AMT and 24-bit address, not the 24-bit address alone. Relevant AMT entries are 0 random/privacy, 5 ICAO, 6 FLARM, 7 OGN-Tracker and 8 FANET. Entries 9 to 63 are manufacturer pages with an 8-bit manufacturer prefix and 16-bit base address (F.2.2).

The source letter describes reception, independently of AMT. An OGN-Tracker heard on ADS-L has source `L` and AMT 7. A target received through the F.2 relay flag or G.3 Traffic Uplink has source `U`.

All sources use this sentence. Fields a source does not supply remain empty or use the relevant table's undefined value. ADS-L traffic always has altitude reference `G`. Reference `P` is available for ADS-B pressure altitude and other sources that have no geometric altitude.

Ground speed, altitude and vertical rate use the decoded values of G.1.6 to G.1.9. Invalid encodings become empty fields: latitude or longitude `0x800000` (G.1.5), altitude `0x3fff` (G.1.7), ground speed `0xff` (G.1.8), and vertical rate `0x1ff` (G.1.9). Track is invalid when speed is zero or invalid (G.1.10).

G.1.7's prose states an altitude ceiling of 61112 m, but its example table gives 61104 m for the highest valid encoding, `0x3ffe`, which decodes to 61104 m.

### `$PDSLI` carries category, quality and callsign

| # | Field | Clause | Written as |
|---|---|---|---|
| 1 | AMT | F.2.2 | 0 to 63 |
| 2 | address | F.2.2 | 6 hex digits |
| 3 | aircraft category | G.1.3 | 0 to 17 |
| 4 | SIL | G.1.11 | table value, 0 to 3 |
| 5 | design assurance | G.1.12 | table value, 0 to 3 |
| 6 | NIC | G.1.13 | 0 to 12 |
| 7 | horizontal position accuracy | G.1.14 | table value, 0 to 7 |
| 8 | vertical position accuracy | G.1.15 | table value, 0 to 3 |
| 9 | velocity accuracy | G.1.16 | table value, 0 to 3 |
| 10 | callsign | interim sources below, then Issue 3 | up to 14 characters of blank, dash, A to Z, 0 to 9 |

SIL is source integrity level. NIC is navigation integrity category. The remaining quality fields carry design assurance and horizontal position, vertical position and velocity accuracy. They retain the clause tables' values rather than converting them into estimated errors.

Aircraft categories retain G.1.3's distinctions:

| Value | Category | Value | Category |
|---|---|---|---|
| 0 | no emitter category information available | 9 | eVTOL / UAM |
| 1 | light fixed wing, under 7031 kg | 10 | gyrocopter |
| 2 | small to heavy fixed wing, 7031 kg or above | 11 | UAS Open category |
| 3 | light rotorcraft | 12 | UAS Specific category |
| 4 | glider / sailplane | 13 | UAS Certified category |
| 5 | lighter-than-air | 14 | model plane |
| 6 | ultralight and motorized hang-glider | 15 | heavy rotorcraft |
| 7 | paraglider | 16 | hang-glider |
| 8 | parachutist / skydiver / wingsuit | 17 | paramotor |

The callsign comes last so its length and character set can follow Issue 3 without moving another field. Until then, a device fills it from received identification: OGN-Tracker and skyBlip payload 66 records, or ADS-B identification messages. F.2.1 assigns payload 66 to OGN Diagnostics and leaves its definition outside the specification.

The identity sentence is sent on first reception, on change and every 10 s. Own-ship's identity is sent every 10 s.

### `$PDSLC` carries the Status payload

| # | Field | Clause | Written as |
|---|---|---|---|
| 1 | AMT | F.2.2 | 0 to 63 |
| 2 | address | F.2.2 | 6 hex digits |
| 3 | max. protocol version | G.2.1 | unsigned, 0 to 15 |
| 4 | M-band receive | G.2.2 | table value |
| 5 | O-band LDR receive | G.2.3 | table value |
| 6 | O-band HDR receive | G.2.4 | table value |
| 7 | ADS-L 4 Mobile | G.2.5 | table value |
| 8 | e-conspicuity receive | G.2.6 | 2 hex digits |
| 9 | e-conspicuity transmit | G.2.7 | 2 hex digits |
| 10 | transponder | G.2.8 | table value |
| 11 | Traffic Uplink client | G.2.9 | 0 or 1 |
| 12 | FIS-B client | G.2.10 | 0 or 1 |

Receive capability is 0 none, 1 occasional (>5%), 2 partial (>40%) or 3 full (>80%) for each band (G.2.2 to G.2.4). O-band HDR receive excludes reception confined to the uplink slot (G.2.4).

ADS-L 4 Mobile is 0 none, 1 initialized but offline or 2 initialized and online. Value 3 is reserved (G.2.5).

The receive bitmap assigns bit 0 to FLARM, 1 to PilotAware, 2 to FANET, 3 to OGN Tracker, 4 to ADS-B 1090, 5 to ADS-B UAT 978 and 6 to Mode-S / PCAS. Bit 7 is reserved (G.2.6). The transmit bitmap uses the same assignments for bits 0 to 5. Bits 6 and 7 are reserved (G.2.7). An unknown capability leaves its bit clear.

Transponder capability is 0 none/off/unknown, 1 Mode-C / ALT or 2 Mode-S. Value 3 is reserved (G.2.8). Each client flag is 0 when the sender does not process that uplink payload and 1 when it does (G.2.9, G.2.10).

This sentence exposes what another aircraft reports it can receive. It does not assert that the other aircraft received a particular transmission.

### `$PDSLW` carries the device's alarm decision

| # | Field | Written as | Source |
|---|---|---|---|
| 1 | alarm level | 0 none, 1 low, 2 important, 3 urgent | this proposal |
| 2 | AMT | 0 to 63 | F.2.2 |
| 3 | address | 6 hex digits | F.2.2 |

ADS-L defines no alerting algorithm. The sentence carries the device's own decision so the display and the device's buzzer agree. It repeats no geometry. The display obtains that from `$PDSLA` and `$PDSLO`.

There is one sentence per alarming target, at most three targets at once. When more than three targets alarm, the device sends the three with the highest level, nearest first among equal levels.

A change is sent immediately, followed by 1 Hz while the level is above 0. A level 0 sentence ends the alarm.

### `$PDSLB` carries own barometer readings

| # | Field | Written as | Source |
|---|---|---|---|
| 1 | static pressure | whole Pa, 0 to 110000 | device sensor |
| 2 | pressure altitude on 1013.25 hPa | whole m, -1000 to 20000 | device sensor |
| 3 | vertical speed | m/s, 2 places, climb positive, -118.00 to 119.00 | device sensor |
| 4 | temperature | whole degrees C, -40 to 85 | device sensor |

A device sends this sentence at 1 Hz only when it has a barometer. Pressure altitude retains the standard 1013.25 hPa datum so the display can apply its own QNH.

### `$PDSLS` carries device status and UTC

| # | Field | Written as | Source |
|---|---|---|---|
| 1 | UTC date | ddmmyy, as `$GPRMC` | device UTC |
| 2 | UTC time | hhmmss.ss | device UTC |
| 3 | GNSS fix | 0 none, 1 2D, 2 3D | device GNSS |
| 4 | geoid separation | m, 1 place, -200.0 to 200.0, mean sea level above the WGS-84 ellipsoid | device GNSS, as field 11 of `$GPGGA` |
| 5 | targets heard | count, 0 to 999 | device traffic table |
| 6 | battery | percent, 0 to 100, empty when unknown | device power state |
| 7 | charging | 0 or 1 | device power state |
| 8 | power level | 0 unknown, 1 normal, 2 low, 3 critical | device power state |
| 9 | own transponder address | 6 hex digits, empty when none | device configuration |

Geoid separation is the height of mean sea level, the geoid, above the WGS-84 ellipsoid. Altitude above mean sea level is ellipsoid altitude minus geoid separation. Near Grenoble, the separation is about +49.5 m, as in the example.

The own transponder address lets the display suppress its own Mode S transponder from the traffic display.

## Examples retain the proposed wire format

The examples use positions near Grenoble on 13 November 2026, with device UTC 10:15:07.25. Each displayed sentence is followed on the link by CR LF.

| Example | Sentence |
|---|---|
| the display asks for version 1 | `$PDSLR,1*44` |
| the device answers | `$PDSLV,1,skyBlip,Go,1.0.0+874*33` |
| own-ship, a microlight, transmitting | `$PDSLO,L,58,00012A,7.25,2,1,45.20113,5.80245,30.5,1532,G,1.1,90.0*1B` |
| its identity | `$PDSLI,58,00012A,1,0,0,0,6,2,2,F-JABC*6B` |
| its capabilities | `$PDSLC,58,00012A,1,2,0,0,0,01,00,0,1,0*34` |
| a skyBlip glider, climbing | `$PDSLA,L,58,000012,7.25,2,4,45.21468,5.84731,31.5,1842,G,2.4,273.5*5C` |
| its identity | `$PDSLI,58,000012,4,0,0,0,6,2,2,F-CGXY*10` |
| an OGN-Tracker paraglider on ADS-L | `$PDSLA,L,7,DD4F21,7.00,2,7,45.19832,5.80115,9.8,1523,G,-1.1,118.1*01` |
| a FLARM glider, emergency status unknown | `$PDSLA,F,6,3EE512,7.00,2,0,45.22740,5.86020,27.0,1655,G,-0.6,301.2*43` |
| an ADS-B aircraft, pressure altitude | `$PDSLA,A,5,3C6589,7.00,2,1,45.36294,5.32906,61.0,1524,P,-2.5,211.6*2F` |
| an aircraft on the ground, relayed by a station | `$PDSLA,U,5,3C6512,6.75,1,1,45.36294,5.32906,0.0,402,G,0.0,*2D` |
| an alarm on the glider | `$PDSLW,2,58,000012*4C` |
| the alarm ends | `$PDSLW,0,58,000012*4E` |
| own barometer | `$PDSLB,84560,1500,1.12,9*57` |
| the device | `$PDSLS,131126,101507.25,2,49.5,3,82,0,1,*45` |

The uncertified devices in these examples transmit SIL and NIC 0, as G.1.17 requires for these devices.

## Rates give each target its own update

A target goes out at most once a second. When a target is heard more than once in a second, the device sends the latest reception. An airborne ADS-L target therefore updates every second and a ground target every 10 s, following the Traffic transmission rates of G.1.16. Issue 2 numbers both Velocity Accuracy and Transmit Rate and Timing as G.1.16.

The device does not redraw a target by extrapolating it. The display ages the target using its timestamp.

For 48 targets at 81 bytes each, traffic alone is about 3.9 KB per second, plus identities every 10 s. At the 182-byte notification payload an iPhone usually settles on, that traffic volume is about 22 notifications per second. This is an estimate, not a device measurement.

## Bluetooth carries the same byte stream

The stream uses the two GATT services current displays already use:

| Service | Characteristics | Current users |
|---|---|---|
| Nordic UART `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | `6E400003` notified by the device, `6E400002` written by the display | XCSoar, Enroute, the SoftRF family |
| HM-10 `FFE0` | `FFE1` notified and written | LK8000, SkyDemon, the profile PowerFLARM Flex and Fusion use |

The display writes `$PDSLR` on `6E400002` or `FFE1`. The device's answer and subsequent output use that connection.

Notifications carry as many stream bytes as the negotiated MTU allows. Notification boundaries are not sentence boundaries. A line may span two notifications. The display reassembles lines on CR LF.

## The code should belong to ADS-L

NMEA assigns the three-letter code after `$P` in a proprietary sentence. It issues manufacturer codes to companies only. The administrative fee is $200, with purchase of the NMEA 0183 standard: $1,150 for an NMEA manufacturer member, $7,500 for government, industrial and testing, or $10,000 for consumer electronics. Source: NMEA's NMEA 0183 page, read 13 November 2026.

NMEA keeps manufacturer codes and sentence identifiers in one list. A code occupied by either is unavailable.

| Code | Evidence |
|---|---|
| `ADS` | occupied by the standard sentence "Automatic Device Status" in NMEA 0183 All Identifiers, 1 August 2013 |
| `DSL` | free in that list. No sentence under it was found in public GitHub code on 13 November 2026 |

`DSL` reads as ADS-L and names no manufacturer. Its availability in the live register is not confirmed. That register is behind an NMEA login and must be checked before an application.

Unregistered use is not a substitute for assignment. SoftRF writes `$PSRF`, although `SRF` belongs to SiRF Technology. We ask the ADS-L governing body to hold the code so different manufacturers implement one family.

## GDL 90 does not preserve the ADS-L payload

GDL 90 is Garmin's binary display interface. It carries FIS-B and ForeFlight reads it. It does not provide ADS-L's address space, category distinctions or full set of quality indicators.

GDL 90 section references below are to 560-1058-00 Rev A, 5 June 2007. FLARM references are to FTD-012 version 7.19.

| Element | ADS-L Issue 2 | FLARM stack | ADS-L NMEA | GDL 90 |
|---|---|---|---|---|
| Encoding | binary radio payload | ASCII lines of 82 characters | ASCII lines of 82 characters | binary frames between 0x7E flags, byte stuffing, CRC-16-CCITT (2.2) |
| Time of fix | per target, quarter seconds (G.1.1) | none | per target, as G.1.1 | the Heartbeat's UTC second for every report of that second (3.1.3, 3.5) |
| Address | 64 AMT entries and 24 bits (F.2.2) | ID type 0 to 2 and 24 bits | as ADS-L | 6 address types, ADS-B or TIS-B, and 24 bits (3.5.1.2) |
| Source | the radio it came on | FLARM, ADS-B, ADS-R, TIS-B, Mode S (FTD-012 version 9) | ADS-L direct or relayed, FLARM, PilotAware, FANET, OGN, ADS-L 4 Mobile, ADS-B, ADS-R, TIS-B | ADS-B or TIS-B, through the address type |
| Position | absolute, 1.2 m north and 2.4 m east at the equator (G.1.5) | relative, metres | absolute, 5 places | absolute, 2.4 m at the equator (3.5.1.3) |
| Altitude | above the ellipsoid (G.1.7) | relative, metres | with its reference, ellipsoid or pressure | pressure altitude, 25 ft (3.5.1.4) |
| Speed, track, vertical rate | 0.25 m/s, 0.7 degree, 0.125 m/s (G.1.8 to G.1.10) | m/s, degrees, m/s | as ADS-L | 1 kt, 1.4 degrees, 64 ft/min (3.5.1.7 to 3.5.1.9) |
| Flight state | undefined, ground, airborne (G.1.2) | none | as ADS-L | ground or airborne (3.5.1.5) |
| Category | 18 values incl. paraglider, hang-glider, paramotor, gyrocopter, eVTOL, UAS (G.1.3) | 16 FLARM types | as ADS-L | ADS-B emitter categories: one value for ultralight, hang-glider and paraglider, rotorcraft for a gyrocopter, none for paramotor or eVTOL (3.5.1.10) |
| Emergency | 7 values (G.1.4) | none | as ADS-L | 7 values incl. minimum fuel (3.5.1.12) |
| Callsign | Issue 3 | in the ID field | its own field, 14 characters | 8 characters (3.5.1.11) |
| Integrity and accuracy | 6 indicators (G.1.11 to G.1.16) | none | all 6 | NIC and NACp per target, VFOM for own-ship only (3.5.1.6, 3.8) |
| Fresh or extrapolated | n/a | none | always as heard | one bit per report (3.5.1.5) |
| Capabilities | Status payload (G.2) | none | `$PDSLC` | none |
| Alarm | none | per-target level in `$PFLAA`, one threat in `$PFLAU` | per-target level, up to 3 | one alert bit per target (3.5.1.1) |
| Own-ship | Traffic payload | `$GPRMC`, `$GPGGA` | `$PDSLO` | Ownship Report and Ownship Geometric Altitude (3.4, 3.8) |
| Barometer, vario | none | `$PGRMZ`, `$LK8EX1` | `$PDSLB` | pressure altitude in the Ownship Report, no vario |
| Battery | none | `$LK8EX1` | `$PDSLS` | none (the Heartbeat's GPS Batt Low bit is the receiver's clock battery, 3.1.1) |
| Device health | Status payload | `$PFLAU` | `$PDSLS`, `$PDSLV` | Heartbeat: position valid, maintenance required, UTC OK (3.1). ForeFlight's ID message 0x65 adds serial, names, capabilities |
| Version negotiation | E.1.1, B.5 | none | `$PDSLR`, `$PDSLV` | none |
| FIS-B | up to 217 bytes (G.4) | none | not carried | Uplink Data, 432 bytes (3.3) |
| Obligations | none | none | none | a display shows Garmin's advisory text at every start (1.4) |

The proposed family keeps ADS-L's fields and the existing Bluetooth stream. GDL 90 travels over Wi-Fi in nearly every display listed below that reads it. ForeFlight specifies UDP port 4000 and no Bluetooth transport. A device used with a paraglider pilot's phone has no Wi-Fi link to spare.

## Existing display support is not support for this proposal

The following records existing formats. No display reads the proposed family yet.

| Display | FLARM stack over Bluetooth LE | GDL 90 |
|---|---|---|
| XCSoar | yes, all six sentences with any driver | a driver over serial or UDP in 7.47, not released (XCSoar `NEWS.txt`) |
| LK8000 | yes, on HM-10 | not checked |
| SkyDemon | yes, "NMEA (FLARM or GPS)" (FLARM FTD-115 v1.02) | not checked |
| SeeYou Navigator | yes (FTD-115) | not checked |
| Enroute | yes (FTD-115) | Wi-Fi, UDP port 4000 (FTD-115) |
| ForeFlight | not checked | Wi-Fi, UDP port 4000 |
| EasyVFR, Airmate, Helios Horizon | not checked | Wi-Fi (FTD-115) |

FTD-115 is FLARM's Hub Electronic Flight Bag Guide, version 1.02, 19 March 2026. XCSoar source and release information were read on 13 November 2026.

## We ask the governing body to adopt the family

1. Adopt the family as a companion to ADS-L 4 SRD-860 or an annex of a later issue, so every ADS-L device speaks it the same way.
2. Hold the NMEA manufacturer code it is written under, `DSL` or another.
3. Map Issue 3's callsign onto field 10 of `$PDSLI`, with its length and character set.

## skyBlip keeps the current output meanwhile

skyBlip Go and skyBlip Bravo send the FLARM stack and nothing else. There is no setting. They will answer `$PDSLR` once the family is agreed.

The repository, including the firmware, has been MIT-licensed since November 2026. The firmware can serve as a reference implementation for any manufacturer.

## References identify the source of each layer

- EASA, ADS-L 4 SRD-860 Issue 2, ED Decision 2022/024/R: https://www.easa.europa.eu/en/document-library/agency-decisions/ed-decision-2022024r. Local transcription: `docs/reference/ads-l_4_srd860_issue_2.md`.
- FLARM, Data Port Interface Control Document, FTD-012 version 7.19: https://www.flarm.com/wp-content/uploads/2024/04/FTD-012-Data-Port-Interface-Control-Document-ICD-7.19.pdf
- FLARM, Hub Electronic Flight Bag Guide, FTD-115 version 1.02, 19 March 2026: https://www.flarm.com/wp-content/uploads/2026/03/FTD-115-FLARM-Hub-EFB-Guide-v1.02.pdf
- Garmin, GDL 90 Data Interface Specification, 560-1058-00 Rev A, 5 June 2007: https://www.faa.gov/sites/faa.gov/files/air_traffic/technology/adsb/archival/GDL90_Public_ICD_RevA.PDF
- ForeFlight, GDL 90 extended specification: https://www.foreflight.com/connect/spec/
- NMEA, NMEA 0183, read 13 November 2026: https://www.nmea.org/nmea-0183.html
- NMEA, NMEA 0183 All Identifiers, 1 August 2013: https://rhothetaint.com/wp-content/uploads/2021/03/NMEA-0183-identifier-list.pdf
- XCSoar, `src/Device/Parser.cpp` and `NEWS.txt`, master, read 13 November 2026: https://github.com/XCSoar/XCSoar
- skyBlip, current output: `firmware/products/common/services/nmea.cpp` and `firmware/core/protocol/nmea_out.cpp`.
- skyBlip, Status and callsign records: `firmware/core/protocol/README.md`, "The Status payload" and "The name ADS-L has no field for".
- skyBlip, Status and callsign transmission schedule: `firmware/core/timing/README.md`, "The seconds each burst owns" and "The tail names us, once every ten seconds".
