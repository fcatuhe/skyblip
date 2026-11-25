# disciplines

A discipline is a way of flying that several aircraft categories share, and its pack is what that way of flying adds to the core. `soaring` serves gliders, hang gliders and paragliders, which circle in thermals and share them with other aircraft. The discipline is own-ship's: `discipline_of` reads `Ownship::aircraft_category`, the ADS-L G.1.3 category the pilot set. A pack reads each target's category too, where the aircraft it serves differ.

| Discipline | Categories | Pack |
|---|---|---|
| `Soaring` | glider, hang glider, paraglider | `soaring/`, which raises nothing yet |
| `None` | every other category | none: the core alone grades |

## What a pack may do

A pack answers one question: what level a target deserves beyond what the ring gives it. The ring is the 3 km and 300 m advisory window in `core/traffic/`, which predicts nothing (`../core/traffic/README.md`). For each target on each pass, `common::AlarmGrader` asks `discipline::grade`, which asks the pack of own-ship's discipline. `traffic::AlarmTracker::update` then keeps the higher of the pack's level and the ring's. So a pack can make an aircraft outside the ring an advisory, and can never make the unit quieter.

The core's rules stay above every pack:

- nothing is graded while either aircraft is on the ground;
- a formation member stays silent (`../core/traffic/formation.h`);
- a dismissal holds until the aircraft arrives again;
- a position older than `traffic::kAlertMaxAgeMs` is not announced.

A pack never changes what goes on air. Its files include only the C++ standard library, the pack's own directory and the headers a discipline reads: `core/model/`, `core/flight/`, `core/traffic/`, `core/units/`, `core/util/` and `protocols/common/`. `scripts/check_discipline_boundary.py` holds every file here to that list in CI. The files directly under `disciplines/` are the dispatcher, which may also include the packs. A pack receives own-ship and the target as `const` references.

`traffic::Level` has two values, `None` and `Advisory`. A pack that needs a more urgent level asks for one, because the new level changes `core/traffic/` and the annunciator.

## The files

| File | Changed by | What it holds |
|---|---|---|
| `discipline.h`, `discipline.cpp` | the maintainer | `Discipline`, which category flies which discipline, and the call to each pack |
| `<pack>/` | the pack's contributor, through a pull request | the pack's code and its `README.md` |
| `../test/disciplines/` | both | the claims, named `<pack>: what holds` |

## Writing a pack

A pack follows the rules of `core/`: integer arithmetic, no heap, no framework header. It compiles into the host suite, the browser simulator and both product images. Its constants follow the tuning vocabulary in `../README.md`, so `docs/TUNING.md` lists them with the comment that justifies each.

A pack is a free function today: `traffic::Level grade(own, target, now_ms)`. A pack that must remember something between passes, such as a wind estimated over a circle, becomes a class with an `observe(own, now_ms)` call, and the dispatcher holds its instance. A pack that has a figure to show, such as that wind, publishes it through a slot on `bus::State`, and a page in the product draws it. The maintainer makes each of these changes with the first pack that needs it.

A pull request for a pack brings:

- the code under `disciplines/<pack>/`, and its `README.md`, which says what the pack predicts and where each number comes from;
- the claims under `test/disciplines/`, one `TEST_CASE` per claim, named `<pack>: what holds`;
- a scenario under `scenarios/` when the pack changes what a flight hears, with a case in `test/products/skyblip_go/test_scenarios.cpp` that flies it. The browser simulator plays every scenario there.

## From SoftRF

The type-specific features of moshe-braner's SoftRF fork land here, or elsewhere, as follows:

| Feature | In the fork | Here |
|---|---|---|
| Collision prediction for circling aircraft | `Alarm_Latest` in `src/TrafficHelper.cpp` | `soaring::grade`, on `flight::Arc`, which already carries a circling aircraft along its arc (`../core/flight/README.md`) |
| Wind estimated while circling | `src/Wind.cpp` | `soaring`, once the pack keeps state |
| No alarm between a tug and its glider | `Traffic_Update` in `src/TrafficHelper.cpp` | already done by `core/traffic/formation`, on geometry alone |
| Paraglider alarms off, or graded on distance alone | `Alarm_Latest` | not a pack, because it lowers a level: a change to `core/traffic/` |
| Winch mode, relaying landed-out aircraft, the radio protocols | `src/protocol/radio/` | not a pack, because it changes what goes on air: `protocols/` and `core/` |
