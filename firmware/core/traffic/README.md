# core/traffic

What the sky around this aircraft contains, how dangerous it is, and who in it is flying with us. Pure, integer, no framework headers: the services in `products/` feed these and publish what they answer.

| File | What it decides |
|---|---|
| `sanity` | whether a decoded position is close enough to have been heard at all |
| `lease` | how long one reception is believed, for every layer that holds a slot |
| `table` | which aircraft the finite table holds, and each one's turn rate |
| `alarm` | whether a contact is an advisory, and what the annunciator is allowed to say |
| `formation` | which contacts are flying with us, on geometry alone |
| `range` | how far an emitter is and how far above, and the order the nearby page lists them in |
| `callsigns` | what each address is called, for as long as anybody should believe it |

## The model

There is one level, and it is a place. An aircraft inside `kAdvisoryDistM` of us and inside `kAdvisoryAltM` of our altitude is a traffic advisory; everything else is a contact on the plot and nothing more.

| Constant | Value | Where the number comes from |
|---|---|---|
| `kAdvisoryDistM` | 3000 | ours. At 240 kt of head-on closure it is 24 s, which is the band TCAS II issues a traffic advisory in at low level (SL3: tau 25 s, DMOD 0.33 NM). It is twice FLARM's own advisory ring |
| `kAdvisoryAltM` | 300 | FLARM's, exactly: its `$PFLAU` traffic advisory is an aircraft entering 1.5 km horizontally and 300 m vertically |

Advisory is the industry's word for this alert and the honest one for this device: a caution that says look, never an instruction. The level above it in every other system is the resolution advisory, which tells a crew to climb or descend and is coordinated with the other aircraft. This device has no link to coordinate over and no authority to give one, so it has nothing above the advisory and never will. `to_number` is the number the wire carries, and 1 is what `$PFLAA` and `$PFLAU` are sent: FLARM's level 1 is the lowest real alarm and the only band a 3 km ring does not overstate.

Why not a prediction. A predicted conflict is a better alarm for an aircraft under power on a track it holds, and it was what this layer did until the levels collapsed into one. What it cost was a model - a protection volume, a spread that grew with lead time, a horizon - and the model decided whether a pilot was told about an aeroplane a kilometre away. A ring decides nothing: it is where the aircraft is. The gaggle case that a prediction was carried for is handled where it belongs, by `formation` below, which silences aircraft that are flying with us rather than guessing at aircraft that are not.

A target that reports no velocity is still charged at `kUnknownTargetSpeedMps` in the closing figure the formation layer reads, because zero would make a relayed position the safest thing in the sky.

## How long an aircraft is held

Two questions, and they have nothing to do with each other. Have we lost this aircraft, which is about the link. Is this position fresh enough to grade, which is about metres a second. `lease.h` answers the first, `kAlertMaxAgeMs` the second.

`kTargetForgetReports` is 6: a target is dropped when six of its own transmissions have gone missing. The interval those are counted in is the sender's, from `flight::report_period_s`, so one multiple gives `kAirborneTargetForgetS` = 6 s and `kGroundTargetForgetS` = 60 s. Every layer that holds a slot takes its window from the observation itself - the table, the alarm tracker's dismissal, the formation membership - so a takeoff shortens the lease on the burst that announces it, and a slot cannot outlive the target it was opened for.

One multiple, and not two numbers, because the error it buys is the same at both ends. A target is drawn where it was last reported and nothing extrapolates it, so the lie on the glass at drop time is the period times the speed times the multiple. Airborne that is 6 s at 60 m/s, 360 m. On the ground it is 60 s at the 5 to 8 m/s nothing taxis faster than, 300 to 480 m. The ground interval is ten times longer and ground speed about ten times slower, which is why the same six reports cost about the same distance.

Six is where a fade stops being a fade. At 1 Hz, six consecutive misses is a link that stopped rather than a burst that collided: even at half the bursts lost it is 1.5% of the time. It was twelve seconds for everything, which airborne was 720 m of lie, and on the ground was 1.2 of an emitter's own intervals - one missed transmission deleted a parked or taxiing aircraft, and the symbol blinked once a cycle on a screen that redraws every second.

`kAlertMaxAgeMs` is 5 s: what the alarm is allowed to grade. It acts on a position, and at 100 m/s of closure a five-second-old fix is already 500 m of uncertainty, so a contact older than that is no longer something to say a word about. It does not scale with the sender's interval and must not: stretching it to a minute for a parked aircraft would be grading a position half a kilometre wrong. What the ground rate asks for instead is below.

`direct_preferred_max_age_s` is the one figure that takes the larger of the two: the alarm's patience airborne, for the reason `table.h` gives, and the sender's own interval on the ground. At 0.1 Hz a relay allowed past after five seconds replaces every ground report with the poorer copy of itself, for ever, when the next direct report is not even due.

## An aircraft on the ground is not traffic

A contact whose G.1.2 code says it is on the ground is drawn, named and listed, and never graded: `assess` returns `Level::None` for it whatever the geometry. A ring of 3 km and 300 m over an airfield is every aeroplane on the apron, and a 0.1 Hz emitter is inside `kAlertMaxAgeMs` for five seconds in every ten anyway, so half of those advisories would be decided on a position the same layer calls too old. `formation` ignores the same contacts, because a tug and its glider hold station on the apron as well as they do on tow.

Unknown is not ground: ALP-TAS carries no state at all, and an emitter that does not say where it is gets the airborne treatment in both places.

## Dismissal

A pilot who has the aircraft in sight has everything the device was trying to give them, and from that moment the annunciator is noise. `AlarmTracker::dismiss` is what a long touch of the pad reaches: what has already been said is not said again, and the buzzer is released mid-pattern.

It is spent per aircraft, on the slot rather than on the tracker: one touch marks every contact that has been announced, and a contact the device has not spoken about yet is not covered, because the claim a pilot makes is about the aeroplanes they were told of. It takes back everything the device is saying about them: the tone, the sector on the glass and the lamp. The grade stands and the target keeps its symbol, its leader and its tag, because the dismissal is a claim about what the pilot has seen and not a claim about the sky - which is also why `bus::State` carries two levels, `alarm_level` for the sky and `alarm_live` for what is still being said about it. The tablet is told the first, over NMEA and from the target's own grade; the lamp and the panel read the second.

An aircraft says it again by arriving again: a contact that leaves the window and comes back is announced, and clears its own dismissal, while one that has been there all along stays quiet. Leaving is not instant either, because the level the tracker last said falls back only after a whole `kRenotifyFloorMs` outside, so an aeroplane sitting on the boundary is one announcement and not a stutter. An aircraft heard for the first time was never dismissed. The rest of the sky stays quiet, which is what makes one touch in a busy circuit hold. That is also why there is no timer on it: a dismissal that expired would shout again about the aeroplane the pilot is looking at.

An advisory is announced once and never repeats itself. A ring can stand for a whole climb, and a tone that came back every two seconds for as long as a glider shared the thermal is the device switched off.

What is deliberately absent: there is no co-circling test, no gaggle range gate, no steady-range timer, and no constant anywhere in this directory that assumes a glider.

## Turn rate

ADS-L carries position, speed, track and climb, and no turn rate (G.1.8, G.1.10), so a neighbour's is differentiated from the tracks it has reported. It lives on the table entry rather than inside the alarm, because the screen needs the same number: `flight::kTurnWindowMs` is the shortest window a 1 Hz track says anything over, the same figure own-ship differentiates its own track with, and `kTurnMaxGapMs` is the gap past which two reports are two facts rather than a rate, so the estimate re-arms instead of averaging across what it did not see. `flight::kMaxTurnDps` clamps what is believed; above it the figure describes the receiver.

## Formation

Aircraft fly together on purpose: a patrol, a tug and its glider, two friends on a task, a gaggle in one thermal. The device cannot see intent, so `formation` names the observable: a contact within `kRangeM` and `kVertM` whose position in own-ship's own heading-up frame has not moved more than `kDriftM` for `kTogetherHoldMs`. That covers all four cases without naming any of them, and a circling pair matches it for the same reason a patrol does.

The station is where the contact was first seen, not where own-ship is. A slot anchors on its first fix inside the band and the box is measured from there, so a contact that appears 40 m off the wing and swings to 40 m off the other one has moved 80 m and holds no station. The anchor used to start at own-ship's own position, which made everything inside 60 m a wingman on arrival - and 60 m is a glider on tow.

Nothing is asked and nothing is announced. The device says it in the picture it was already drawing: the square closes around own-ship as the formation forms, the count moves between quadrants, the square opens again when the last member leaves. A pilot who wanted a word for it is a pilot reading words instead of a plot, and the aircraft it is about is out of the window.

Three rules keep it honest:

A member is silenced on the annunciator and never on the plot. It stops being drawn as a separate symbol because the square around own-ship and the count in its quadrant are its depiction, and drawing it twice would be two aircraft.

**Closure takes the silence back.** A member closing at `kClosingMps` or more is released on that fix and graded like any other traffic. Holding station is the whole claim the detector makes, and an aircraft coming at us is not holding station, so the silence ends before the drift test has had two fixes to notice. The figure is 3 m/s because the wire cannot say anything smaller and mean it: ADS-L quantises ground speed at 0.25 m/s and track at 512 steps of a turn, which is half a metre a second of phantom closure at 40 m/s (G.1.8, G.1.10). What it costs is named below.

**A split is not a conflict.** When station keeping breaks, the contact becomes `State::Parting` rather than traffic again, and stays quiet while it goes. Two aircraft leaving each other are the least surprising thing in the sky, and the geometry of a break reads like a closure to an alarm that grades distance. Parting ends the way it must: the moment they close again by `kClosingMps`, when they are out of the band entirely and are two aircraft that have nothing to do with each other, or when the contact is back on the station it left for `kTogetherHoldMs`, which is a rejoin.

The break is measured from the station, and `kBreakFixes` is what one bad solution cannot fake. While a member holds station the anchor does not move, so a departure accumulates against it: 60 m of drift and two fixes past it, whatever the rate it leaves at. The anchor used to be re-set on every fix that missed the box, which asked for 60 m twice in a row - 60 m/s of separation - and anything slower stayed inside the square until it crossed `kRangeM`. A wingman peeling off at 10 m/s is drawn as its own symbol eight seconds in, where it took a hundred before.

The station does not move while the contact is Parting either. A slow peel-off holds any box for six fixes, 10 m/s being exactly 60 m in six seconds, so a Parting slot re-anchored on the break rejoined every six seconds until it crossed `kRangeM`, and the square flickered. Only the station it left takes it back: a wingman sliding back into its slot rejoins, and one that settles 500 m out stays Parting, quiet and drawn on its own, until it closes or leaves the band.

The lease ends by itself: a contact nobody has heard for its own lease (`lease.h`) is forgotten with its membership, and a neighbour that settles back on station for `kTogetherHoldMs` rejoins. Addresses rotate only between flights, so a slot reallocated to another aircraft starts at `State::None`.

What this design gives up, deliberately, is the slow merge. A member drifting in at less than `kClosingMps` stays silent, and 3 m/s across a 30 m gap is ten seconds. The alarm is not the thing protecting that pair: they have been in formation for at least `kTogetherHoldMs`, the pilot is looking out at an aircraft they chose to fly next to, and an annunciator that shouts through the whole flight to cover those ten seconds is an annunciator switched off before them.

## Why the names are not in the table

`CallsignTable` is keyed on the same pair `TrafficTable` is, the address table and the address, and it is a separate table because a name and a position have nothing in common but that key. A position is an observation: it is worthless in six of the sender's own reports and the table forgets it. A name arrives once every ten seconds at best, never changes in flight, and is worth keeping long after the aircraft has dropped off the plot and come back, which is what `kCallsignForgetS` is for. Putting it in `Target` would have thrown the name away with every gap in reception, and made every reader of an observation carry fourteen bytes it does not use.

It holds one name per target slot, so everything the traffic table can track can be named, and past that the aircraft heard longest ago loses its name first. Only ADS-L Type 66 frames fill it (`core/protocol/README.md`): FLARM broadcasts no name at all, so a FLARM-path target stays hex for ever - and so does the same aircraft's ADS-L identity if the two arrive under different address tables, which is correct rather than unfortunate. Those are two identities on the wire and this device does not guess that they are one aircraft.
