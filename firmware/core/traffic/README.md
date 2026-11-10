# core/traffic

What the sky around this aircraft contains, how dangerous it is, and who in it is flying with us. Pure, integer, no framework headers: the services in `products/` feed these and publish what they answer.

| File | What it decides |
|---|---|
| `sanity` | whether a decoded position is close enough to have been heard at all |
| `gate` | whether an ADS-B target is near enough, in distance and in height, to take a slot |
| `lease` | how long one reception is believed, for every layer that holds a slot |
| `table` | which aircraft the finite table holds, and each one's turn rate |
| `alarm` | whether a target is an advisory, and what the annunciator is allowed to say |
| `formation` | which targets are flying with us, on geometry alone |
| `range` | how far a target is and how far above, and the order the nearby page lists them in |
| `callsigns` | what each address is called, for as long as anybody should believe it |

## The model

There is one level, and it is a place. An aircraft inside `kAdvisoryDistanceM` of us and inside `kAdvisoryAltitudeM` of our altitude is a traffic advisory; everything else is a target on the plot and nothing more.

| Constant | Value | Where the number comes from |
|---|---|---|
| `kAdvisoryDistanceM` | 3000 | ours. At 240 kt of head-on closure it is 24 s, which is the band TCAS II issues a traffic advisory in at low level (SL3: tau 25 s, DMOD 0.33 NM). It is twice FLARM's own advisory ring |
| `kAdvisoryAltitudeM` | 300 | FLARM's, exactly: its `$PFLAU` traffic advisory is an aircraft entering 1.5 km horizontally and 300 m vertically |

Advisory is the industry's word for this alert and the honest one for this device: a caution that says look, never an instruction. The level above it in every other system is the resolution advisory, which tells a crew to climb or descend and is coordinated with the other aircraft. This device has no link to coordinate over and no authority to give one, so it has nothing above the advisory and never will. `to_number` is the number the wire carries, and 1 is what `$PFLAA` and `$PFLAU` are sent: FLARM's level 1 is the lowest real alarm and the only band a 3 km ring does not overstate.

Why not a prediction. A predicted conflict is a better alarm for an aircraft under power on a track it holds, and it was what this layer did until the levels collapsed into one. What it cost was a model - a protection volume, a spread that grew with lead time, a horizon - and the model decided whether a pilot was told about an aeroplane a kilometre away. A ring decides nothing: it is where the aircraft is. The gaggle case that a prediction was carried for is handled where it belongs, by `formation` below, which silences aircraft that are flying with us rather than guessing at aircraft that are not.

A target that reports no velocity is still charged at `kUnknownTargetSpeedMps` in the closing figure the formation layer reads, because zero would make a relayed position the safest thing in the sky.

## A target that sends no altitude

ADS-L lets a position mark its altitude invalid (G.1.7), and an ADS-B target may send no altitude at all. Unknown is a state, not 0 m. `flight::relative_north_east_up` says whether the height it returns is real, through `up_valid`, and every reader decides what unknown means for it. None of them may read the target as level with us:

- `alarm` grades it on distance alone: inside `kAdvisoryDistanceM` it is an advisory, because it may be at our level. `AlarmAssessment::relative_altitude_valid` is false, so neither the radar, `nearby` nor the tablet prints a separation it does not have. The records do not carry the flag yet: the flight log (`products/common/services/flight_log.cpp`) and the diagnostics `Traffic` record (`products/common/services/alarm.cpp`) write 0 for an unknown altitude, and `scripts/blip_records.py` decodes that 0 as a separation. #203 adds the flag.
- `formation` never makes it a member, and a member that stops sending its altitude leaves on that report. Holding station is a claim about three axes, and a target with no height cannot be seen to hold one.
- `range` gives it a ground distance and `RangeRow::up_valid` false, which `nearby` prints as `--`.
- `sanity` checks its ground distance, since there is no slant to take.
- `gate` holds an ADS-B target on its distance alone, below.

What the radar and the list draw for it is in `products/skyblip_go/pages/README.md`, and what the tablet is sent in `core/protocol/README.md`.

## How long an aircraft is held

Two questions, and they have nothing to do with each other. Have we lost this aircraft, which is about the link. Is this position fresh enough to grade, which is about metres a second. `lease.h` answers the first, `kAlertMaxAgeMs` the second.

`kTargetForgetReports` is 6: a target is dropped when six of its own transmissions have gone missing. The interval those are counted in is the target's, from `flight::report_period_s`, so one multiple gives `kAirborneTargetForgetS` = 6 s and `kGroundTargetForgetS` = 60 s. Every layer that holds a slot takes its window from the observation itself - the table, the alarm tracker's dismissal, the formation membership - so a takeoff shortens the lease on the burst that announces it, and a slot cannot outlive the target it was opened for.

One multiple, and not two numbers, because the error it buys is the same at both ends. A target is drawn where it was last reported and nothing extrapolates it, so the lie on the glass at drop time is the period times the speed times the multiple. Airborne that is 6 s at 60 m/s, 360 m. On the ground it is 60 s at the 5 to 8 m/s nothing taxis faster than, 300 to 480 m. The ground interval is ten times longer and ground speed about ten times slower, which is why the same six reports cost about the same distance.

Six is where a fade stops being a fade. At 1 Hz, six consecutive misses is a link that stopped rather than a burst that collided: even at half the bursts lost it is 1.5% of the time. It was twelve seconds for everything, which airborne was 720 m of lie, and on the ground was 1.2 of a target's own intervals - one missed transmission deleted a parked or taxiing aircraft, and the symbol blinked once a cycle on a screen that redraws every second.

`kAlertMaxAgeMs` is 5 s: what the alarm is allowed to grade. It acts on a position, and at 100 m/s of closure a five-second-old fix is already 500 m of uncertainty, so a target older than that is no longer something to say a word about. It does not scale with the target's interval and must not: stretching it to a minute for a parked aircraft would be grading a position half a kilometre wrong. What the ground rate asks for instead is below.

`direct_preferred_max_age_s` is the one figure that takes the larger of the two: the alarm's patience airborne, for the reason `table.h` gives, and the target's own interval on the ground. At 0.1 Hz a relay allowed past after five seconds replaces every ground report with the poorer copy of itself, for ever, when the next direct report is not even due.

## An aircraft on the ground is not traffic

A target whose G.1.2 code says it is on the ground is drawn, named and listed, and never graded: `assess` returns `Level::None` for it whatever the geometry. A ring of 3 km and 300 m over an airfield is every aeroplane on the apron, and a 0.1 Hz target is inside `kAlertMaxAgeMs` for five seconds in every ten anyway, so half of those advisories would be decided on a position the same layer calls too old. `formation` ignores the same targets, because a tug and its glider hold station on the apron as well as they do on tow.

A full table evicts anything on the ground before anything in the air, however near the one and far the other. A parked aircraft is held for a minute, and a table that kept its nearest regardless would let a busy apron keep an arrival off the plot.

Unknown is not ground: a target that does not say where it is gets the airborne treatment everywhere, the six-second lease included.

## Own-ship on the ground grades nothing

The same rule turned around: while own-ship's flight state is on the ground, `assess` returns `Level::None` for every target, however close and whatever it says about itself. A unit at the hold beside a busy circuit is inside 3 km and 300 m of every aircraft flying it, and it announced each one that climbed out, turned in or came back into reception, for as long as it sat there. FLARM does the same: its warnings are disabled while the own aircraft is taxiing or stationary on the ground.

The liftoff is when the device starts talking. Every target inside the ring becomes an advisory on the same pass, and the annunciator plays one tune for a pass however many arrived on it, so a departure into a full circuit is one advisory and not one per aircraft. Targets within formation range are the exception, below.

## Whichever system heard it

The ground rules above read one field, `AircraftObservation::flight_state`, and never ask which system a report came from. The field holds the ADS-L G.1.2 code whatever the wire was, and `flight::state_from` is its only reader, so a code nobody named is `Unknown`. Each decoder fills it: ADS-L copies G.1.2, the uplink record carries the same two bits, and ALP-TAS maps its own two-bit field in `alptas_flight_state` (1 on the ground, 2 airborne, 3 airborne and circling, and 0, which no known sender transmits, `Unknown`). The lease, the grade, the direct hold and the formation all key off that field and nothing else.

ADS-B fills it in the assembler (#190): `Ground` from a surface position (type code 5 to 8) or a DF17 capability of 4, `Airborne` from an airborne position (type code 9 to 22) or a capability of 5. Any other message leaves a fresh state as it is, and `flight_state_age_ms` keeps the instant it was true. An aircraft that has sent no such evidence yet is `Unknown`, and gets the airborne treatment like any other.

A new inbound source owes this directory one thing: write the field, `Unknown` when its wire has no state, and it inherits everything above without a line changing here. `test/core/test_traffic_sources.cpp` flies one parked aircraft through every decoder and holds them all to the same answers, so a source added to its table is checked the day it lands.

## ADS-B

ADS-B reaches the table as `Source::Adsb`: from the LR2021 on a Bravo alone, and from a paired Bravo on a Go (#193). The rules below are the table's and `relative_north_east_up`'s, so both products apply them the same way.

### Which height is compared

`flight::relative_north_east_up` takes a target's height in this order:

1. its height above the ellipsoid, `altitude_m`, against own-ship's `altitude_hae_mm`;
2. else its pressure altitude, `pressure_altitude_ft`, against own-ship's `pressure_altitude_mm`;
3. else none: `up_valid` is false, as above.

A height is only compared with its own kind. A pressure altitude is measured on 1013.25 hPa, and it differs from a GNSS height by the geoid and by the day's weather. ADS-L, FLARM and the uplink send no pressure altitude, so they take the first branch or none, as before.

The rule reads the validity flags only. Whether a field is still fresh enough to be valid is decided from its age where the observation is assembled (#190), and own-ship's pressure altitude is expired by whoever publishes it: #193 on the Go and #176 on Bravo. Until they do, no product publishes one, so a target that sends only a pressure altitude is unknown.

`flight::carried_to` carries `altitude_m` by the target's climb, and leaves both pressure altitudes as they were reported. At the 1.5 s `flight::kExtrapolationCeilingMs` and 10 m/s of climb, that is 15 m inside a 300 m band.

### One aircraft heard on several paths

`source_rank` puts a direct ADS-L or FLARM reception at 3, ADS-B at 2 and the uplink at 1, the order epic #184 decided. A lower-ranked copy never displaces a higher-ranked one younger than `direct_preferred_max_age_s`, however much newer it is. Past that age it takes over, and the higher-ranked copy takes the target back on its next reception. ADS-B ranks below a direct reception because it sends each field in a message of its own, with its own age, while ADS-L and FLARM carry them all in one burst. On a paired Go it also crosses a second link, from the Bravo. It ranks above the uplink, which relays what a ground station heard.

The table merges two reports only when both the address mapping table and the address match. An ADS-B address is an ICAO address, table 5 in ADS-L F.2.2 (`settings::kIcaoAddressMappingTable`), and the ADS-B assembler (#190) must write that table on every observation. So an ADS-L or FLARM unit merges with its ADS-B copy only when it sends the aircraft's ICAO address, which F.2.3 asks of an aircraft that also carries a transponder. Under any other table it is a second identity, and the table does not guess that the two are one aircraft.

### Our own aircraft

The table refuses two addresses, before any other check:

- our own ADS-L address, under our table 58, which `common::TrafficService` sets on every pass from the activation (`set_own_address`);
- our aircraft's ICAO address, under table 5 (`set_own_icao`). That is the transponder of the aircraft we fly in. A Bravo in the same cockpit hears it louder than anything, a FLARM set to that address sends it, and a ground station relays it.

Zero sets an entry to filter nothing. Until the Go has an own-ICAO setting (#191), `common::TrafficService` passes 0.

### A report with no position

An ADS-B observation can exist before its position does, such as half a CPR pair (`core/model/README.md`). The table refuses `Source::Adsb` without `position_valid`, before the sanity check, and counts it in `adsb_without_position()`, so an assembler that hands one over shows up. Admitted, it would have had a slant of 0, and a full table would have kept it over every real target. So the ADS-B assembler (#190) may hand the table an observation only once it holds a position, and the rule above stops one that slips through. Other sources are unchanged.

### Sanity: the radio horizon

`kAdsbRangeCeilingM` is 1115 km. The ADS-L ceiling is our link budget, but nobody has measured what the LR2021 hears on 1090 MHz yet (#172), so the bound that holds without a bench figure is line of sight. Over a smooth earth, with the standard atmosphere's effective radius of 4/3 of the earth's:

    d = sqrt(2 k R h1) + sqrt(2 k R h2), with k = 4/3 and R = 6371 km

Both aircraft are put at FL600, 18288 m, the top of the class A airspace airliners and business jets fly in (14 CFR 71.33). Each term is then 557.4 km, and d is 1114.8 km. Own-ship is put at FL600 too, because the ceiling has to hold wherever the receiver flies. `test/core/test_traffic_sanity.cpp` computes the figure. A position further than that was not sent from where the aircraft is: it is a decode gone wrong, and it counts in `implausible()` like any other.

### The gate

The table already keeps its nearest targets: a full table evicts the farthest slot, and never one under an advisory. So the gate does not protect ADS-L from a flood of ADS-B. Nearest-first does that. The gate keeps out the ADS-B a pilot has no use for, so that it holds no slot and no NMEA time: `common::NmeaService` sends at most `kTargetsPerPass`, 10 targets a second, so a full table sends each target only every fifth second.

It applies to `Source::Adsb` only, and refuses a target that is either:

- further than `kAdsbGateRangeCeilingM` over the ground, which is `kPlausibleRangeCeilingM`, 30 km. No ADS-B target is held further away than a direct ADS-L or FLARM target can be. The radar's widest range is 16 km (`kRangeStepsKm`), and the alarm acts inside 3 km. For comparison, PowerFLARM's `ADSBRANGE` defaults to unlimited, and its `PCASRANGE` for transponder-only targets to 7408 m (FLARM FTD-014, version 1.18 of 2024-12-19, 3.2.10 and 3.2.8);
- or more than `kAdsbGateHeightCeilingM`, 500 m, above or below us. That is the default of PowerFLARM's `VRANGE`, the vertical range of the aircraft it shows (FTD-014, 3.1.6, values 500 to 2000). It is 200 m wider than the alarm's `kAdvisoryAltitudeM`, so a target is held before it can become an advisory: closing at 10 m/s vertically, it enters the table 20 s before it enters the band. FLARM's own `ADSBVRANGE` defaults to unlimited (3.2.11), and its `PCASVRANGE` to 610 m (3.2.9).

The gate measures from the table's reference, `set_own_reference`. Paired, a Bravo gates its exports on the Go's position (#192). A target the gate refuses or releases counts in `adsb_outside_gate()` and not in `implausible()`, because a real aircraft far away is not a decoder fault. No link reports it yet.

The gate decides admission, and it never freezes a slot. A report from an aircraft the table does not hold yet takes no slot when it is outside the gate. A report outside the gate from an aircraft the table holds releases its slot on that report, when the slot would have taken the report: `prefer_new` still keeps a fresher, higher-ranked copy. The one exception is a slot under an alarm: the report refreshes it instead, so the alarm grades the new position, and the next report outside the gate releases it once the alarm has dropped. An ADS-B advisory with an unknown altitude that next reports a height 800 m above us therefore stops being an advisory on that report, rather than standing on the radar for its 6 s lease.

**Unknown altitude** is neither inside nor outside a height band. The gate holds such a target on its distance alone, as `alarm` grades it on distance alone: it may be at our level. That includes a target that sends only a pressure altitude while own-ship has none.

**Without a fix of our own** there is no reference, so the gate holds every ADS-B target and `weight_of` gives every slot a slant of 0. Eviction then reads the ground first, then the rank, then the age. Among targets with the same ground state, a direct ADS-L or FLARM target outranks ADS-B, which outranks the uplink, and among ADS-B targets the one heard longest ago goes. No slot under an alarm is evicted. So a receiver without a fix holds the ADS-B aircraft it heard most recently, wherever they are. It grades none of them and sends none as `$PFLAA`, because `assess` and `NmeaService::emit_targets` both need our fix. When the fix comes, each far aircraft is released on its next report, and nearer aircraft take the slots as they report.

### Accepted for v1

**Own ICAO not set.** Without it, the aircraft's own transponder may cause persistent or repeated advisories. The formation layer is not an own-aircraft filter and does not guarantee silence: it needs the target within `kVerticalM` (100 m) of us, holds eight targets (`formation.h` `kMaxTrackedTargets`), and never takes a target whose altitude is unknown (#161). Accepted for v1, and #191 has the Go warn about it.

**An aircraft carrying both a Go and a transponder** shows twice on another paired Go: once as its Go's ADS-L address under table 58, and once as its ICAO address under table 5. The cause is the Go: it always transmits under table 58. ADS-L F.2.3 says that an aircraft that also uses a Mode-S transponder "shall use" its ICAO address, and that the ADS-L system "shall enforce this" (`docs/reference/ads-l_4_srd860_issue_2.md`, F.2.3). The table merges on the address pair and does not guess that the two are one aircraft. Accepted for v1, and the site says so (#197).

## Dismissal

A pilot who has the aircraft in sight has everything the device was trying to give them, and from that moment the annunciator is noise. `AlarmTracker::dismiss` is what a long touch of the pad reaches: what has already been said is not said again, and the buzzer is released mid-pattern.

It is spent per aircraft, on the slot rather than on the tracker: one touch marks every target that has been announced, and a target the device has not spoken about yet is not covered, because the claim a pilot makes is about the aeroplanes they were told of. It takes back everything the device is saying about them: the tone, the sector on the glass and the lamp. The grade stands and the target keeps its symbol, its leader and its tag, because the dismissal is a claim about what the pilot has seen and not a claim about the sky - which is also why `bus::State` carries two levels, `alarm_level` for the sky and `alarm_undismissed` for what is still being said about it. The tablet is told the first, over NMEA and from the target's own grade; the lamp and the panel read the second.

An aircraft says it again by arriving again: a target that leaves the window and comes back is announced, and clears its own dismissal, while one that has been there all along stays quiet. Leaving is not instant either, because the level the tracker last said falls back only after a whole `kRenotifyFloorMs` outside, so an aeroplane sitting on the boundary is one announcement and not a stutter. An aircraft heard for the first time was never dismissed. The rest of the sky stays quiet, which is what makes one touch in a busy circuit hold. That is also why there is no timer on it: a dismissal that expired would shout again about the aeroplane the pilot is looking at.

A ground roll is not leaving. While either side is on the ground the level falls back only after `kGroundRenotifyFloorMs`, a minute: a touch-and-go, a stop-and-go or own-ship's own is back in the air well inside it, and the climb-out used to announce again an aircraft that never left the ring, once a lap. An aircraft parked for longer than that is announced as it lifts off, and so is the circuit when own-ship lifts off after a longer stop.

An advisory is announced once and never repeats itself. A ring can stand for a whole climb, and a tone that came back every two seconds for as long as a glider shared the thermal is the device switched off.

What is deliberately absent: there is no co-circling test, no gaggle range gate, no steady-range timer, and no constant anywhere in this directory that assumes a glider.

## Turn rate

ADS-L carries position, speed, track and climb, and no turn rate (G.1.8, G.1.10), so a target's is differentiated from the tracks it has reported. It lives on the table entry rather than inside the alarm, because the screen needs the same number: `flight::kTurnWindowMs` is the shortest window a 1 Hz track says anything over, the same figure own-ship differentiates its own track with, and `kTurnMaxAgeMs` is the gap past which two reports are two facts rather than a rate, so the estimate re-arms instead of averaging across what it did not see. `flight::kTurnCeilingDps` clamps what is believed; above it the figure describes the receiver.

## Formation

Aircraft fly together on purpose: a patrol, a tug and its glider, two friends on a task, a gaggle in one thermal. The device cannot see intent, so `formation` names the observable: a target within `kRangeM` and `kVerticalM` whose position in own-ship's own heading-up frame has not moved more than `kDriftM` for `kTogetherHoldMs`. That covers all four cases without naming any of them, and a circling pair matches it for the same reason a patrol does.

The station is where the target was first seen, not where own-ship is. A slot anchors on its first fix inside the band and the box is measured from there, so a target that appears 40 m off the wing and swings to 40 m off the other one has moved 80 m and holds no station. The anchor used to start at own-ship's own position, which made everything inside 60 m a wingman on arrival - and 60 m is a glider on tow.

Nothing is asked and nothing is announced. The device says it in the picture it was already drawing: the square closes around own-ship as the formation forms, the count moves between quadrants, the square opens again when the last member leaves. A pilot who wanted a word for it is a pilot reading words instead of a plot, and the aircraft it is about is out of the window.

Three rules keep it honest:

A member is silenced on the annunciator and never on the plot. It stops being drawn as a separate symbol because the square around own-ship and the count in its quadrant are its depiction, and drawing it twice would be two aircraft. For the same reason the radar's traffic count leaves it out, and it does not strike own-ship's vector, which is drawn against traffic in the ring; `nearby` still lists it (`products/skyblip_go/pages/README.md`).

**Closure takes the silence back.** A member closing at `kClosingMps` or more on `kClosingFixes` receptions in a row is released and graded like any other traffic. Holding station is the whole claim the detector makes, and an aircraft coming at us is not holding station. The figure is 3 m/s because the wire cannot say anything smaller and mean it: ADS-L quantises ground speed at 0.25 m/s and track at 512 steps of a turn, which is half a metre a second of phantom closure at 40 m/s (G.1.8, G.1.10). It is compared in millimetres a second: the whole metres the alarm grades in rounded 2.5 m/s up to 3. Two fixes, because a tight pair is where the closing figure is least true: four metres apart, the line of sight it is projected on is GNSS noise, and a replay of the flight logs of two units in one car put 3.1 m/s of closure on it. What it costs is named below.

**A split is not a conflict.** When station keeping breaks, the target becomes `State::Parting` rather than traffic again, and stays quiet while it goes. Two aircraft leaving each other are the least surprising thing in the sky, and the geometry of a break reads like a closure to an alarm that grades distance. Parting ends the way it must: when they close again by `kClosingMps` for `kClosingFixes`, when they are out of the band entirely and are two aircraft that have nothing to do with each other, or when the target is back on the station it left for `kTogetherHoldMs`, which is a rejoin.

Every count here is a count of receptions. The service loop asks about every target on every pass, a hundred times a second, and it used to re-read one report each time: two passes 10 ms apart were the two fixes a break asks for, and one bad solution broke a formation on its own. The tracker now weighs a report once, on the pass it arrives.

The break is measured from the station, and `kBreakFixes` is what one bad solution cannot fake. While a member holds station the anchor does not move, so a departure accumulates against it: 60 m of drift and two fixes past it, whatever the rate it leaves at. The anchor used to be re-set on every fix that missed the box, which asked for 60 m twice in a row - 60 m/s of separation - and anything slower stayed inside the square until it crossed `kRangeM`. A wingman peeling off at 10 m/s is drawn as its own symbol eight seconds in, where it took a hundred before.

The station does not move while the target is Parting either. A slow peel-off holds any box for six fixes, 10 m/s being exactly 60 m in six seconds, so a Parting slot re-anchored on the break rejoined every six seconds until it crossed `kRangeM`, and the square flickered. Only the station it left takes it back: a wingman sliding back into its slot rejoins, and one that settles 500 m out stays Parting, quiet and drawn on its own, until it closes or leaves the band.

The lease ends by itself: a target nobody has heard for its own lease (`lease.h`) is forgotten with its membership, and a member that reports itself on the ground leaves on that report rather than counting on the square until the lease runs out. Addresses rotate only between flights, so a slot reallocated to another aircraft starts at `State::None`.

### Only while both are airborne

A formation is a fact about two aircraft in the air. Own-ship on the ground forgets every membership on the pass it lands, the same way a target on the ground leaves on its report. On the ground there is nothing to test it with either: a rolling aircraft reports once every ten seconds and covers 150 m between two of them, a parked one holds station better than any patrol, and a formation kept through the landing would still be silencing the partner when it departed alone hours later.

What that would cost at every takeoff is paid instead at the liftoff. A fresh slot is traffic, so a pair lifting off together, a tug lifting off ahead of its glider and a pair pulling away from a stop all re-formed through an advisory, one for every takeoff. So a target first anchored within `kLiftoffWindowMs` of the moment the pair became airborne, by own-ship lifting off or by the target's own liftoff, starts as `State::Joining`: silent, and not a member yet. It becomes one after `kTogetherHoldMs` on station, the same test any wingman passes. One fix off station or a confirmed closure makes it traffic on that fix, and it is never offered the join again until the next liftoff. A second aircraft catching up after an interval takeoff is a closure, and is announced.

The target's liftoff is the one the traffic table saw: a report in the air from a target whose last report was on the ground. It lives on the table entry because the table holds every target, while this tracker holds eight, and a gliding grid has more than eight aircraft parked inside a kilometre.

What the liftoff costs is time: a target within formation range that lifts with us and does not keep station is announced on the fix it leaves its station, up to `kTogetherHoldMs` after it was first seen. One closing on us is announced after two fixes of it.

What this design gives up, deliberately, is the slow merge. A member drifting in at less than `kClosingMps` stays silent, and 3 m/s across a 30 m gap is ten seconds. The alarm is not the thing protecting that pair: they have been in formation for at least `kTogetherHoldMs`, the pilot is looking out at an aircraft they chose to fly next to, and an annunciator that shouts through the whole flight to cover those ten seconds is an annunciator switched off before them.

## Why the names are not in the table

`CallsignTable` is keyed on the same pair `TrafficTable` is, the address table and the address, and it is a separate table because a name and a position have nothing in common but that key. A position is an observation: it is worthless in six of the target's own reports and the table forgets it. A name arrives once every ten seconds at best, never changes in flight, and is worth keeping long after the aircraft has dropped off the plot and come back, which is what `kCallsignForgetS` is for. Putting it in `Target` would have thrown the name away with every gap in reception, and made every reader of an observation carry fourteen bytes it does not use.

It holds one name per target slot, so everything the traffic table can track can be named, and past that the aircraft heard longest ago loses its name first. Only ADS-L Type 66 frames fill it (`core/protocol/README.md`): FLARM broadcasts no name at all, so a FLARM-path target stays hex for ever - and so does the same aircraft's ADS-L identity if the two arrive under different address tables, which is correct rather than unfortunate. Those are two identities on the wire and this device does not guess that they are one aircraft.
