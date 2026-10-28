# ui

The drawing kit: a 1-bit canvas, the 5x7 font inside it, and the marks that are the same on any glass. No page lives here and no panel size is written here.

`Canvas` carries its own width and height and draws on bytes it does not own. `Panel<W,H>` is the storage, and it is declared where the size is known: `boards/<board>/glass.h` states what is soldered on, a product names it (`products/skyblip_go/glass.h` is `ui::Panel<200,200>`), and the driver that drives it says what it can drive (`parts::Ssd1681::kGlassWidth`). The board is where those two are asserted equal.

The size is not a constant in this directory because a canvas that knew it would fix one panel for the whole tree: `make test` links every product into one binary, so the day a second product carries a different glass, a global `kWidth` is a compile error at best and one product drawing through another's stride at worst. A runtime width costs a multiply per row on a panel that refreshes once a second.

A page is a pure function from a snapshot to pixels and belongs to the product whose device it is: `products/skyblip_go/pages/`. So does what a press means (`products/skyblip_go/input/`), and the contact conditioning under it is `core/input/`. What is left here is what any product would draw the same way.

| File | What it is |
|---|---|
| `canvas.h`, `canvas.cpp` | the surface, the primitives, the font |
| `widgets/wordmark.*` | the skyBlip wordmark, the brand mark |
| `widgets/skyship.*` | the ownship symbol, hot spot on the point given |
| `widgets/blip.*` | the traffic blip: a diamond, or the same mark pointing up or down, at two sizes, with its caret |

Nothing here includes a framework header, which is what buys the host suite and the WASM simulator.
