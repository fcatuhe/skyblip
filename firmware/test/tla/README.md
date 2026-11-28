# firmware/test/tla

Two TLA+ specifications of the state machines that decide what a unit takes for the aircraft's own, checked with TLC. They model the code as it runs, not a design, so a change to `core/traffic/aboard.h` or to the notices and menus of `products/skyblip_go/services/screen*.cpp` changes them too. CI does not run them, because TLC needs Java.

| Spec | What it models | What TLC checks |
|---|---|---|
| `Aboard.tla` | `traffic::Aboard`: the table, the admission, the damping, the leader loop and the hold | the loop names the emitter the README's rule names, an emitter is named only once it has qualified at every tick for the whole hold, and one that has is named |
| `AboardGlass.tla` | `go::ScreenService`: the `NewAircraft` and `NewFlarm` notices, the `AIRCRAFT` and `FLARM` blocks and the question that erases a FLARM ID, with the panel drawing a pass or more late | nothing is stored or erased that the glass did not show, the FLARM notice opens once a boot, a notice that stands proposes what is heard on the ground, an alarm takes the glass, and no notice opens over a menu |

`AboardGlassAsWas.cfg` runs `AboardGlass.tla` with `Fixed = FALSE`, the code before the FLARM ID: TLC finds a store of what the glass never showed within eight steps. Two presses, `OPEN` then `SET`, stored the block the settings menu had last computed but not drawn. A notice that followed another inherited its predecessor's time on the glass, so a press stored a proposal still being drawn.

Run from this folder, with the jar from [github.com/tlaplus/tlaplus/releases](https://github.com/tlaplus/tlaplus/releases):

```
java -cp tla2tools.jar tlc2.TLC -workers auto -config Aboard.cfg Aboard.tla
java -cp tla2tools.jar tlc2.TLC -workers auto -config AboardGlass.cfg AboardGlass.tla
java -cp tla2tools.jar tlc2.TLC -workers auto -config AboardGlassAsWas.cfg AboardGlass.tla
```

On 27 November 2026, with TLC 2026.11.26, `Aboard` explored 17,411 distinct states with no error, and `AboardGlass` 1,388,295 in two to six minutes on eight cores with no error.
