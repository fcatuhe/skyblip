# core/annunciation

What the buzzer and the vibration motor should be doing right now, decided in one place. The traffic tracker says which level stands and whether it just got worse; `annunciator.h` says how that sounds and how it is felt, as a tune of notes each with a pitch, a length and the silence after it, and it is the only thing allowed to hand the buzzer from one voice to another.

`ports::Annunciator::tone()` opens a continuous tone that runs until `silence()`, so a tune is over at its last note and nothing here can hold the tone open. A note with no silence after it runs straight into the next pitch. A tune that starts while another is sounding, a second aircraft mid-call or traffic cutting the fix tune, starts after `kReannounceRestMs` of silence, so it is heard and felt as a new call and no stretch of sound outlasts the longest call. The policy issues a command only when the tone, or its pitch, changes, because a PWM re-armed on every service pass is a click and not a tone.

## The advisory says sky-blip, twice

The advisory is two calls, each a low note then a high one, so every call rises. A tone that changes pitch is noticed against a steady noise more readily than a held one, and a cockpit is a steady noise. The low note is long and the high one short, the way the name is said: sky-blip. The second call's blip is long, so the advisory ends on a note that sounds finished rather than cut off.

The rhythm is written on a grid, `kTuneSixteenthMs`, and every length is a whole number of it: sky 2, blip 1, rest 3, sky 2, long blip 3. The grid is `kShortestBlipEarCanPlaceMs`, because the blip is one grid step and cannot be shorter than the ear can place. Changing the grid changes the tempo of every alarm and keeps their proportions. `kShortestAlarmPhaseMs` is the shortest note or gap of any alarm, the advisory, the lost-peer note and the rest before a new call. It has to clear the ear's floor, and `common::AlarmService` asserts that the service loop passes through it eight times, so an alarm sounds as written to within one pass.

The two pitches are A7 and C#8, 3520 Hz and 4435 Hz, an equal-tempered major third, so the call is in tune rather than an arbitrary pair of frequencies. They sit either side of where the transducer is loudest. The part LilyGO fits is not identified in its own material; it is a passive piezo driven straight off one PWM pin, and commodity 9x9 mm SMD piezo sounders resonate at 4000 Hz +/- 500 Hz, so whichever way a given unit's resonance falls, one note of every call is near it. A bench sweep with a sound level meter is what settles the real peak, and the pair moves to the third that straddles it then.

## The first fix is a melody

The first fix plays E E . E . C E . G on an eighth-note grid at 200 BPM, `kFirstFixTempoBpm`, so an eighth is 150 ms, `kFirstFixEighthMs`. Each note sounds for 7/9 of its eighth, 117 ms, `kFirstFixNoteMs`. The silence after it keeps two repeated notes apart: 33 ms before the next eighth and 183 ms across a skipped one. The last note is as short as the others.

The fix tune is a melody, not an alarm, so the ear's floor and the eight passes do not bind it. Its 33 ms gap still spans three passes of the loop that the alarms hold to, so the loop never swallows it.

## The motor says the calls, and the signs on and off

A call is the notes that run together between two silences. The traffic voice runs the motor exactly while a call sounds: 270 ms, then 450 ms, with the 270 ms rest between them, the same `kAdvisory*Ms` figures the buzzer plays. A pilot under a headset feels the rhythm others hear. The motor is driven the way the buzzer is, on until told off, so it stops on the pass the buzzer does, whatever released it. `kAnnunciatorOnCeilingMs` is the guard the adapters hold under both, for a loop that stops running mid-call (`ports/README.md`). The fix tune is never felt. The motor is reserved for traffic and for the signs on and off (`../indication/README.md`), which are one pulse and three, so a pilot who feels two calls knows what they mean without looking.

The ERM on the T-Echo Plus takes time to spin up and to stop. Whether a 270 ms pulse is felt through a harness, and whether the 270 ms rest reads as a gap, is a bench question that has not been measured.

Bravo never calibrates the X1's LRA, because the calibration drives the motor for about a second outside any call (`hardware/parts/drv2605/README.md`).

## A lost peer is one low note

A paired Go whose alarm sounds and that loses its Bravo plays `kPeerLostTune`, one C7, 2093 Hz, held `kPeerLostNoteMs`, three steps of the grid. `common::AlarmService` raises `Situation::peer_lost` on the one pass `bus::State::bravo` turns lost, so a Bravo that stays lost, or changes reason while lost, is not said again, and one that comes back says nothing. A Bravo that never came up turns lost on the Go's first taxi, so the note can come on the taxiway rather than in flight (`../comms/README.md`, What the Go makes of its Bravo). One note and not a call: it is a fact about the equipment, not about the sky, and the glass's corner carries the reason. It sits a sixth below the advisory's A7, on C7, the lowest pitch any tune plays, which the first-fix jingle shares, so it is never heard as traffic. It is never felt, since the motor is reserved for traffic and the signs on and off. Traffic outranks it and cuts it as it cuts the fix tune, and it outranks the fix tune.

A paired Bravo plays the same note on the pass it stops playing its Go's calls, because the link dropped or the claims stopped, while its own flight state is airborne and its alarm in force sounds. A plain Go has no buzzer for its own tone, so Bravo's note is all its pilot hears when the pair comes apart. On the ground it says nothing, so switching the Go off after landing is silent, and an unpairing says nothing either, because the pin goes before the link.

## A paired Bravo plays its Go's calls

A Go paired with a Bravo grades the traffic for both, and Bravo plays what the Go announces (`../comms/README.md`, The alarm). `call.h` is the shape of that: a `Call` is the level the leader announces, the level its glass and lamp show, and the number of its announcements since it booted. The leader's tracker says when an aircraft is announced, and the number moves on that pass. `Follower` is the other end. It hands the follower's policy the leader's level, and says an aircraft was announced whenever the number moves, so a second aircraft under a standing level is called again on both units, as it is on one. The first call it hears after a lost leader is no announcement, because the leader numbers from 0 again after a boot.

A press on the follower dismisses the call it heard: `dismiss()` holds the number, and `follow()` answers no level for as long as the leader still sends that number. The leader learns which call was dismissed and dismisses it too, and a new number ends the hold, so the press silences the follower at once and never outlasts the call it answered. `lose()` forgets the leader and the hold together, and the follower grades its own traffic again.

The policy is the same on both units, so arbitration stays here: traffic outranks the lost peer, which outranks the fix tune, whichever unit graded the traffic. The fix tune and the lost-peer note are each unit's own and never travel.

## Loudness is the port's, the tune is not

The tune decides pitch and timing and never level. The pilot's volume reaches the port as `ports::Loudness`, `Soft` or `Loud`, and what that means in duty cycle is the platform's: a square wave's fundamental goes as sin(pi x duty), so half duty is the most one pin can give and a tenth of the period is about 10 dB under it. An alarm of off or visual never reaches the port: the service silences the buzzer and the motor both. A unit with neither a buzzer nor a motor plays a soft or loud alarm as visual, so a plain Go never sounds, while the Bravo it is paired with does.
