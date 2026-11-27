# products/skyblip_go/pages

Each file draws one page as a pure function from a snapshot struct to pixels. A page reads, owns and decides nothing: `go::ScreenService` fills the snapshot, so every page is tested without a device, a bus or a clock.

The pages live with the product rather than in `ui/`, because they map one for one onto `go::Page` and no ground station draws them. They draw on `go::Glass` (`../glass.h`), the 200x200 panel of the T-Echo Plus (`boards/lilygo/t_echo_plus/glass.h`). Every figure below is measured for that glass at arm's length in sunlight, so another glass re-measures them rather than scaling them.

## The unit every page reads in

`settings::units` sets distance and speed on every page that prints them: the radar's range, `nearby`'s distance and `sixpack`'s speed dial. It is `Nautical`, nautical miles and knots, out of the box, or `Metric`, kilometres and km/h. The other choice is not called imperial, because knots and flight levels are not imperial units and the word would invite statute miles onto a page.

Vertical is feet and feet per minute on every page, whatever the setting, because levels are cleared and climbs flown in feet everywhere.

`status` ignores the setting. It prints the aeronautical and the SI figure side by side, so a bench converts nothing.

## radar

The radar is heading up: own ship points up and the picture turns, so a target lies on the bearing a pilot looks along. No heading is written on it, because `sixpack` shows the track in figures.

`radar_geometry.h` places the scope: the centre, the rings, the rim, the footer and the heading-up turn. `radar_traffic.cpp` plots the aircraft with their leaders, own ship's path and the rim. `radar.cpp` draws the ring, the readings, the formation and the alarm wedges, and composes the frame.

The glass is an even grid, so its centre is the point between pixels 99 and 100, `kNear` and `kFar`. A feature on the centre is a pair of pixels, which keeps own ship centred and the rings concentric with it.

### One blip, three ways up

Traffic is a blip, `ui/widgets/blip.*`: a diamond at our level, the same mark pointing up for an aircraft above and flipped for one below. One mark turned reads faster than three shapes.

The fill comes from `core/traffic/alarm.h`, so the glass and the annunciator always agree. `traffic::Level::None` is an outline. `Advisory`, inside `kAdvisoryDistanceM` and `kAdvisoryAltitudeM`, fills it. A fill never changes the mark's size.

Size carries the same window. Inside `kAdvisoryAltitudeM` of our altitude the blip is the near mark, 17 px across. Beyond it, where no advisory can rise, it is the far mark, 13 px, with a 2 px edge and one bar under the point instead of three and two. `kLevelM`, 60 m either side of us, is the deadband inside which the blip is a whole diamond, against ADS-L altitude noise. `ui::Vertical` names the five cases, `FarAbove` to `FarBelow`, plus `Unknown`, in one enum, so a far diamond cannot exist.

`Unknown` is a target that sends no altitude (`core/traffic/README.md`). It is a ring, which points nowhere and has no far size. It fills on an advisory, which for it is distance alone. It is 11 px across, so the caret 4 rows off the plot point clears it by a pixel. On the rim it ranks as level, because its height may be ours.

A caret above or below the blip marks a climb or a descent past 2.5 m/s. It is the bar drawn two pixels thick, 4 rows off the plot point at both sizes.

No figure stands beside the blip, because the shape, the size and the caret already say whether an aircraft is a factor, and a figure would clear glass the plot needs. `nearby` lists the figures.

The radar draws at most `kMaxRadarTargets`, twelve of the table's 48, in the order of `traffic::rank_by_relevance`, the order `nearby` lists in: every aircraft raising an alarm, then those inside `kAdvisoryAltitudeM` of our height, then the rest, nearest first in each. What is left off cannot raise an alarm, and the traffic count still counts it.

The page draws in two layers. The ground goes first: the ring, own ship, the footer readings and the state word, each on a patch of the reading plus `kLabelPad`, 2 px, on each side, or `kRangePad`, 4 px, for the range, which the ring meets from both sides. Traffic goes over it: the leaders, the symbols, the alarm sector, the formation square and the rim. A symbol clears no glass, so it covers only the strokes it lands on. A reading under an aircraft can be read once it moves, while an aircraft under a label would be lost.

There is no resolution advisory, because an RA has to be coordinated with the other aircraft and this device has no link for it. The wedge says where to look.

A leader runs from each symbol to where the aircraft will be in a minute, at its last reported speed and track, turned into the nose-up frame. A minute is the horizon a pilot can act inside. The leader stops at the ring. A target with no velocity, such as every ground-station relay, or one not moving, gets none.

Own ship gets a dashed line out of its nose with a 6 px ball at one minute and at two, so a turn shows: at 3 deg/s a minute is 60 degrees of curve. Where a target's leader meets a ball, the two aircraft meet. The line is 2 px wide on the 99|100 pair and dashed 3 px on and 3 off, so it never reads as a solid leader. It starts clear of the propeller and stops at the ring, and a ball past the ring is dropped, since one held at the edge would read as a wrong speed. Two balls give a rate, and a third would be a ruler.

The line shows only while an aircraft other than a formation member is inside the ring, because a scale with nothing to measure is ink a pilot learns to ignore. It is the one mark driven by own speed alone, so it can cost a partial refresh a second: the first ball steps a pixel every 0.65 kt per mile of range. There is none without a fix, without a ground speed, or with both balls inside the propeller.

Only a paired Bravo's corner is written across the top of the glass, so the sector ahead carries the plot alone. There is no compass rose, because 5x7 letters are unreadable at arm's length.

The flight time reads bottom-left at double height as `H:MM` from `flight::FlightTimer`, under `FLIGHT` while the clock runs, and `-:--` before a flight. It is the figure a pilot copies into a logbook. After a landing the seconds join it in the small font, because a logbook wants them, and they go at the next takeoff, since a slow panel would always show them wrong in flight.

### The wedge, and the word that ends it

From an advisory on, the 90-degree sector the aircraft lies in inverts once a second, from own ship out to the range ring. A quarter of the glass flipping is seen without being looked at, and it says where to turn the head. It starts with the advisory, because 3 km is about half a minute at a head-on closure and a search that starts later starts late.

It inverts rather than fills, so the threat and its leader read reversed out. It starts 15 px out, or at the formation square, which is drawn after it. It runs to the ring and stops under its stroke, so the ring never flashes gaps. The readings it reaches keep a rounded plaque: the range, the state word, and the flight clock once the hours widen it past the ring.

The rate is the panel's floor: `ScreenService` flips the phase on each frame it presents, at most once a `kPresentFloorMs`, one second. The SSD1681 could go faster, but every flip is a whole-frame partial refresh, and a large black region toggled faster ghosts for good.

Every advisory flashes its own sector on one shared phase, and overlaps invert once, because two aircraft are two places to look.

An advisory brings the radar back: a pilot on `sixpack`, `nearby`, the g-meter, the settings menu or the `bravo` page is put on the radar on that pass, once. Leaving it again under a standing alarm is the pilot's choice. The diagnostic pages and the `DIAGNOSTICS` menu that opens them are never taken, because they are read on the ground and a busy sky beside a club fleet would seal them for the whole session.

The long touch on the pad dismisses (`../input/README.md`). With an alarm standing it does nothing else: no page change and no wipe. It takes the sector, the sound and the lamp together for every aircraft already announced, and writes nothing in their place. The blip and its leader stay, because they describe the sky. An aircraft that leaves the window and returns, or one heard for the first time, takes its own sector, tone and lamp back on that pass.

### What is not a flight is said in the ring

`NO FIX`, `GROUND` and `TAXI` stand in the ring at double height, so a glance at an empty plot says whether the sky is empty or the page is blind. The word clears its patch, and aircraft are drawn over it. Only one shows: without a fix the ring says `NO FIX` and the clock keeps the figure it froze at. In flight the ring carries the plot alone and `FLIGHT` stands over the clock. A dismissal has no word, since the sector going out says it.

The ring has two slots, a double-height banner and a small line under it. The two are centred as a block between own ship's tail and the footer, or the range on the rim, and the banner keeps that place when the small line is empty. A line coming or going therefore never moves the banner, and the panel redraws only that line. A cell that needs charge takes the banner, `BAT 12%` from the knee down, because it is the one thing on the page a pilot can act on. Everything else moves down a slot, and the small line's word drops: a taxiing aircraft on a flat cell reads `BAT 4%` over `TAXI`, and a blind one `BAT 4%` over `NO FIX`. The level is `core/power`'s, `Low` and under (`core/power/README.md`), so a charger, which holds the level at `Normal`, clears it at once. The ring and the lamp start at the same knee.

A Go in cable mode that lost its cable on the ground counts down in the ring: `OFF IN` over `4 MIN`, one banner split over two lines at double height and one `line_pitch` apart, as the parked `CHARGE THE` over `BATTERY`. `OFF IN` stands where every banner stands, and `4 MIN` takes the small line's place (`../../../core/power/README.md`, Cable mode). It outranks the cell and the state word, because the unit goes off before either matters. The minutes are whole and rounded up, so the last one reads `1 MIN` and the panel refreshes once a minute. The countdown never stands in flight or during an update, since the hold runs only on the ground with no update under way, and it goes with the cable's return.

The ring speaks for the Go alone, and a paired Bravo speaks in its own corner. A banner too wide for the `ALL` ring's chord rises a pixel at a time until it clears, with the small line. Every current word fits, so the rise guards the next one.

Under `NO FIX` stands the receiver's stage (`core/gnss/README.md`), `GPS OFF`, `MOVE OUT` or `FINDS FIX`, each naming an action: a bench, a walk into the open, or patience. Its elapsed seconds stay on `status`, since they would refresh the panel every second.

`TAXI` is `ScreenService::taxiing`: a fix, the ground state of `core/flight/state.h`, and `state.flight.rolling` from `ownship`, which latches between 1.5 and 1.0 m/s (`core/flight/README.md`). So a glider pushed to the grid at a walk is taxiing, and the word holds through a slow turn. The device transmits, logs and locks as when parked, because ADS-L G.1.2 has two states and taxiing is ground. After a landing the word returns while the aircraft still rolls, because the landing hold ends a flight after ten seconds under 8 m/s.

The range stands on the bottom of the ring as its label, `4` at double height with `NM` small beside it, its patch keeping the stroke off the digits.

`PLOT` on the menu picks `ALL`, where every aircraft with a position is drawn, or `TO SCALE`, where every mark stands where the scale puts it. `ALL` ships first, because an aircraft left off is one a pilot was not told about. To scale, the plot uses the whole glass past the 92 px ring, about 4.3 NM abeam and 6.1 NM into a corner on the 4 NM ring, drawn over the footer readings, and `nearby` lists the rest.

### Bravo has its corner

A paired Go says what is wrong with its Bravo in the top-right corner, small, flush right, on a patch it clears, and nowhere else on the page. A live Bravo with nothing to report leaves the corner empty and shows a `B` in an 11 by 13 px frame over `TRAFFIC`, `kStackGap` above it, because the count holds its ADS-B traffic. Where `TO SCALE` shortens the word to `TFC`, the `B` stands over the digits, clear of the ring. A live Bravo on a low cell keeps its `B` and says the cell in the corner. Any other Bravo has no `B` and says `BRAVO` over the reason:

| Bravo | Reason |
|---|---|
| no `health` | `NO LINK` |
| `health` without UTC | `MOVE OUT`, the Go's own word before it has a date |
| its 1090 MHz receiver not listening | `RX DOWN` |
| live, its cell at `Low` or under | `BAT 9%`, or `BAT LOW` where `health` carries a level and no percentage |

The block is black on white while Bravo comes up by itself, and reversed once it is lost, after the plot so a mark over it reads white on black (`comms::lost`, `../../../core/comms/README.md`, What the Go makes of its Bravo). A Bravo is lost once it has been live since the Go booted or paired, once the Go has taxied, or when its receiver does not listen, and the tone sounds on the pass the block reverses. So a Tango switched on in the hangar clears a white block by itself, and a Bravo still not live at the taxi gets the black block and the tone on the ground. The corner reads health alone, so a quiet sky says nothing.

The cell shows because a Bravo in a flight bag shows its red lamp to nobody. Its level comes from `health` (`../../skyblip_bravo/README.md`).

### PLOT ALL: the rim

With `ALL` the ring shrinks from 92 px to 76, and an aircraft past it stands on a rim 87 px out at its bearing, with no distance drawn. 76 px leaves room for a near mark with its caret, eleven pixels either side of its point, between the ring and the glass's edge.

A rim mark is the blip it would be inside the ring, since a far aircraft calls for the same decision, taken earlier. Its leader is drawn to scale from where the aircraft is, inside the ring only, so a closing aircraft shows where it will enter. A rim mark that would touch the range label steps along the rim to clear it, on the aircraft's side, about 15 degrees off its bearing.

Two rim marks closer than 15 px, about ten degrees, are one mark. The aircraft are ranked, an advisory first, then the smallest vertical separation, then the nearest, and each joins the first mark already placed within 15 px or places its own. So a group wears its most urgent member's mark at that member's bearing, with a digit for the count, capped at 9. The digit goes along the rim on the side away from the centre line, under the mark when the mark is abeam. When that spot is within 4 px of another mark or on the range label, it takes the other side, then the spot straight out, and otherwise it is not drawn, because a mark outranks a count.

The ring is one circle, 2 px thick, since a second ring made a lone target read as near a ring rather than at a place, and a 1 px circle vanishes at arm's length. It is solved a row at a time, each row drawing the chord between radius `r` and `r - 2` as one `horizontal_line` per quadrant, because two nested midpoint circles leave specks on the diagonals. The chord is worked in half-pixels, `(2a+1)^2 + (2b+1)^2 <= (2r)^2`, since the ring is centred between pixels.

The ring is 4 NM out of the box, stored as the step a pilot picked, so the label is the setting with no rounding. Miles because the other instruments and the airspace use them, and four because a head-on conflict entering that ring is about two minutes away at this device's speeds.

The count on the right is all the traffic the device holds, a live Bravo's ADS-B included: the ring, the rim, ground-station relays and aircraft heard with no position. It leaves out formation members, since the square is their depiction, so a pilot flying with one partner reads `0`. It is the two `TFC` figures of `nearby` less the formation, while `status` shows the table's raw count. It is the biggest figure on the page, with `TRAFFIC` small beside it, traffic because a skydiver or a drone is not an aircraft. Where the word would touch the ring or the label, it shortens to `TFC`, ICAO's abbreviation.

The count reads `-` until it can claim a number: an SX1262 on the board, PPS locked so the dwells fall where the protocol puts them, and a position to plot against. Without them an empty sky and a dead radio look the same. One dash at the figure's size, so the footer does not shift when the first aircraft arrives.

The footer readings share one baseline, 4 px off the bottom of the glass, as the outer two are off its sides. `FLIGHT` stands over its clock, because both side by side are wider than the glass leaves.

## nearby

`nearby` is the radar as a list: the targets the device can place, most relevant first, under a row for the Go and one for a paired Bravo. The radar says where to look, and this page says who and how far above.

A unit row reads the unit's letter, its cell, the satellites its fix uses and the traffic its own radio holds: `GO 87% 11 SAT 9 TFC`. A pilot checks it before taxiing, and a Bravo in a bag has no other glass. The two `TFC` figures split the table by source, Bravo's being `traffic::TrafficTable::count_of(model::Source::Adsb)`, and together count every emitter, formation included, so a radio gone quiet shows its own `0`. The satellites are those in the solution, GGA's count, from `model::Ownship::satellites` and from `health` (`../../skyblip_bravo/README.md`, What Bravo serves), because satellites in view need GSV, which only the `satellites` page asks for. A Bravo built before a key reads `--` for it.

When a unit has something to say, a word replaces its satellites and traffic, flush right: the receiver's stage without a fix, `GPS OFF`, `MOVE OUT` or `FINDS FIX` (`core/gnss/README.md`), `TX OFF` for a Go with a fix that does not transmit (NO TX), and for Bravo `NO LINK`, `RX DOWN`, or its `gnss_stage`. A Bravo built before `gnss_stage` reads `MOVE OUT` without UTC, as on its radar corner. A lost Bravo's cell reads `--`, and a unit on its cable `CHG`. Nothing is reversed, because black means nothing on this page. The widest word, `FINDS FIX`, holds the stage words to nine characters.

The heads stand flush with the figures they name: `SRC ID` left, `DIST` and `ALT` right. No unit is printed, since the unit is a setting named on the menu. With nothing to list, `NO TRAFFIC` or `NO FIX` stands centred on the second row under the heads.

Each row has three double-height columns:

- The source letter (`model::Source`) and the whole 24-bit address, which a pilot reads back or types into a phone: `L 3FA21C` for ADS-L, `F` for ALP-TAS, `U` for a ground-station relay and `B` for Bravo's ADS-B. The hex alone is in the small font, `kNearbyAddressDrop` rows down, so an address never passes for a callsign such as `BEE123`. A callsign from the aircraft's own type 66 burst (`core/traffic/README.md`) replaces it, `L D-KXYZ`, cut at `kNearbyIdCharacters` so it never runs into the distance.
- The ground distance to a tenth, in the unit `settings::units` sets, because ground distance is what a pilot flies and what the radar and the alarm use. A tenth of a mile, 185 m, is what two receivers and an extrapolation support.
- The relative altitude in hundreds of feet with its sign: `+12` is 1200 ft above, level is `00`, and no altitude is `--`. Always two digits, so the column never shifts.

The order is `traffic::rank_by_relevance`, as on the radar. Seven rows fit under the two units, eight with no Bravo. Without a fix the page lists nothing, since every figure is measured from own ship.

A distance past 99.9 reads `FAR` and a separation past 9900 ft `+99`, because the three columns fill the glass exactly. ADS-B has no range gate (`core/traffic/README.md`), so an airliner from Bravo often reads both.

## radio_log

`radio_log` is the station log, newest at the top, `radio::Log::kMaxEntries` rows, and what scrolls off is gone. Each row is one burst:

```
34:56.462 RX M0 ADS-L  3FA21C -87   an ADS-L frame from 3FA21C
34:56.918 RX M1 FLARM  4C11A0 -93   an ALP-TAS frame, the other channel
34:56.085 RX M1 NAMED  3FA21C -87   that aircraft's own registration
34:56.240 RX O  UPLINK        -74   a ground station's relay, which names no one sender
34:55.107 RX M0 DEC    ED4838 -101  a frame that passed its check and was refused
34:55.482 RX M1 TYPE   4C11A0 -62   a message type this firmware does not read
34:55.913 RX M0 WAIT          -58   no fix of our own yet, so nothing was tried
34:55.221 RX M0 KEY+18 ED4838 -18   its sender keyed 18 s away from our second
34:55.033 RX M1 SYNC          -97   framed as neither system: not ours to decode
34:55.694 TX M0        AIR          own-ship's burst left the antenna
34:55.165 TX M1        CALLSIGN     and this one carried its callsign
34:53.881 TX M1 LOST   GROUND       armed, and the radio never reported it sent
```

The columns are the stamp, the direction, the dwell's channel, the verdict, the emitter's address and the level. The verdict column is six characters, set by `KEY+18`, so a good reception spells the system it framed as. A good transmission, like a good reception, prints no verdict, so the rows that went wrong stand out. In the address column a transmission prints its schedule, `GROUND` at 0.1 Hz or `AIR` at 1 Hz (§G.1.16), which nothing else shows, or `CALLSIGN` or `STATUS` (§G.2), since a Status leaves in the same second as a ground position.

`DEC` is a burst that framed, passed its protocol's check and was refused anyway. It separates an empty sky from a receiver that frames nothing, and it is the one to take to `core/protocol/`. Band noise that framed is only the `NOISE` counter on the title line (`core/radio/README.md`). The other refusals are no decoding fault:

- `WAIT`: an ALP-TAS position is coded against the receiver and keyed on the UTC second, so nothing is tried before our fix and date.
- `TYPE`: a message type this firmware does not implement, as from a SoftRF mixing Air V6 with Air V7.
- `SYNC`: a sync tail that names neither system.
- `KEY`: the CRC held, and the sender keyed on a second the given offset away from ours, so its clock is wrong.

A row past the CRC names its emitter whatever the verdict, because the ALP-TAS address word is in the clear and under the CRC. `WAIT` and `SYNC` read no frame, so they name none.

The stamp is UTC as `mm:ss`, with `.mmm` when the PPS phase is known, or `T+<seconds>` since boot before the receiver gives a second, so a reading is never taken for a wall clock.

The second line explains the stamps. `PPS LOCK`, `PPS HOLD 12` and `PPS NONE` say whether a phase exists. `HOLD` is `timing::in_pps_holdover`, the slot map's own test: the dwells keep their places on the held edge and nothing transmits (`../../../core/timing/README.md`, The PPS holdover keeps the dwells, receive only). `NONE` follows once `kPpsHoldoverMs` runs out. `BAND` is the noise floor the dwells measured, because a device deaf at -85 dBm has a full band and one deaf at -110 dBm has a fault.

## bravo_log

`bravo_log` is where a pilot reads a paired Bravo, which has no glass, from the `BRAVO LOG` row of the `DIAGNOSTICS` menu. It is `radio_log` for the frames Bravo forwards, under a three-line head.

1. The tag, `BRAVO 3F2A`, and on the right the Go's verdict: `LIVE`, `WAIT`, `LINK DOWN`, `RECEIVER DOWN`, `NO UTC`, or `NOT PAIRED` on a Go with no pin.
2. The image Bravo runs, `FW 0.4.1+1702`, and its cell: `BAT 80%`, `BAT CHG` on the cable or `BAT --` with no reading.
3. Its 1090 MHz receiver, `1090 412/S` for the frames handed up in the last second or `1090 OFF`, and on the right `B 14 IN 1234 OUT 2`: the ADS-B aircraft in the table, the frames folded since boot, and the frames refused or unplaced, once there are any.

The readings come from Bravo's last fresh `health` (`../../skyblip_bravo/README.md`), so a lost Bravo keeps its state word and loses the figures.

Each row is one observation frame, newest at the top, `BravoLog::kMaxEntries` of them. The stamp is when it reached the Go, UTC as `hh:mm:ss` or `T+` seconds since boot. The verdict is `ADS-B` for a frame folded into the table, `BAD` for one `comms::read_observation()` refused, and `TIME` for one the Go could not place: no UTC of its own, or dated past `BravoService::kAheadCeilingMs`. Then the address, and the callsign when the frame named one, else its flight level. A refused frame prints no address.

## sixpack

Six dials over the GNSS own-ship state, each with its number: ground speed, the cell, the altimeter, the turn coordinator, the track and the vertical speed. The faces are a block, 66 px between centres, so 3 px of glass separates every pair. Each row's numbers stack off its outer edge at double size, with the label small beside, and nothing stands between the rows. The speed dial alone follows `settings::units`, by the rule at the top of this file.

The top middle face is a horizon. It pitches with the flight-path angle, vertical speed over ground speed, and rolls with the bank a coordinated turn implies, `omega * V / g`. No sensor measures a bank and the hub's gyroscope is never read (`core/flight/README.md`), so the page never calls it an attitude indicator. Its wing bars reach 0.65 of the face radius with a 5 px dot, larger than the instrument's 0.22 to 0.49 (`Attitude indicator level flight.svg`, Wikimedia Commons), because smaller read as dashes and a speck.

Its number is the cell, `BATTERY` over `64%`, because a pilot reads figures here and finds the cell otherwise only on a bench page or as the lamp's colour (`core/indication/README.md`). The sign sits against the digits, here and on `status`, since a space at double height splits a reading. `--%` is a unit with no divider, no reading yet, or a reading outside `power::plausible_mv`. On the cable the dial reads `CHARGE V` and the divider to a hundredth, `4.80`, because the divider then reads the USB rail, 4.7 to 4.8 V whatever the cell holds (`core/power/README.md`), and a rail must never read as the cell.

The flight clock and the state words live on the radar. The altimeter reads GNSS altitude, with no subscale, and pressure altitude is on `status`.

### The two scales that are not linear

The speed dial hangs zero straight down and sweeps clockwise up the left at 1.8 degrees per knot to 175 kt, so 100 kt points straight up. Eight marks on the 45-degree rays, the only angles drawn as exact lines, make 25 kt intervals. In metric the same arc is 315 km/h, `kAsiFullScaleKmh`, in 45 km/h intervals, so the needle keeps its place across units. Zero hangs at the bottom so the unreachable arc takes the bottom right and the top carries the scale.

The vertical speed dial puts +1000 fpm straight up and -1000 straight down, the rates of a climb-out and a circuit descent. The first thousand takes 90 degrees and the second 80. Marks stand every 500 fpm, long at the thousands.

Both dials checker the arc their scale cannot reach: past full scale on the speed dial, and 20 degrees about the horizontal on the vario. The checker means one thing: the scale does not go here. It is a 7 px band inside the ring, with no outline, since a slice to the hub would read as a needle. The fill and the marks share the cordic angles, so the shading ends where the scale does. 175 kt puts both ends of the speed dial's band on exact rays. Each sector derives from its full scale, so moving one moves its shading. The needle clears a white channel through the checker, where a stopped aircraft parks it.

### With no fix

Every needle parks at rest, since a dial with no needle reads as broken, and every number reads `---`, since `0` would claim a stationary aircraft at sea level heading north. The vario keeps reading through an outage while a barometer feeds it, `ScreenService::climb_measured`, and the cell keeps reading, being no GNSS figure. A track due north reads `360`.

The vertical speed figure moves in steps of `kClimbFigureStepFpm`, 10 ft/min, because the damped rate wanders a few feet a minute on a bench. Ten is under half the 24.6 ft/min ADS-L resolves, so the figure still shows more than the wire carries. Only the figure is rounded. Zero prints without a sign.

The turn coordinator's symbol is an aeroplane from behind. The face has four marks, two at the horizontal and two 20 degrees below, where the wings sit in a standard-rate turn of 3 deg/s, as on the real instrument (`Turn coordinator - coordinated.svg`, Wikimedia Commons). The symbol leans 20 degrees per 3 deg/s, hard over at 45, with the turn rate and not the bank, so a turn sits on the same mark at any airspeed. The title, `TURN D/S`, gives the unit.

The rate is the GNSS track differentiated once a second (`core/flight/turn.h`), so it steps by about 0.7 deg/s, a cordic9 step, and arrives a second late. The gyroscope that would do better costs most of a milliamp (`core/flight/README.md`). The symbol, the vario and their numbers read `state.indicated`, damped by `kIndicatedRateDampingMs`, and the horizon is worked from the same pair, so the three dials agree.

Under the symbol a 9 px ball slides 16 px either way in a cage, hitting its stop at 0.2 g across the wings, since at 18 px it touched the ring. What it measures is in `core/flight/README.md`: it answers the rudder, while the symbol answers the rate. The cage shows only on a device with an inclinometer, so a plain T-Echo's lower half stays empty. The Plus's BHI260AP takes 101 KB of firmware over I2C at every boot, paced against the loop (`hardware/parts/bhi260/`), so its ball arrives about four seconds after the cage. A hub that refuses its image leaves the cage empty, since a centred ball would claim every turn coordinated.

The card reads `TRK`, GNSS course over ground from true north, not `HDG`: there is no magnetometer and no variation model.

## status

`status` is the bench's page, so every reading is the raw measurement, never the damped, median or rounded figure the other pages show, and a figure that moves was moved by the sensor. Each prints once to the resolution it is held in, in the SI column: the track in hundredths of a degree, pressure altitude in centimetres, GNSS altitude in tenths of a metre, speed in hundredths of a km/h, climb in millimetres a second, the cell in millivolts. The aeronautical column converts the same reading to whole feet, knots and feet a minute. The cell's percentage is the one derived figure, and on the cable the row reads `-- V --% CHG`, since the divider reads the USB rail there.

The identity line pairs the address at double height with the callsign, small, at its right, because the pair is what another aircraft sees on its `nearby`.

The receiver row reads `GNSS 3D 9 SAT` with a fix, `2D` or `3D` from GSA field 2, or from the four satellites altitude needs when GSA gives none. Without a fix it reads the stage and its age, `GNSS MOVE OUT 0:48` (`core/gnss/README.md`), stopping at `9:59`, the room the row has. The count is satellites used, so it goes with the fix.

The barometer row prints the BME280's pressure to the tenth of a pascal, `1013.252 hPa`, about nine centimetres of altitude, so a bench sees the sensor breathe. `STD` is pressure altitude on 1013.25 hPa and `GNSS` the geometric altitude. The vertical speed is in millimetres a second, not the 0.125 m/s the radio sends (`core/flight/README.md`).

The right of the track row reports the sensor hub: `IMU RUN -120mg` is a hub that booted and a ball 120 thousandths of a g to the left. The word is its stage (`hardware/parts/bhi260/README.md`), and the reading shows only while a sample is fresh. Without one, the field says what the hub handed over, because a hub up and silent is a fault the stage cannot show:

| Field | Meaning |
|---|---|
| `B0` | the FIFO is always empty though the configuration was accepted |
| `B99` | bytes arrive with no acceleration among them, capped |
| `U3` | three events the parser could not step over |
| `E1A` | the hub's error register, 0x2E, is not zero |
| `M12` | the last meta event, shown unless it is `Initialized`, `M0` included |
| `I18` | `INT_STATUS`: `I00` nothing produced, `I18` data in the non-wakeup FIFO, `I03` in the wakeup one |
| `SE4:23` | meta event 11, a sensor error: virtual sensor 4, the accelerometer, code 0x23, `Sensor Init Failed: Unknown` in Bosch's list. It replaces the byte count |

A hub that stopped names the stage and why: `IMU BOOT VERIFY` is an image refused, `IMU LOAD DOWN` a bus that quit mid-upload, `IMU HIF TIMEOUT` a part never ready, `IMU CONF NOSENS` an image with no accelerometer and `IMU CONF NOCFG` a configuration read back as nothing. The field is 16 cells at its widest, what the row leaves past `TRUE`.

The traffic row ends with `TX ON` or `TX OFF`, from `timing::TransmitGate::on_air()`: the verdict every burst is sent by, with no position held past 5 s, so the glass never disagrees with the radio. `TX OFF` names the first requirement unmet now, `RADIO`, `GNSS`, `FIX`, `UTC`, `PPS` or `HELD` (`../../../core/timing/README.md`, The transmit gate), at once, without the 10 s the `NO TX` mark waits. `TX OFF` alone is the settle after acquisition (`core/gnss/README.md`).

## self_test

`self_test` names the part that did not answer, so a device that refuses to fly says why instead of going dark. It is drawn from what the board probed at bring-up and stays on the glass while a required part is missing. A device that flies reaches it only from `SELF TEST`, the last row of the `DIAGNOSTICS` menu.

The header shows `ID 00012A`, the 24-bit address of the unit's serial, or `ID ------` before activation, and on the right the reset reason (`core/power/reset_reason.h`), read once at boot so the companion app reports the same word. `WATCHDOG`, `CPU LOCKUP`, `BROWNOUT`, `SOFT RESET`, `RESET PIN` and `DEBUGGER` are worth a bug report. The `ID` stays the serial even under an aircraft address, because the bench and skyblip.eu know a unit by it.

One row per part reads what it is, which part answered and how:

| Row | The part behind it |
|---|---|
| `RADIO` | the SX1262 transceiver, by a register round-trip: no radio, no flight |
| `GNSS` | the L76K receiver on its UART: no receiver, no flight |
| `BARO` | the BME280, with the address that answered, ours at 0x76 and LilyGO's at 0x77: no barometer, no flight and no activation |
| `PANEL` | the e-paper, by the lot its ID names (`hardware/README.md`) |
| `IMU` | the BHI260AP sensor hub, which feeds the ball |
| `TEMP` | the nRF52840's die sensor, which the thermal charge limits read |
| `BATTERY` | the resistor divider into the SAADC |
| `CONTACTS` | the button on P1.10, also the wake pin, and the pad on P0.11 |
| `BUZZER` | the piezo, found by its drive stage holding P0.06 down |
| `HAPTIC` | the DRV2605 at 0x5A, or `PIN` for a motor driven off a pin |
| `LAMP` | the RGB status LEDs, the only sign of life with the glass parked |
| `LINK` | Bluetooth LE |
| `STORAGE` | the settings in internal NVS and the flight log on the external SPI NOR, with `DEFAULTS` when a stored settings blob could not be read (`../README.md`) |
| `DFU` | MCUboot, which writes firmware updates |

`RADIO`, `GNSS` and `BARO` are `kRequired`, the parts the product cannot fly without, so they come first and only they can read `FAIL`. `PASS` is a part that answered, and `NOT FOUND` an empty footprint, a real answer on a plain T-Echo with no buzzer and no haptic. The middle column names the part looked for, or what answered instead.

`CONTACTS` proves least: a released switch reads like an empty footprint, so the row checks only that both lines are declared and their GPIO controller is up, as `LAMP` does, and prints the pins for a probe. Hold the button and the power-off count starts, tap the pad and the page changes: that is the test.

The bus row, `I2C IMU RTC HAPTIC BARO`, names every address that answered on the sensor bus, ascending, from `boards/lilygo/t_echo_plus/i2c_scan.h`, and prints an unknown one as hex, which is why the bus is scanned rather than probed. The scan costs about 20 ms, so the board runs it the first time a menu opens, and at boot only on a unit that refuses to fly. The RTC appears only here, because nothing drives it and a row would claim a `PASS`.

The fifteen rows set the pitch at 9 px. A paired Go adds `BRAVO 3F2A` and its verdict, `WAIT`, `ON`, `LINK DOWN`, `RECEIVER DOWN` or `NO UTC`, as a sixteenth. While neither the Go nor its Bravo has an aircraft address and Bravo is neither lost nor waiting, that row reads `ADS-B` and `UNSET` instead, without claiming the transponder shows as traffic, since the Go cannot know it has ADS-B Out. The sixteenth row shares the footer's line, ending at 198 px.

The radio and receiver rows add how the part works now, because the transmit gate reads both (`../../../core/timing/README.md`, The transmit gate). `RADIO SX1262 CCA OK` enters its dwells and reads its channel before a burst, and `NO CCA` stopped. `GNSS L76K 2 HZ` solves twice a second on 115200 with a fix, UTC and an edge. Otherwise the word is the first requirement unmet: the bring-up stage, `1 HZ` at the factory rate, `STAMP` for a solution off `.000` and `.500`, `NO FIX`, `NO UTC` or `NO PPS`. While the `NO TX` mark names that requirement, the verdict reads `NO TX`. The boot verdict under the divider stays.

Under the divider stand the cell in volts to two decimals and the verdict, `PASS` or `FAIL`, inverted so it never reads as a row.

## NO TX

While the Go does not transmit ADS-L for a fault of its own, every page says so in its top-left corner: `NO TX` over the reason, small, reversed in a patch it clears, mirroring the radar's Bravo corner. The reason is the first requirement of the transmit gate unmet: `RADIO`, `GNSS`, `PPS`, `HELD` or `ADDRESS` (`../../../core/timing/README.md`, The transmit gate). A missing fix or UTC is no fault of the unit, and the radar's `NO FIX` and the nearby Go row already say it, so it clears the mark and is never named. It is drawn last, over everything, because it is the one thing a pilot must read. Menus, prompts and notices do not carry it, since they stand for seconds.

The bursts stop on the pass a requirement fails, `HELD` aside, which names positions held while the bursts keep trying, but the mark waits until the failure has lasted 10 s, so a reconfigured receiver or a skipped pulse does not flicker it. It stays until every requirement has held 10 s, while the bursts resume at once. The radio is judged from boot. `GNSS`, `PPS` and `HELD` wait 10 s after the first fix, and their 10 s restart when a lost fix returns, so a unit acquiring on the apron shows its stage, not a fault. The Go shakes once, `indication::kSignNoTx`, when the mark and flight first stand together, and not again while the mark stays.

`ADDRESS` is drawn for completeness, since an unactivated unit shows its activation page instead. The phone reads the same reason from `status` as `no_tx` (`../../../core/comms/README.md`).

Words on the glass are spelled in full. Acronyms such as `GNSS`, `UTC`, `PPS` and `TX` are fine, `TX` being a radio term (Trig TY91 manual: "The TX icon shows that the radio is transmitting"). An abbreviation is used only where the word does not fit, as `TFC`. The widest reason, `ADDRESS`, is seven characters, and the patch widens for it.

## The frame every asking page is set in

Every page that asks the pilot something, or tells them something they did not open, is set in `frame.h`: a heading, a body and two keys along the foot. The flight and bench pages ask nothing and are not.

The heading is the mark from the logo (`ui/widgets/mark.h`) left of the title, centred as one group over a short rule. Its size is a fact: small on a page the pilot opened, such as a menu, a field or the `bravo` page, and large on a page the device pushed, such as a prompt, a notice, the installation, the recovery or the activation. A pilot who pressed nothing sees the large mark and knows the device is asking. The mark's ink box is centred on the height of the capitals.

The keys say what each contact does on this page. The button's key is always left and filled black, the pad's always right and white, so a contact reads by shape and place first. A contact that does nothing has no key, and its half stays empty rather than the other key moving. A page with no key asks for nothing.

The body is centred between the heading's rule and the keys, or the foot of the glass with no key, or a foot line such as a countdown. A list starts under the rule, since it is read from the top. Lines keep an 18 px pitch and a 10 px paragraph gap (`draw_body`).

Every question the glass asks counts down and stands 30 s, `kAskCeilingMs`, the phone's prompt included (`common::kPromptCeilingMs`). The upload window behind `DOWNLOAD` is not a question and keeps its ten minutes.

Words are at double height, sixteen to a line, and the value a pilot checks is larger: a callsign or a name at triple height, a tag at four times. Nothing in the frame is small, because a line too small to read at arm's length is never read. The words follow ASD-STE100 Simplified Technical English: one instruction to a sentence, approved verbs only, PUSH and not press, and a comma after a condition. No line ends with a full stop, as on a cockpit legend, so a second sentence starts its own paragraph. The briefing's canvas keeps full stops in its label for screen readers.

## activation

A unit skyblip.eu has not activated neither transmits nor receives, and its glass shows this page in place of every page, a prompt and an incoming image aside: `NOT ACTIVATED` under the large mark, `GO TO SKYBLIP.EU` over `/ACTIVATE` and `THEN SELECT:`, and the four hex digits of the Bluetooth tag at four times the type size. The chooser lists `activate 3F2A skyBlip`. The tag is the low 16 bits of the advertised address, so a pilot with three units picks the right row. The page goes once the unlock lands, through a wipe (`core/identity/README.md`). It takes no gesture, because no page or menu lies behind it, and the power-off hold still works. `SELF TEST` shows the address as it is now, so an unlock shows there without a reboot.

The server's register asks on the confirm page, titled `ACTIVATE`: `SEND THE KEY OF THIS UNIT TO SKYBLIP.EU`, with no apostrophe, because the glass font has none.

## confirm

A phone's prompt is titled with the operation under the large mark, `POWER OFF` or `FIRMWARE`, with what it will do under it in at most three lines and `CANCELS IN 27 S` counting down above the keys. The button's key reads `ACCEPT`, and one press allows. The pad's reads `CANCEL`, and a tap refuses. Every question on the glass splits this way: the button does and the pad declines, as on `PAIR BRAVO`, `UNPAIR BRAVO` and the aircraft-address clear. The fill names the contact, not the answer the device prefers.

A tap counts only once the button's answer is armed, so a tap made while paging refuses nothing. A `set`, a `dfu`, a `register` or a `pair` from a browser that proved a key the unit does not hold adds `TRUSTS BROWSER` under the detail, because the same press stores that key (`../../../core/comms/README.md`, Trusted browsers). The countdown stays pinned above the keys, and the detail is centred between the rule and the countdown (`draw_body_above`, `draw_foot_line`).

After an accepted `FIRMWARE` the page reads `DOWNLOAD`, with the pad's key `CANCEL`, which closes the upload window. The button does nothing there, so the thumb that accepted cannot cancel it.

## menu

The button opens the menu of the page a pilot stands on, so a pilot with no phone can change what matters. Two pages have one, and one file draws both.

| Menu | Rows |
|---|---|
| `radar` | `SETTINGS`: `LIGHT`, `AIRCRAFT`, `UNITS`, `RANGE`, `PLOT`, `ALARM`, `BRAVO`, `POWER` |
| `AIRCRAFT` row | `AIRCRAFT`: what Bravo heard, `TYPE`, `CALLSIGN`, `ADS-B` |
| `nearby` | `DIAGNOSTICS`: `STATUS`, `SATELLITES`, `RADIO LOG`, `BRAVO LOG`, `SELF TEST` |

The headings say what the rows are, not the page behind them, which the pilot knows.

Rows stand at double height, `kMenuRowHeight` apart, so `kMenuLines`, seven, fit between the rule and the keys. A longer menu slides up under the focus, a line per tap past the seventh. The button's key reads `CHANGE`, or `OPEN` on a row that opens something. The pad's reads `NEXT`, or `BACK` on the last row.

The radar's menu holds every setting changed on the glass, and the front light, because the first screen is where a device is set up. A reading such as the device address is no row.

- `LIGHT` comes first, reached for in the dark: two presses from the radar light it, and the press also gives the page back. `ScreenService` holds it and never stores it, so every boot starts dark and a light left on in daylight costs no next flight (`scripts/power_budget.py` models it at 20 mA, not yet metered).
- `AIRCRAFT` is second, because a new device needs it before its first flight. It shows six characters of the callsign, or `NONE`, and opens the `aircraft` menu.
- `UNITS`, `RANGE` and `PLOT` follow, the three the radar is made of.
- `RANGE` steps 1, 2, 4 and 8 NM, or 2, 4, 8 and 16 KM, and round. Each table doubles in whole units of its own rather than converting the other, and the step survives a change of unit.
- `ALARM` steps `LOUD`, `SOFT`, `VISUAL`, `OFF` and round, so one stray press from the default still sounds. A Go with no buzzer and no haptic toggles `VISUAL` and `OFF`, and while paired steps `BRAVO LOUD`, `BRAVO SOFT`, `VISUAL` and `OFF`, the `BRAVO` steps sounding on Bravo alone. Pairing sets `BRAVO LOUD`, and unpairing turns a `BRAVO` step into `VISUAL`. While paired the row sets the alarm for both units (`../../../core/comms/README.md`, The alarm).
- `BRAVO` follows, since a Bravo is paired once, on the ground. It shows the tag or `NONE` and opens the `bravo` page.
- `POWER` is last, the one row the menu slides up to, because it is set once per aircraft. `BUTTON`, the default, keeps the button as the switch. `CABLE` hands it to USB power, so a Go wired to the aircraft's socket follows the master switch (`../../../core/power/README.md`, Cable mode).

`TYPE` holds ADS-L G.1.3's codes 0 to 10 and 15 to 17, the categories a person flies in. The longest name, `HANG GLIDER`, fits the row with its label, and a code with no name prints as `CODE 12`.

The `DIAGNOSTICS` rows open pages, in the order they are reached for: `STATUS` on the apron, `SATELLITES` when it says no fix, the logs on the bench, `SELF TEST` for a bug report. A tap on such a page returns to the page whose menu opened it (`menu_owner`).

`sixpack` and the g-meter have no menu, since `enter_menu` opens no empty menu. A plain T-Echo has no inertial sensor, so `next_page` skips the g-meter on its walk.

The pad moves the focus down and the button acts, each further press stepping the same field. Nothing depends on timing, so the long press that switches off never changes a setting. A pilot cannot get stuck: a tap past the last row returns to the page, a long touch to the radar, and `kIdleReturnHoldMs` untouched gives the picture back.

## aircraft

The `AIRCRAFT` menu holds the settings that belong to the aircraft rather than the pilot, `TYPE`, `CALLSIGN` and `ADS-B`, under a four-line block of what a paired Bravo heard, so one press sets all three.

Bravo names the transponder it hears loudest by a clear lead (`../../skyblip_bravo/README.md`, Own transponder), and its identification gives the callsign and the type. ADS-B has no dash, so `adsb::registration` (`../../../protocols/adsb/registration.h`) restores it where the address block ends the nationality mark: `FJABC` from France's block is `F-JABC`. The block reads the type, or `SAME` when the rows already match, the callsign at triple height, or the address when no flight ID came, and `ADS-B 3A4B5C`, the order of the rows, the notice and the Briefing page. The button's key reads `SET`, or nothing when a press would change nothing.

A press stores the address as `aircraft_address`, the callsign when one was heard and the type when it has a name, and returns to `AIRCRAFT` in the settings menu. A new address with no callsign clears the stored callsign, so another aircraft's name never goes on air beside it. The press stores what the glass showed, and only while Bravo still hears exactly that, so a proposal that changed since the last redraw stores nothing. The block is its own confirmation, at the size a registration is checked against.

Bravo proposes on the ground only, `flight.confirmed_state` reading ground, because in the air a wingman 10 m off is as loud as our own transponder. In flight the block reads `IN FLIGHT` and `BRAVO FINDS IT ON THE GROUND`, and a Go with no fix reads `NO FIX`, since after a brownout in the air it knows no better. The other states say what to do: `NOT PAIRED` with `PAIR A BRAVO FIRST`, where the press opens the `bravo` page, `NOT CONNECTED`, and `NOT HEARD` with `SET THE TRANSPONDER TO ALT`, because a transponder in `STBY` sends nothing and a pilot's guide asks for `ALT` on the ground. Bravo decodes extended squitters only, so a transponder without ADS-B Out is never heard.

The pad walks the block and the rows, and past `ADS-B` returns to `AIRCRAFT`. The rows open the fields below, so a unit with no Bravo still types. `ADS-B` shows the stored address or `NONE` (`../README.md`, Aircraft address).

A Bravo that hears a transponder the Go does not hold says so once a boot over whatever page stands: `BRAVO HEARS` under the large mark, the callsign at triple height, the address and the type, with `SET` and `CLOSE`. It catches a Go moved to another aircraft with the old address, which would hide that aircraft from the radar. The button stores what the notice shows, but only once the notice is on the glass and the thumb has been up for `PromptAnswer::kAnswerSettleMs`, the prompt rule. Every other way out stores nothing: the pad, the countdown from `CANCELS IN 30 S`, or Bravo no longer hearing it, a takeoff included. It never opens over a menu. No other aircraft-address notice exists, so a unit with no address stays quiet until Bravo hears a transponder, at the holding point at the latest.

## bravo

The `bravo` page, opened from the `BRAVO` row, pairs a Go with a Bravo (`../../../core/comms/README.md`, "The Go's link to its Bravo"). It is the one page off the walk the settings menu opens, so the pad returns it to the radar and an alarm takes it like a menu. Leaving it by any way stops the scan and drops a pairing under way.

Unpaired, the page scans and lists each Bravo heard as `BRAVO 3F2A`, or `NO BRAVO FOUND`. The tag is all it names, because it is all that tells two Bravos in a hangar apart. The button asks `PAIR BRAVO`, with the tag at four times the type size and `PAIR` and `CANCEL`, and a second press pairs.

While pairing, the page reads `PAIRING` over the tag, then `WAIT`, then `PUSH THE BUTTON ON BRAVO`, and the pad's key cancels. It never idles out, since the pairing has its own ceilings. The wait for Bravo's button counts down `BravoService::kAnswerCeilingMs`, the 30 s Bravo's request stands on its lamp. A refusal reads `NOT PAIRED`, why and what to do: `3 PHONES CONNECTED`, `LINK TOO SMALL`, `NO DATA FROM BRAVO`, `NO LINK`, `UNKNOWN PROTOCOL` or `NOT FOUND`. The button's `PAIR` retries, except with three phones connected, which a retry would meet again.

Paired, the page reads the tag and `CONNECTED`, `NOT CONNECTED`, `LINK TOO SMALL` or `UPDATE NECESSARY`. The button asks `UNPAIR BRAVO`, with `UNPAIR` and `KEEP`, and a second press unpairs and lists the Bravos in range, so a replacement pairs on the same page once the Go has told the old one, at most `BravoService::kFarewellCeilingMs`.

A browser can start a pairing by the tag (`../../../core/comms/README.md`, Pairing from a browser). The page then opens by itself over any page, with no list, and leaving it tells the browser `cancelled`. A browser the Go does not trust asks first, on `PAIR BRAVO` over `PAIR WITH BRAVO 3F2A`.

A paired Go whose Bravo holds no pin for it asks again (`../../../core/comms/README.md`, The Go's link to its Bravo), and says so once over any page but an alarm: `BRAVO LOST THIS GO` and `PUSH THE BUTTON ON BRAVO`, counting down `BravoService::kAnswerCeilingMs`, with `CLOSE` on both keys. Closing leaves the request on Bravo's lamp. With no answer, `BRAVO IS NOT PAIRED` follows.

## The callsign field

`CALLSIGN` opens the one screen with a cursor, because nine characters from thirty-eight make a field, not a row. The gestures swap here, and the keys say so: the pad, `CHANGE`, rolls the character over the bar, because it is the contact a thumb repeats freely, and the button, `NEXT`, steps to the next character, then `SAVE` on the last, because a field walked off its end is finished. The ring rolls blank, dash, `A` to `Z`, `0` to `9` and blank again, so a thumb past `Z` comes back with the same gesture. Blank costs nothing to leave, and the dash comes next because every registration here carries one second: `F-JABC`, `D-KXYZ`.

The stored name drops trailing blanks and keeps one in the middle. A press on a blank first character removes the name, and the button's key turns to `ERASE` while that is armed, so the gesture shows. No registration starts with a blank.

`ADS-B` opens the same field six wide, for a hex address, rolling blank, `0` to `9`, `A` to `F` and blank. The last press stores only six typed digits that are not `000000`, the unset value, or `FFFFFF`, which ICAO assigns to no aircraft. While a paired Bravo names the aircraft's transponder, the field shows `BRAVO HEARS` over that address, and any other is refused: the field stays open, the bar returns to the first digit, and `NOT ACCEPTED` stands until the pad rolls a character. ADS-L F.2.3 asks for an inconsistent configuration to be disallowed (`../../../core/settings/README.md`, The ICAO address). A clear is never refused.

Clearing the address takes one more press, because without it our own transponder can show as traffic and alarm: a second screen shows the address, `YOUR TRANSPONDER SHOWS AS TRAFFIC`, `ERASE` and `KEEP`, and only the button clears. The pad, the long touch and the `kAskCeilingMs` countdown keep it.

The field is at triple height, the largest type on any page, because it is checked character by character against a fuselage. Leaving it unfinished, by the long touch or `kIdleReturnHoldMs` untouched, stores nothing.

## gmeter

The g-meter shows the accelerometer with nothing inferred, so only its calibration can be wrong. It needs no fix and runs no filter.

The field holds the two axes a wing works against, drawn as a seat: the marker sits where the load throws the pilot. A pull sinks it, a push raises it and a skid slides it to the ball's side, so +4 g is at the bottom and -2 g at the top. Normal load is 16 px a g about the 1 g of level flight at the centre, and lateral load a g to each edge. The box around the marker has the four peaks as its edges.

Fore and aft stands on its own strip beside the field, because it is no wing load, and a third direction in the field would invite a meaningless diagonal. The top is the nose: the bar rises under braking and falls under power, labelled `DEC` and `ACC`.

Three rows give each axis in tenths of a g: the reading now at double size, and the least and most beside it, small. `G` is signed, as load factor always is. `SIDE` names a hand in the ball's sense, `R0.4` and `L0.6`, so the marker and the six-pack's ball lean together. `ACCEL` names the event, `A0.3` accelerating and `D0.4` braking. Zero carries no sign or hand: `0.0`. The least stands left of the most, `-1.3` left of `+4.2`, `L0.6` left of `R0.7`, `D0.4` left of `A0.3`, matching the field and the strip.

The rows avoid flight-test names, since `LAT` and `LON` would read as latitude and longitude. The peaks reset when the flight timer starts (`core/flight/gload.h`).

With no inertial sensor the page says `NO SENSOR` and draws no scale. A hub that has not reported yet shows the scale, an empty field and dashes.

## sats

`satellites` explains on the apron why a device on for two minutes still does not transmit. One bar per satellite in view, grouped by constellation and labelled underneath, stands as high as its C/N0 against a dotted line at 40 dB-Hz, good reception. A bar is filled when GSA put the satellite in the solution and hollow when it is only in view. A satellite with no level is a foot on the baseline, since the receiver knows where to look and has heard nothing yet.

GSV is asked for as the page opens and given up as it closes (`core/gnss/README.md`), so the bars arrive a second after the page and no other page pays for them. The levels run for as long as the page is up, fix or no fix, because unmeasured bars would be a lie. Before the first set the page says `WAIT FOR THE LEVELS` over what GSA and GGA carry anyway: the satellites solved, their split across constellations, and the two DOPs. A receiver that heard nothing says `NO SATELLITE HEARD`.

Under the DOPs stand two figures from the receiver's wire. `NAV` is how long after its quarter second the last solution landed. The receiver solves every 500 ms and the clock trusts no second that lands over 250 ms after its edge, so `NAV` past 250 says which transmissions are lost (`core/gnss/README.md`). With no PPS edge there is no figure, never a zero. `BAUD` is the port's final rate, with the driver's word for how far configuration got, and an overrun count once the port has dropped a byte.

## raw and capture, retired

The `raw` and `capture` pages went once Bluetooth could do both: the diagnostics dump answers every counter `raw` printed, and its capture group carries what `capture` showed (`../../../core/comms/README.md`, The capture group). A page arms a capture with the `capture` command, and the confirm page asks (`../README.md`, Which capture is armed). Their codes, 7 and 8, are never reused, because a capture records the page on the glass by its code.

## The others

| Page | What it answers |
|---|---|
| `sixpack` | what own ship is doing: speed, altitude, vertical speed, track, turn, and what the cell has left, or the charge rail on the cable |
| `status` | what the sensors say: fix, position, pressure, battery, UTC, and whether we transmit |
| `satellites` | what is above the antenna, how loud, and which satellites solved |
| `nearby` | every target placed, most relevant first: who, how far, how far above |
| `menu` | what a pilot can change, or which diagnostic page to open |
| `confirm`, `installing`, `recovery` | being asked, receiving and writing an image, being handed to the USB bootloader |
| `activation` | what a unit with no serial shows instead of every page |
| `bravo` | which Bravo this Go is paired with, and the pairing |
| `bravo_log` | what a paired Bravo runs, holds and hears, and the frames it forwards |

`go::Page` lists the four pages the pad walks first, `Radar`, `Nearby`, `SixPack` and `GMeter`, and `go::kWalkedPages` marks where the pages opened by name begin. All four always stand, because the radar is home and the nearby menu opens every page off the walk. A long touch returns to `Radar` from anywhere once no alarm stands to be silenced. Every page arrives through one call that wipes the glass even when the page is already up, because a pilot holding the pad at home asks for that black (`../input/README.md`). The alarm asks for the radar only when it is not up, so it never spends that wipe.
