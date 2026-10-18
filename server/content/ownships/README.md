# Ownship drawings

One file per aircraft type, named after the key `Ownship::AircraftType` gives it, in the order of the aircraft type select on the manage page, so `glider.yml` is type 4. A type with no file shows the light aircraft, which is the symbol every device compiles in today. `/briefing/ownship` draws them.

## The file

- `art`: the drawing as text, the same text the editor copies and pastes. Rows of `#` and `.`, nose up, one character per pixel of the 200x200 glass, and a `<` after the hot spot row: the row that lands on the centre of the radar, where the aircraft is. The position of a target is measured from it, so it sits on the wing, not on the middle of the bounding box. A drawing with no `<` puts it on the widest row.
- `planforms`: the real aircraft the drawing is made against, in plan view, the first one shown by default. Each has a `key`, a `name`, its `sources`, a `hot_spot` in metres from the nose, and an `outline`: the right half from the nose round to the tail, in metres from the centreline and from the nose. The editor ghosts the chosen one under the grid at its true proportions, scores the drawing against it stretched to the grid, and traces a starting drawing from it.

The outlines are drawn from published dimensions and from measuring 3-views, so no drawing of anyone else's is shipped. The light aircraft has three: the Nicollier HN 700 Ménestrel II, measured off the Molveau 3-view, whose licence is unknown, so it is a measuring reference only; the Spitfire Mk IX, measured off Marek Cel's CC0 3-view of the Mk V, which shares the Mk IX's wing and tail; and the outline the standalone editor this page replaced carried as its background, the one the compiled symbol was hinted against.

A pilot can import an image instead, a plan view of their own aircraft, one per aircraft type. It stays in the browser, in local storage, and is never sent: it sits under the grid at its own proportions, with no score and nothing to trace.

## Where the editing happens

In the browser. The page is loaded once per aircraft type, with the type's drawing, its outlines, the device's box and the words of the checks as Stimulus values, and from then on `ownship_controller.js` paints, reshapes, traces, scores and checks with `app/javascript/skyblip/ownship.js` and sends nothing. Each change, a whole stroke for painting, rewrites the address in place, so a reload or a bookmark keeps the drawing, and keeps it in local storage as the draft for its type. A drawing opened from a link becomes the draft only once it is edited, so a link never erases one. A drawing travels as its text: copy it, paste it into another editor or a message.

The rules a drawing must follow exist twice, in `ownship.js` for the editor and in `Ownship::Checking` for `ownship:compile`, and `test/fixtures/files/ownship_checks.json` holds both to the same answers: `node --test` runs it against one and `bin/rails test` against the other.

`light_aircraft.yml` must stay the table in `firmware/ui/widgets/skyship.cpp`, and the test suite says so. Change the drawing here, then `bin/rails ownship:compile` writes the table. It refuses a drawing of another size or hot spot than the compiled one, which `skyship.cpp` asserts and every screen measures against.

## What a drawing must be

The checks are the ones the standalone editor made, with the box the firmware lays its screens out around added: every screen was laid out around the compiled symbol, 24 px wide, 5 rows ahead of the hot spot and 11 from it aft (`ui/widgets/skyship.h`). Wider, the wingtips touch the formation square on the radar. Longer, the tail runs into the banner below it.

- Even width, so the symbol straddles the centre of the glass, which is the point between pixels 99 and 100.
- Mirrored about that axis, and the two middle columns inked on every row.
- No pixel without a neighbour: on a collision display a lone dot reads as traffic.
- No blank row inside, or the tail reads as detached.

## The glider

The drawing is a Jonker JS3 Rapture with its 18 m wings. It is the sailplane pilots covet most today, on three counts:

- the 18 m class at the 2024 World Gliding Championships in Uvalde: 14 of the top 25 flew a JS3, 3rd to 6th included, against four AS 33s, one of which won, and two Ventus 3s ([results](https://www.soaringspot.com/en_gb/38th-fai-world-gliding-championship-uvalde-garner-field-2024/results/18-meter/task-12-on-2024-08-30/total));
- 200 built by October 2023 ([Wikipedia](https://en.wikipedia.org/wiki/Jonker_JS-3_Rapture));
- about a year of lead time on a new one ([Jonker](https://jonker-sailplanes.com/js3-rapture/)).

The Schleicher AS 33 is the runner-up, and in plan view the two are nearly the same aircraft, so one symbol covers both.

The outline comes from Jonker's published figures (18 m span, 9.95 m² of wing, 6.86 m long) and from measuring their [3-view](https://jonker-sailplanes.com/wp-content/uploads/2025/08/Copy-of-JS3_RES_3_VIEW_no_logo.png): root chord 0.76 m, tapering to about 0.2 m at a rounded tip, a straight leading edge 2.76 m aft of the nose, a fuselage 0.63 m wide at the cockpit narrowing to a 0.1 m boom, and a T-tail 2.68 m wide near the end of the fuselage. The measured chords integrate to 10.1 m² against the published 9.95 m².

At 24 px of span the real glider is 9 px long with a wing less than one pixel deep, and that does not read. So the drawing is 11 rows long and borrows ink where the eye looks for a glider:

- a wing 2 rows deep at the root and 1 row deep at the tips, so the taper shows: the whole span on the hot spot row, 18 px on the row behind it;
- a fuselage of 2 px from the nose to the tail, thinner than any powered aircraft's, with the wing 4 rows behind the nose, where the JS3 has it;
- a T-tail of 6 px, a little over the real 0.15 of the span, on the second-to-last row, with the rudder below it.

A first version had a canopy pod 4 px wide and an 8 px tail. The pod read as the cowling of a light aircraft, and the even fuselage reads as a glider.

## The Bluetooth command, proposed

The editor shows the command a device could take a drawing in. No firmware reads it yet.

```json
{"cmd":"ownship","type":4,"w":24,"o":5,"rows":"<8 hex digits per row, leftmost column in the lowest bit>"}
```

It is one write on the config endpoint, beside `set`, and the tallest symbol the box allows, 16 rows like the light aircraft, comes to 177 bytes, inside the 182 an iPhone link carries (`firmware/core/comms/README.md`). Like a `set`, the device would ask on its glass before storing it, store one drawing per type, and draw the one of the type it is set to, the compiled symbol remaining for a type it holds nothing for. A drawing it cannot fit is refused by the same rules as above, which is why they are the device's box and not the editor's taste.
