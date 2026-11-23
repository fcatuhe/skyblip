# core/traffic

What the sky around this aircraft contains, how dangerous it is, and who in it is flying with us. Pure, integer, no framework headers: the services in `products/` feed these and publish what they answer.

| File | What it decides |
|---|---|
| `sanity` | whether a decoded position is close enough to have been heard at all |
| `lease` | how long one reception is believed, for every layer that holds a slot |
| `table` | which aircraft the finite table holds, and each one's turn rate |
| `versions` | which ADS-L version each client heard direct reads, kept apart from the display (§B.5) |
| `alarm` | whether a target is an advisory, and what the annunciator is allowed to say |
| `formation` | which targets are flying with us, on geometry alone |
| `range` | how far a target is and how far above, and the order the nearby page lists them in |
| `callsigns` | what each address is called, for as long as anybody should believe it |

## The model

There is one level, and it is a place. An aircraft inside `kAdvisoryDistanceM` of us and inside `kAdvisoryAltitudeM` of our altitude is a traffic advisory. Everything else is a target on the plot.

| Constant | Value | Where the number comes from |
|---|---|---|
| `kAdvisoryDistanceM` | 3000 | ours. At 240 kt of head-on closure it is 24 s, the band in which TCAS II issues a traffic advisory at low level (SL3: tau 25 s, DMOD 0.33 NM) |
| `kAdvisoryAltitudeM` | 300 | FLARM's `$PFLAU` traffic advisory: 1.5 km horizontally and 300 m vertically |

There is no level above the advisory, because a resolution advisory needs a link to coordinate with the other aircraft and the authority to order a climb. `$PFLAA` and `$PFLAU` send `to_number`, 1: FLARM's lowest alarm is the only band a 3 km ring does not overstate.

The ring does not predict. A prediction needs a model of protection volume, spread and horizon, and that model would decide whether a pilot hears about an aeroplane a kilometre away. Aircraft flying with us are silenced by `formation` instead.

A target that reports no velocity closes at `kUnknownTargetSpeedMps`, because zero would make a relayed position the safest thing in the sky.

## A target that sends no altitude

Unknown altitude is a state, not 0 m: ADS-L can mark it invalid (G.1.7), and ADS-B may send none. `flight::relative_north_east_up` reports it through `up_valid`, and no reader may take the target as level with us:

- `alarm` grades it on distance alone, and `AlarmAssessment::relative_altitude_valid` is false so no display prints a separation. The flight log (`../flight/README.md`) and the diagnostics `Traffic` record (`../diagnostics/README.md`) carry the flag. A record written before the flag existed holds 0 for an unknown altitude, and nothing tells it from a target level with us.
- `formation` never makes it a member, and a member that stops sending its altitude leaves, because holding station is a claim about three axes.
- `range` gives a ground distance with `RangeRow::up_valid` false.
- `sanity` checks the ground distance.

What the radar and the list draw for it is in `products/skyblip_go/pages/README.md`, and what the tablet is sent is in `core/protocol/README.md`.

## How long an aircraft is held

The lease (`lease.h`) says whether the link has lost an aircraft. `kAlertMaxAgeMs` says whether a position is fresh enough to grade. They are separate questions.

A target is dropped after `kTargetForgetReports`, 6, of its own report periods (`adsl::report_period_s`): `kAirborneTargetForgetS` is 6 s and `kGroundTargetForgetS` 60 s. The table and the alarm tracker take the window from the observation, so a takeoff shortens the lease on the burst that announces it. One multiple fits both because nothing extrapolates a target: airborne the drop-time error is 6 s at 60 m/s, 360 m, and on the ground 60 s at 5 to 8 m/s, 300 to 480 m. Six misses at 1 Hz is a link that stopped: even with half the bursts lost it happens 1.5% of the time.

`kAlertMaxAgeMs`, 5 s, is the oldest position the alarm grades, because at 100 m/s of closure a five-second-old fix is 500 m off. It must not scale with a target's report period, because a ground position a minute old is half a kilometre off.

Both ages count from the instant a reception was true, not from its arrival, so a target a paired Bravo forwards late stops sounding at its own age (`AlarmTracker` reads `at_ms`). The table counts in seconds and holds a reception dated up to `kReceptionAheadCeilingS`, 2 s, ahead as new: its second lags its clock by up to one, and a paired Bravo's clock may run one ahead (`../comms/README.md`, What the Go makes of its Bravo). Further ahead counts as infinitely old.

`direct_preferred_max_age_s` is the larger of the two: 5 s airborne, the target's own period on the ground. Otherwise at 0.1 Hz a relay would replace every ground report with its poorer copy before the next direct one is due.

## An aircraft on the ground is not traffic

A target whose G.1.2 code says ground is drawn, named and listed, never graded, because a 3 km ring over an airfield holds every aeroplane on the apron. `formation` ignores it too, since a tug and its glider hold station on the apron as well as on tow. A full table evicts ground targets before airborne ones, so a busy apron cannot keep an arrival off the plot. Unknown is not ground: it gets the airborne treatment everywhere.

## Own-ship on the ground grades nothing

While own-ship is on the ground, `assess` returns `Level::None` for every target, as FLARM does, so a unit at the hold does not announce the whole circuit. At liftoff every target in the ring becomes an advisory on the same pass, and the annunciator plays one tune per pass. Formation targets are the exception, below.

## Whichever system heard it

The ground rules read only `AircraftObservation::flight_state`, never the source. It holds an ADS-L G.1.2 code, and `flight::state_from` reads any other code as `Unknown`. ADS-L and the uplink copy G.1.2. ALP-TAS maps its field in `alptas_flight_state`. The ADS-B assembler (#190) sets `Ground` from a surface position (type code 5 to 8) or DF17 capability 4, and `Airborne` from an airborne position (type code 9 to 22) or capability 5, with `flight_state_age_ms` keeping when it was true.

A new source must write this field, `Unknown` when its wire has no state, and add itself to `test/core/test_traffic_sources.cpp`.

## ADS-B

ADS-B reaches the table as `Source::Adsb`, from the LR2021 on a Bravo and from a paired Bravo on a Go (`../comms/README.md`, What the Go makes of its Bravo).

### Which height is compared

`flight::relative_north_east_up` compares the ellipsoid heights (`flight::gnss_relative_altitude_m`), else the pressure altitudes (`flight::pressure_relative_altitude_m`), else none. It never mixes the two kinds, because they differ by the geoid and the day's weather. It reads the validity flags only: freshness is decided where the observation is assembled (#190), and `common::OwnshipService` expires own-ship's pressure altitude (`products/common/README.md`). `flight::carried_to` leaves pressure altitudes as reported, which costs at most 15 m (1.5 s of `flight::kExtrapolationCeilingMs` at 10 m/s).

### One aircraft heard on several paths

`source_rank` puts direct ADS-L or FLARM at 3, ADS-B at 2 and the uplink at 1 (epic #184). A lower-ranked copy never displaces a higher-ranked one younger than `direct_preferred_max_age_s`. ADS-B ranks below direct because each field comes in its own message with its own age, and on a Go it crosses a second link.

The table merges two reports only when both the address mapping table and the address match. An ADS-B address is under table 5 (`adsl::kAddressTableIcao`, ADS-L F.2.2), so an ADS-L or FLARM unit merges with its ADS-B copy only when it sends its ICAO address, as F.2.3 asks. Under another table it is a second identity: the table does not guess.

### Our own aircraft

The table refuses two addresses before any other check:

- our ADS-L address under table 58 (`set_own_address`), even while we transmit an aircraft address, because a ground station relays us back and the alarm would grade a head-on with ourselves;
- our aircraft's ICAO address under table 5 (`set_aircraft_address`): our own transponder, which a Bravo in the cockpit hears loudest and a ground station relays.

Zero filters nothing. `go::TrafficService` sets both on every pass. A new aircraft address releases the slot it names even under an alarm, because that alarm is on our own transponder. The setting is `aircraft_address` (`products/skyblip_go/README.md`, Aircraft address).

### A report with no position

The table refuses `Source::Adsb` without `position_valid`, such as half a CPR pair (`core/model/README.md`), and counts it in `adsb_without_position()`. Admitted, it would have a slant of 0 and a full table would keep it over every real target.

### Sanity: the radio horizon

`kAdsbRangeCeilingM` is 1115 km, line of sight, because nobody has measured what the LR2021 hears on 1090 MHz (#172). Over a smooth earth with an effective radius of 4/3 of the earth's:

    d = sqrt(2 k R h1) + sqrt(2 k R h2), with k = 4/3 and R = 6371 km

Both aircraft, own-ship included, are at FL600, 18288 m, the top of class A airspace (14 CFR 71.33). Each term is 557.4 km, and d is 1114.8 km (`test/core/test_traffic_sanity.cpp`).

### No gate

The table holds every ADS-B target it can place, at any distance and height: the owner decided all traffic, with nothing to configure. Three rules bound it: `kAdsbRangeCeilingM` above, `adsb::Assembly` refusing a global decode past 180 NM of its reference (`protocols/adsb/README.md`), and eviction of the ground first, then the farthest slant, never a slot under an alarm.

The cost is NMEA time: `common::NmeaService` sends at most `kTargetsPerPass`, 10 `$PFLAA` a second, in rotation, so a full table of 48 refreshes each target about every fifth second.

Slant is measured from `set_own_reference`. Paired, a Bravo uses the Go's position (#192). **Without a fix of our own** every slant is 0, so eviction falls back to ground, then rank, then age, and nothing is graded or sent as `$PFLAA`.

### Accepted for v1

**Aircraft address not set.** The aircraft's own transponder may then cause repeated advisories. Formation does not reliably silence it: it needs the target within `kVerticalM`, holds eight targets (`formation.h` `kMaxTrackedTargets`), and never takes an unknown altitude (#161). A paired Go with both addresses unset lists it on its self-test page (`go::AircraftAddressWarning`, `products/skyblip_go/README.md`, Aircraft address), and a paired Bravo proposes the address from the transponder it hears loudest (`products/skyblip_go/pages/README.md`, aircraft). The proposal never filters on its own, because geometry cannot tell our transponder from a close wingman.

### An aircraft that carries a Go and a transponder

It shows once when its Go has `aircraft_address` set, because the Go then transmits under table 5 the address its transponder carries (ADS-L F.2.3, `core/settings/README.md`, The ICAO address). Unset, it shows twice.

## Sanity

A frame that passed its CRC can still carry a miscorrected position (`protocols/adsl/test/test_packet.cpp` counts them, and the uplink adds Reed-Solomon miscorrection), and a ghost on the radar teaches a pilot to stop believing it. So `range_check` refuses a slant past what the path could carry, as OGN does at 25 km (nrf52-ogn-tracker src/proc.cpp).

| Constant | Value | Where the number comes from |
|---|---|---|
| `kPlausibleRangeCeilingM` | 30000 | +14 dBm e.r.p. (ERC 70-03 band h1.4, `hardware/parts/sx1262/sx1262.h`) and about -107 dBm of bench sensitivity at 100 kchip/s give 121 dB. Free space at 868.2 MHz spends 120.8 dB by 30 km (`test/core/test_traffic.cpp`) |
| `kRelayedRangeCeilingM` | 60000 | two hops, and a ground station's mast hears at least as far as we do (`core/protocol/adsl_uplink.h`) |
| `kAdsbRangeCeilingM` | 1115000 | 1090 MHz line of sight, above |

30 km is a ceiling, not a range: a cockpit installation reaches about a third of it. It hides nothing anyone could act on, because the advisory ring is 3 km.

The gate sits in `TrafficTable::update`, the door every path comes through. It judges one observation at a time, so one ghost in an uplink frame does not take the other nine aircraft with it, and a refusal never displaces the slot its address holds. Refusals count in `implausible()`. Without a fix of our own there is nothing to measure from: `range_check` answers `NoReference` and the table keeps the target.

## Dismissal

`AlarmTracker::dismiss`, a long touch of the pad, silences every target already announced: the pilot has them in sight. A target not yet announced is not covered. The grade, the symbol and the tag stay, because a dismissal is about what the pilot has seen, not about the sky. So `bus::State` carries `alarm_level` for the sky, which the tablet gets, and `alarm_undismissed`, which the lamp and the glass read.

An aircraft is announced again only by arriving again. Leaving takes a whole `kRenotifyFloorMs` outside the window, so one on the boundary does not stutter. There is no timer, because an expiring dismissal would shout about the aeroplane the pilot is watching. While either side is on the ground the floor is `kGroundRenotifyFloorMs`, a minute, so a touch-and-go does not re-announce an aircraft that never left the ring.

An advisory is announced once, because a tone that repeats while a glider shares the thermal gets the device switched off.

## Which ADS-L version a client reads

`VersionTable` (`versions.h`) keeps §B.5's state apart from the display table, because the display keeps what a pilot should see and §B.5 asks about every client close enough to read our Traffic. A full display drops a parked aircraft before a far airborne one, and may show an aircraft from another system under the same address, so it cannot answer for §B.5. The Go owns one table and hands it to `TrafficService`, which writes it, and to `RadioService`, which reads it. Bravo sends no ADS-L and holds none.

An entry is one ADS-L client heard direct, keyed on its address table and address. It holds the version its last Traffic came in, the Max. Protocol Version of its last Status with the second it was heard, and the position, height, speed, track and climb its own last Traffic gave. A Status is filed whether or not its sender has sent Traffic or is on the display. `max_version()` is the Status's version while the Status holds, else the Traffic's, as §B.5 asks. A packet whose F.2 relay flag is set is not filed, because another station sent it and its sender may be out of our range, a reading EASA is asked to confirm (`docs/ADSL_EASA_QUESTIONS.md`, question 7). The uplink is a ground station's relay and is not filed either. Nor is a packet under an address we transmit under, our serial's under table 58 or our aircraft's ICAO address under table 5, which the display refuses too (`TrafficTable::own_aircraft`): an echo of our own Traffic, often version 0 by §E.1.1, would otherwise hold us to Issue 1. Another ADS-L device aboard under the same ICAO address cannot be told from that echo, so it is not filed either.

The position lapses with the display's lease, `kTargetForgetReports` of the client's own report periods: 6 s airborne, 60 s on the ground. The Status lapses after `kStatusForgetS`, 60 s, three of G.2.11's 20 s periods, so a client still sending keeps its declaration through two Statuses missed in a row. A lapsed declaration leaves the Traffic's version, which a conforming client never sends above its Status's, so lapsing early can only lower the version: own Traffic goes in Issue 1's form, or is held when it carries a field Issue 1 cannot write. The entry goes when both have lapsed.

`readable_version` returns the lowest `max_version()` among the clients `adsl::close_client()` calls close, both sides carried to the burst's instant and an unknown altitude counted as level. A client with no position is close to nobody.

The table holds `kMaxClients`, 48. A full table gives a new client the place of the one that matters least: a client not close before a close one, among clients not close the one heard longest ago, and among close ones the one reading the highest version, then the one heard longest ago. A newcomer that matters no more than that one is not filed, its Status included, though the radio log still shows the Status as `STAT`. So a far client never displaces a close one, and a close client is given up only for one that reads no more, which leaves `readable_version` as it was. Closeness here is judged at own-ship's last fix.

## Turn rate

ADS-L carries no turn rate (G.1.8, G.1.10), so the table differentiates each target's track, where the screen reads it. `flight::kTurnWindowMs` is the shortest window a 1 Hz track says anything over. `kTurnMaxAgeMs` is the gap past which the estimate re-arms instead of averaging over what it did not see. `flight::kTurnCeilingDps` clamps the result.

## Formation

Aircraft fly together on purpose, and the device cannot see intent, so `formation` tests geometry: a target within `kRangeM` and `kVerticalM` whose position in own-ship's heading-up frame stays within `kDriftM` for `kTogetherHoldMs`. A patrol, a tow and a circling pair all match it.

The station is where the target was first seen inside the band, not own-ship's position, so a glider on tow 60 m behind is not a wingman on arrival.

Nothing is announced. The plot draws a square around own-ship with a count per quadrant instead of the members' own symbols, and the radar's traffic count leaves them out. `nearby` still lists them (`products/skyblip_go/pages/README.md`).

**Closure takes the silence back.** A member closing at `kClosingMps` or more on `kClosingFixes` receptions in a row is graded like other traffic. 3 m/s is the smallest closure the wire can mean: ADS-L's 0.25 m/s and 512-step track quantisation make half a metre a second of phantom closure at 40 m/s (G.1.8, G.1.10). It is compared in millimetres a second, because the alarm's whole metres round 2.5 m/s up to 3. Two fixes, because two units in one car logged 3.1 m/s of closure on GNSS noise.

**A split is not a conflict.** A member that leaves its station for `kBreakFixes` fixes becomes `State::Parting` and stays quiet, because a break reads like a closure to an alarm that grades distance. Parting ends on a confirmed closure, on leaving the band, or on `kTogetherHoldMs` back on the station it left.

The station does not move while a member holds it or parts from it, so drift accumulates at any rate, and a slow peel-off cannot rejoin by re-anchoring every few fixes. Every count is a count of receptions: the tracker weighs each report once, however often the loop asks.

### Only while both are airborne

Own-ship on the ground forgets every membership, and a target on the ground leaves, because ground reports are 10 s apart and a parked aircraft holds station better than any patrol.

A target first anchored within `kLiftoffWindowMs` of either side's liftoff starts as `State::Joining`: silent, not yet a member. It becomes one after `kTogetherHoldMs` on station, and becomes traffic on its first fix off station or confirmed closure. So a pair lifting off together re-forms without an advisory, and an aircraft catching up after an interval takeoff is announced. The target's liftoff is recorded on the table entry, because a gliding grid parks more than the tracker's eight aircraft.

The design gives up the slow merge: a member drifting in under `kClosingMps` stays silent, 10 s across a 30 m gap. The pilot chose to fly beside it, and an annunciator that shouts all flight to cover those seconds gets switched off.

## Why the names are not in the table

`CallsignTable` uses the same key as `TrafficTable` but is separate, because a name outlives the position: it never changes in flight and is worth keeping for `kCallsignForgetS` after the aircraft drops off the plot. In `Target` it would be lost at every gap in reception. It holds one name per target slot, dropping the oldest first. ADS-L Type 66 frames fill it (`core/protocol/README.md`), and so do ADS-B identification messages under table 5, from Bravo's receiver or a paired Bravo's `callsign` (`../comms/README.md`). FLARM sends no name, so a FLARM target stays hex.
