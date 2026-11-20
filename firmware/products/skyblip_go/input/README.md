# products/skyblip_go/input

Two contacts, four things a pilot can say. What a contact does electrically is the board's and is conditioned in `core/input/`. What it means is here, because the thresholds that separate a tap from a long touch, and a press from the hold that switches the device off, are a contract with a pilot that no board may change.

The board pushes one `events::ContactEvent` per settled edge, stamped when the level changed. `Controls` turns those into three gestures, and `go::ScreenService` is their only reader: one method per gesture, `tap`, `long_touch` and `press`, each a ladder of meanings in the order they win.

| What the thumb does | `go::Gesture` | On a page | In a menu |
|---|---|---|---|
| pad touched and released under `Controls::kLongTouchHoldMs` | `Tap` | the next page, and off a page opened from a menu the page it was opened from | the next row, and off the last row the page it belongs to |
| pad held past `Controls::kLongTouchHoldMs` (1 s) | `LongTouch` | a standing alarm is dismissed, and with none standing, the radar, through black even when the radar is already on the glass | the radar |
| button pressed | `Press` | opens this page's menu, or accepts a standing prompt | changes the focused row, or opens the page it names |
| button held past `power::kLongPressHoldMs` (2 s) | none | off | off |
| pad held through that press | none | off, with a blank glass: the stow | the stow |

The pad moves, the button acts, and the long touch is the way back to the traffic picture from anywhere, so nothing learned on the pages is unlearned on the rows. The pad only moves forward and never dead-ends: past a menu's last row it hands the glass back to the page the menu belongs to.

The pad carries the navigation because a gloved thumb finds it on the top edge without looking. The page changes on the release, which leaves room for the long touch in the same finger. A touch the button joins says nothing, because that pair is the stow and a device on its way off must not change page. A long touch resolves while the finger is still down, which is why `Controls` is ticked as well as read.

## The long touch under an alarm

With any graded contact standing, the long touch dismisses it (`core/traffic/README.md`) and does nothing else: the page stays, with no 360 ms of black, so the plot the pilot is reading stays on the glass. The way home waits for the next long touch. The dismissal covers the aircraft the device has already spoken about and no others, so the next one heard sounds again, and the grade and the plot stand. That is why a single 1 s touch is the right price.

Made in quiet on the radar, the same touch goes through black on purpose: the rings and the range label are the same ink for hours, and the black scrubs the shadow e-paper keeps of them. So `show_page` draws the page it is given even when it is already on the glass, and the alarm taking the glass asks for the radar only when the radar is not already there.

The dismissal is on the pad, not the button, because the button's long press switches the device off, and a pilot silencing an alarm must never be a thumb away from stowing the device that raised it.

## The press that authorises

A phone's prompt is answered by one press of the button through `common::PromptAnswer`, the security boundary, whose rule is in `../../common/README.md` (Prompts and the press that answers them). A tap of the pad at an armed prompt refuses it, as the glass's `CANCEL` key shows (`../pages/README.md`, confirm). A capture a page asks for is answered on the confirm page too, which names the profile and the flights it erases before the press spends them (`../README.md`, Which capture is armed).
