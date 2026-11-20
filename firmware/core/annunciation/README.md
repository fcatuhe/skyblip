# core/annunciation

`annunciator.h` decides what the buzzer and the vibration motor do right now, and only it hands the buzzer from one voice to another. Traffic outranks a lost peer, which outranks the fix tune. A tune is a list of notes, each with a pitch, a length and the silence after it.

`ports::Annunciator::tone()` runs until `silence()`, so a tune ends at its last note and nothing here can hold the tone open. A note with no silence after it runs straight into the next pitch. A tune that starts over a sounding one waits `kReannounceRestMs` first, so it reads as a new call and no sound outlasts the longest call. The policy commands only a change of tone or pitch, because a PWM re-armed on every pass clicks.

## The advisory says sky-blip, twice

The advisory is two calls, each a long low note and a short high one, the way sky-blip is said. A rising pitch stands out against a cockpit's steady noise. The second blip is long, so the advisory sounds finished.

Every length is a whole number of `kTuneSixteenthMs`: sky 2, blip 1, rest 3, sky 2, long blip 3. The grid equals `kShortestBlipEarCanPlaceMs`, since the blip is one step. Changing it changes every alarm's tempo and keeps their proportions. `kShortestAlarmPhaseMs`, the shortest note or gap of any alarm, must clear the ear's floor, and `common::AlarmService` asserts that the service loop passes through it eight times, so an alarm plays as written to within one pass.

The pitches are A7 and C#8, 3520 Hz and 4435 Hz, an equal-tempered major third. LilyGO does not identify its piezo, and commodity 9x9 mm SMD piezo sounders resonate at 4000 Hz +/- 500 Hz, so the pair straddles the likely peak. A bench sweep with a sound level meter will find the real peak, and the pair will move to straddle it.

## The first fix is a melody

The first fix plays E E . E . C E . G in eighths at `kFirstFixTempoBpm`, 200 BPM, so `kFirstFixEighthMs` is 150 ms. Each note sounds 7/9 of its eighth, so a silence separates repeated notes. The last note, G, is a quarter, `kFirstFixLastNoteMs`, and sounds whole since nothing follows it. A melody is not an alarm, so the ear's floor and the eight passes do not bind it, though its 33 ms gap still spans three passes.

## The motor says the calls, and the signs on and off

The motor runs exactly while a traffic call sounds: 270 ms, a 270 ms rest, then 450 ms, so a pilot under a headset feels the rhythm. It stops on the pass the buzzer does. `kAnnunciatorOnCeilingMs` is the guard the adapters hold under both, for a loop that stops mid-call (`ports/README.md`).

The motor is reserved for traffic and for the signs on and off (`../indication/README.md`), one pulse and three, so two felt calls always mean traffic. No other tune is felt.

Nobody has measured whether the T-Echo Plus ERM, slow to spin up and stop, makes a 270 ms pulse and rest felt through a harness.

Bravo never calibrates the X1's LRA, because calibration drives the motor for about a second outside any call (`hardware/parts/drv2605/README.md`).

## A lost peer is one low note

A paired Go whose alarm sounds plays `kPeerLostTune` when its Bravo turns lost: one C7, 2093 Hz, for `kPeerLostNoteMs`. `common::AlarmService` raises `Situation::peer_lost` only on the pass `bus::State::bravo` turns lost, so a Bravo that stays lost or changes reason is not said again, and one that comes back says nothing. A Bravo that never came up turns lost on the Go's first taxi, so the note can come on the taxiway rather than in flight (`../comms/README.md`, What the Go makes of its Bravo).

It is one note and not a call, because it is about the equipment, not the sky, and the glass's corner carries the reason. C7 is the lowest pitch any tune plays, so it never sounds like traffic. It is never felt.

A paired Bravo plays the same note when it stops playing its Go's calls, because the link dropped or the claims stopped, while it is airborne and its alarm sounds. A plain Go has no buzzer, so this note is all its pilot hears. On the ground it stays silent, so switching the Go off after landing makes no sound. An unpairing is silent too, because the pin goes before the link.

## A paired Bravo plays its Go's calls

A paired Go grades the traffic for both units, and Bravo plays what the Go announces (`../comms/README.md`, The alarm). A `Call` in `call.h` carries the level the leader announces, the level its glass and lamp show, and its announcement count since boot. `Follower` reports an announcement whenever the count moves, so a second aircraft under a standing level is called on both units. The first call after a lost leader is not an announcement, because the leader counts from 0 after a boot.

A press on the follower dismisses the call it heard: `dismiss()` holds the count, and `follow()` answers no level while the leader still sends it. The leader dismisses it too, and a new count ends the hold. `lose()` forgets the leader and the hold, and the follower grades its own traffic again. The fix tune and the lost-peer note belong to each unit and never travel.

## Loudness is the port's, the tune is not

The tune sets pitch and timing, never level. The pilot's volume reaches the port as `ports::Loudness`, `Soft` or `Loud`, and the platform maps it to duty cycle. A square wave's fundamental goes as sin(pi x duty), so half duty is the loudest one pin gives and a tenth of the period is about 10 dB under it. An alarm setting of off or visual never reaches the port. A unit with neither buzzer nor motor plays soft or loud as visual, so a plain Go never sounds while its paired Bravo does.
