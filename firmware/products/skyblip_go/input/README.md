# products/skyblip_go/input

Two contacts, four things a pilot can say. What a contact does electrically is the board's and is conditioned in `core/input/`; what it means is here, because the thresholds that separate a tap from a long touch, and a press from the hold that switches the device off, are a contract with a pilot and a board must not be able to read them.

The board pushes one `events::ContactEvent` per settled edge, stamped when the level changed. `Controls` turns those into the three gestures this device answers, named after what the thumb does and not after what the screen makes of it, and `go::ScreenService` is the only thing that reads them: one method per gesture, `tap`, `long_touch`, `press`, each a ladder of meanings in the order they win.

| What the thumb does | `go::Gesture` | On a page | In a menu |
|---|---|---|---|
| pad touched and released under `Controls::kLongTouchMs` | `Tap` | the next page, or on `capture` the next capture offered, and off the last one the page it was opened from | the next row, and off the last row the page it belongs to |
| pad held past `Controls::kLongTouchMs` (1 s) | `LongTouch` | a standing alarm is dismissed, and with none standing, the radar, through black even when the radar is already on the glass | the radar |
| button pressed | `Press` | opens this page's menu | changes the focused row, or opens the page it names |
| button pressed twice inside `ConfirmGesture::kDoublePressMs` | `Press`, twice | nothing, unless a prompt stands: then it authorises, or on `capture`, where it arms and stops the diagnostics capture | the same |
| button held past `power::kLongPressMs` (2 s) | none | off | off |
| pad held through that press | none | off, with a blank panel: the stow | the stow |

One vocabulary on two contacts: the pad is touched, the button is pressed, and the long one of each is the second thing that contact says. The pad moves, the button acts, and the long touch is the way back to the traffic picture from anywhere. Nothing a pilot learns on the pages has to be unlearned on the rows.

The pad only ever moves forward and it never dead-ends. The pages it walks are a closed rotation of three (`../pages/README.md`), the rows of a menu are not: past the last row it hands the glass back to the page the menu belongs to, so the thumb that opened a menu closes it with the same gesture rather than hunting for a way out.

The pad carries the navigation because it is what a gloved thumb finds on the top edge without looking, and the page lands on the release rather than on the contact: that is what leaves room for the long touch in the same finger. A touch the button joins says nothing at all, in either direction, because that pair is already the stow and a device on its way off must not change page on the way.

A long touch resolves without being let go of, which is why `Controls` is ticked as well as read: the pilot's finger is still on the pad when the radar comes back, and a gesture that waited for the release would be a second of glass saying nothing.

## The long touch under an alarm

With any graded contact standing, the long touch dismisses it (`core/traffic/README.md`) and stops there. Silence is what the pilot is asking for with a buzzer going, and it is what the gesture spends itself on: the page they are on is left where it is, and the way home waits for the next long touch, which is one made in quiet. An alarm loud enough to be worth turning the head for has already brought the radar with it (`../pages/README.md`), so the touch that silences it is almost always made on the traffic picture anyway.

What that buys is a glass that stands still at the moment it is being read. The dismissal asks for nothing else: the page is left where it is, so it does not cost the 360 ms of black every screen change goes through. A pilot holding the pad at a converging glider gets the sector, the tone and the lamp out, and the plot they were reading stays on the glass throughout.

The same touch made in quiet on the radar does go through black, and that is the point of it. The rings, the ownship and the range label are the same ink for hours, which is what an e-paper keeps a shadow of, so a pilot who sees the picture greying has a way to scrub it that needs no menu and nothing to learn: the gesture they already use to come home, made at home. `show_page` therefore draws the page it is given whether or not it is the page already on the glass, and the one caller that must not spend the black is the alarm taking the glass, which asks only when the radar is not already there.

It is the pad and not the button. The button's long press is already the way the device switches off, and a pilot silencing an alarm must never be a thumb away from stowing the device that raised it. The press the button does have here opens the radar's menu, which is a page the same alarm has just taken off the glass.

What a dismissal costs if it was an accident is one flight's worth of nothing: the grade stands, the traffic stays plotted, and anything worse speaks again with its sector and the lamp back. The touch covers the aircraft the device has already spoken about and no others, so the next one heard arrives with its own voice. That is what makes a single 1 s touch the right price rather than a gesture a pilot has to be taught.

## The button's third meaning

`ConfirmGesture` is the security boundary, not an input helper. This product ships with BLE pairing off, so nothing proves cryptographically that the phone asking for a firmware upload belongs to the pilot, and physical presence stands in for it. The gesture has to be one a thumb cannot produce by accident and one that cannot be confused with the press that opens a menu or the long press that switches the device off: a double press inside `kDoublePressMs` is the only one of the button's meanings a pilot has to mean to make.

It authorises nothing unless a prompt the pilot can read is on the glass, and a lone press at a prompt refuses the operation rather than leaving it standing. Fail closed, which is what makes "press twice to allow, once to refuse" true on the panel.

The `capture` page borrows the same gesture rather than inventing a fourth thing the button can say, and under the same two conditions: the page has reached the glass, so the price it states has been readable, and one press refuses. Arming a capture spends the pilot's partition and cannot be a gesture a thumb makes on the way past (`../pages/README.md`). What it arms is the capture the pad has the bar on, and the pad walks off the last one to the menu, so neither contact dead-ends on that page.
