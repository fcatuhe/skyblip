# protocols/adsb

This stack reads the 1090 MHz extended squitter and Mode S replies: messages (`message.*`), compact positions (`cpr.*`), one track per aircraft (`assembly.*`) and the registration a flight ID spells (`registration.*`). It follows the RTCA DO-260B draft, version 4.2 (working paper 1090-WP30-18), because the published DO-260B was not available.

skyBlip keeps the rest: the observation a track becomes (`core/protocol/adsb_observation.*`, `core/protocol/adsb_category.*`), the LR2021 (`hardware/parts/lr2021/`), and Bravo's `AdsbService`, which owns the cache. The header-only `squitter.h` builds frames for the tests and the simulator. No device transmits with it.

## ADS-B: from frames to typed messages

`receive()` takes the 14 bytes of a frame and returns one `Message`, stamped with the radio's time and RSSI. The LR2021 gives a level only to the newest frame of a FIFO read, so the reception carries `rssi_valid` through to `Track::rssi_valid`.

**Bits.** The receiver decides each bit from its two chips, so the decoder never sees a chip. 1090 MHz sends a 1 as `10`, so `squitter.h`'s `chips_of()` is `wire::manchester_encode` inverted. DF18 is refused as `Unsupported`, so TIS-B, ADS-R and non-transponder ADS-B are not read.

**Parity.** Mode S parity is ADS-L's CRC (`protocols/common/crc.h`), so `wire::crc24_check` leaves 0 for a sound DF17 frame. A frame that fails is refused as `Crc`. `receive()` repairs nothing: the LR2021 marks no doubtful chip, and a blind one-bit repair can draw a ghost aircraft. `repair_bits()` lets a caller repair a DF17 that holds an address it already knows (The screen).

**Messages.** Each one carries the 24-bit address, the capability field and the type code. Units follow `firmware/README.md`:

| TYPE | Kind | What it reads |
|---|---|---|
| 1 to 4 | `Identification` | category set D down to A, category 0 to 7, the callsign with trailing blanks cut. A callsign holding a code outside the character set is refused whole, as skyBlip's `callsign_of` refuses one on ADS-L |
| 5 to 8 | `SurfacePosition` | the movement code as the floor of its band in thousandths of a knot (Doc 9871 A.2.3.3.1), the 7-bit track and its status, T, and the CPR pair |
| 9 to 18 | `AirbornePosition` | pressure altitude, T, and the CPR pair |
| 20 to 22 | `AirbornePosition` | GNSS height above the ellipsoid in the same field, T, and the CPR pair |
| 19 | `Velocity` | subtypes 1 and 2: east and north ground speed. Subtypes 3 and 4: airspeed, its type and the magnetic heading. All four: the vertical rate with its source, and GNSS height minus pressure altitude |
| 31 | `OperationalStatus` | subtype, version, and GVA when the status is airborne and version 2 |

Every other type code, a velocity subtype outside 1 to 4 and a status subtype outside 0 and 1 come back `Unsupported` with the header filled. TYPE 0 carries a pressure altitude with no position and is not parsed yet.

**Zeros and top codes.** In a velocity field, all zeros means nothing, the value is (code - 1) steps, and the all-ones top code is marked `saturated` (RTCA 1090-WP-14-09R1, Figures A-6 and A-7). The movement code saturates at 124, 175 kt or more, and 125 to 127 are reserved. A version 0 status reads as version 0. GVA exists from version 2 and is passed on raw, because its code values are not checked against DO-260B yet.

**Altitude.** A 12-bit field with Q = 1 counts 25 ft steps from -1000 ft. With Q = 0 it is a Gillham code in 100 ft steps, laid out C1 A1 C2 A2 C4 A4 B1 Q B2 D2 B4 D4 (DO-260B draft 4.2, Figure 2-4): D2 D4 A1 A2 A4 B1 B2 B4 are a Gray-coded count of 500 ft, C1 C2 C4 a Gray-coded count of 100 ft that runs backwards when the 500 ft count is odd, and C1 C2 C4 of 000, 101 or 111 is no altitude. All zeros is no altitude.

**GNSS height, TYPE 20 to 22, is coded as pressure altitude is, in feet.** The draft's §2.2.3.2.3.4.2 codes GNSS HAE "in accordance with §2.2.3.2.3.4.3", the pressure altitude clause, and the published DO-260 (2000) agrees. pyModeS and RadioLib read whole metres instead.

**Test vectors.** The tests pin frames and decodes from The 1090MHz Riddle (Junzi Sun), [IndoorOutdoor/1090dumpCSV](https://huggingface.co/datasets/IndoorOutdoor/1090dumpCSV) (Apache-2.0, listed in `NOTICE.md`), Wikipedia's Gillham table and pyModeS 3.7.0, and build the rest with `squitter.h`. No code was copied from any source.

## Mode S: replies with no position

A transponder without ADS-B out still squitters a DF11 and answers every interrogation, so Bravo hears its address. `receive()` reads the downlink format in bits 1 to 5, then the frame by that format's rule, so a DF17 with a wrong bit there is read as another format.

| DF | Kind | Length | What it reads |
|---|---|---|---|
| 11 | `AllCall` | 56 bits | the address in clear and the capability. The parity leaves 0 for an acquisition squitter or a reply to interrogator code 0, which sets `address_verified`. It leaves the interrogator code, at most 0x7F, for a reply to another code, which is `Ok` with `address_verified` false. Anything larger is `Crc` |
| 0, 4, 16, 20 | `Reply` | 56 bits for 0 and 4, 112 for 16 and 20 | the address the parity leaves, and the pressure altitude of the AC field, bits 20 to 32 |
| 5, 21 | `Reply` | 56 bits for 5, 112 for 21 | the address the parity leaves. The identity code is not read, since nothing uses it yet |

Every other format is `Unsupported` with no parity checked. `kDownlinkFormats`, 25, counts DF0 to DF24, DF24 standing for every frame that opens on `11`.

**Address and parity.** A reply other than DF11 overlays its address on its parity, so noise reads as some address too. A `Reply` is never `address_verified`: believe it only for an address already heard in clear, which Bravo's `Transponders` tracks (`products/skyblip_bravo/README.md`). `Assembly::fold` refuses `AllCall` and `Reply`.

**Altitude.** The 13-bit AC field is the 12-bit ES field with an M bit after A4, laid out C1 A1 C2 A2 C4 A4 M B1 Q B2 D2 B4 D4. With M = 0, removing M leaves the ES field, read as above. M = 1 is an altitude in metres, which this stack reads as no altitude. All zeros is no altitude.

## The screen

`screen()` gives `receive()`'s verdict on a frame without decoding it, so a radio thread can run it before a queue: `Squitter` for a DF17 whose parity leaves 0, `AllCall` for a DF11 whose parity leaves 0 or an interrogator code, `Reply` for DF0, 4, 5, 16, 20 and 21, `Format` for every other format and `Parity` for a DF11 or DF17 that fails. `Repaired` is the caller's verdict on a `Parity` frame that `repair_bits()` mended. It keeps every frame `receive()` reads: a test runs both on 20,000 frames of noise, squitters and all-calls and checks it. A `Reply` passes unchecked, because its parity is its address, and only the caller knows the addresses heard in clear. A DF17 with a type code the decoder does not read passes as a `Squitter` and comes back `Unsupported` from `receive()`. The parity costs one pass of the bitwise CRC over 7 or 14 bytes, run only for DF11 and DF17.

`repair_bits()` mends a DF17 that fails by one or two flipped bits and says how many it flipped. Mode S parity is linear, so flipped bits in a sound frame leave the XOR of their syndromes. The stack holds the 107 syndromes of bits 6 to 112, sorted at compile time: a one-bit error is one binary search, and a two-bit error is at most 107, about 10 us on the nRF52840 by count, not yet timed. Every one of the 5671 pairs leaves a syndrome of its own, distinct from every single bit's, which a test checks pair by pair, so no repair is ambiguous. DO-260B 2.2.4.3.4.7.3.a never corrects the DF field, so its five bits are left out, and a DF11 is left alone because its parity may carry an interrogator code. Noise lands on one of the single-bit syndromes about once in 157,000 tries and on a pair about once in 3,000, and then names a random address. So the caller keeps a repaired frame only for an aircraft it already holds, as ADSBee does with its one- and two-bit repair (PantsForBirds/adsbee #382), and a repair never founds a track. dump1090 repairs the same way with `--fix` and `--fix-2bit`.

## ADS-B: from messages to an aircraft

`Assembly::fold` is the specification's report assembly. It takes one `Ok` message, the instant it was true, its `Receipt` and where the receiver is, and returns the aircraft's `Track` in ADS-B's units. It decides nothing about which aircraft matter to the pilot. The caller exports a track when `position_valid` is true.

| Rule | Figure | Clause |
|---|---|---|
| An airborne pair's halves are at most this far apart | `kAirbornePairMaxAgeMs`, 10 s | A.1.7.7, §2.2.10.3.1 |
| A surface pair's halves | `kSlowSurfacePairMaxAgeMs`, 50 s. `kSurfacePairMaxAgeMs`, 25 s, when either half's ground speed may be over 25 kt or is unknown | A.1.7.8, §2.2.10.3.2 |
| A pair whose halves fall in different numbers of longitude zones does not decode | NL(even) == NL(odd) | A.1.7.7 d, A.1.7.8 d |
| A single frame decodes against a reference within | 180 NM airborne, 45 NM surface | A.1.7.4 |
| A global decode is refused further than the receiver can hear | `kAdsbRangeCeilingM`, 180 NM, ours | A.1.7.10.2, §2.2.10.3.1 a |
| A position not refreshed for this long is cleared, and only a new pair founds the next | `kTrackPositionMaxAgeMs`, 120 s | §2.2.10.3.1 f, §2.2.10.3.2 f |
| A single frame moves a track only within `kTrackJumpWindowMs`, 30 s, of its last position, and no further than | `kAirborneJumpCeilingM`, 6 NM airborne, `kSurfaceJumpCeilingM`, 0.75 NM surface, `kTransitionJumpCeilingM`, 2.5 NM between the two | §2.2.10.6.3 |
| A new track holds until a second, independent pair lands where it says | within `kAirborneAgreementCeilingMm`, 5 m airborne, `kSurfaceAgreementCeilingMm`, 1.25 m surface | A.1.7.10.2, §2.2.10.6.2 |
| A record nobody has heard for this long is forgotten | `kAdsbAircraftForgetMs`, 250 s | §2.2.10.1.3 b |

The local limits hold by construction: a single frame decodes only against the aircraft's own track within 30 s, in which 600 kt covers 5 NM. Past 30 s a single frame moves nothing (§2.2.10.6.3 b).

**The receiver's reference.** `AdsbReference` is Bravo's own fix, or the Go's position when paired, `valid` only while its source is current. It picks a surface pair's quadrant, so no surface aircraft is placed without one. It refuses a global decode past `kAdsbRangeCeilingM`, and it ranks the cache. An airborne pair decodes without it (A.1.7.7), unchecked for range.

**The reception ceiling.** 180 NM is half the 360 NM between two airborne CPR solutions (A.1.7.1 Note 4). Bravo's measured range may lower it (#172). The traffic table has no ADS-B gate, so this is the furthest an aircraft enters it from (`core/traffic/README.md`, No gate).

**Confirmation.** A track founded on one pair is exported at once (§2.2.10.3.1 d). The next pair whose halves both arrive after it must decode within the agreement ceiling of the track's local decode, or the track is dropped and the next frame founds a new one. Both decodes round half up, so an honest frame decodes identically both ways, and the largest product stays under 6e16 on every ABI. A zone index fits 32 bits for any 32-bit CPR field, so its modulo takes no 64-bit division on the nRF52840. A decode past a pole is refused, and every longitude is wrapped into +-180 degrees.

**Freshness.** Times are compared as signed differences on `ports::Clock`, right for 24.8 days, and a negative duration counts as none. `age_out` and `fold` expire a half pair past its window, a position past 120 s, a GNSS-baro difference past 10 s and every other field past 250 s (§2.2.10.1.3). Bravo calls `age_out` every pass, so no held time is old enough to wrap. `at_ms` is the position's instant, or the newest message's while there is no position.

**GNSS height.** It comes from a TYPE 20 to 22 position, or as pressure altitude plus a velocity message's difference, valid while the two were true within `kHeightDifferenceMaxAgeMs`, 10 s, of each other. The newer source wins. In 10 s a 2000 ft/min climb 20 K off standard moves the difference about one 25 ft step (DO-260B Table 2-36). A saturated difference derives nothing. Every GNSS height is read as HAE (#161) until flight data settles the datum (#183).

**Velocity.** Subtypes 1 and 2 give ground speed and true track. Subtypes 3 and 4 give airspeed and magnetic heading, which are neither, so they refresh only the climb. A surface position gives a ground speed with its track, or a stop.

**Air or ground.** A surface position says on the ground. An airborne position or a velocity says airborne (Table 2-77). Any other message says so only through a capability of 4 (ground) or 5 (airborne).

**Own aircraft.** `set_own_address` names the aircraft whose messages are dropped: the paired Go's `aircraft_address` when set, else Bravo's own. Zero drops nothing.

## The ADS-B cache

`Assembly` holds `kMaxAircraft`, 100 aircraft, apart from the traffic table, because a pair must accumulate before the table holds the aircraft. 100 is the track files DO-260B Table 2-105 asks of a class A0 receiver.

A newcomer takes a free place, then a place silent for 250 s, then the place of the furthest aircraft if it is nearer, and is refused otherwise. Evicting the oldest instead would fail in a crowd larger than the cache, as each aircraft would leave before its second half arrived. A half pair is ranged by decoding it against the receiver (A.1.7.4). A newcomer with no range can take only a silent or unranged place. With no reference, the newcomer takes the place heard longest ago.

A refused message costs a search of the 100 places, not yet measured on the nRF52840 (#188).

`evictions()` counts aircraft that lost their place, `refusals()` newcomers that found none, and `implausible()` positions refused: a global decode past the ceiling, a frame that jumps or decodes past a pole, or a second pair that disagrees with the track.

## The registration an ADS-B flight ID spells

ADS-B's charset has no dash, so F-JABC arrives as `FJABC`, and `registration()` puts the dash back. It reads the nationality mark from the address block (ICAO Annex 10 Volume III Part I Chapter 9) for eighteen European states, and inserts the dash only when the callsign starts with that mark followed by three to five letters or digits. Anything else is copied as heard. The United States is left out because `N905NA` has no dash. Luxembourg and Slovenia are left out because two published tables disagree on their blocks.

## The conformance suite

The draft is read clause by clause in two halves, as ADS-L is. `test/conformance/` holds the cases this stack satisfies on its own. `firmware/test/adsb/` holds the cases that need Bravo around it, and its [README](../../test/adsb/README.md) says how the suite is kept. [`docs/ADSB_CONFORMANCE.md`](../../../docs/ADSB_CONFORMANCE.md) lists the cases from both halves under their clause headings.
