# core/annunciation

What the buzzer and the vibration motor should be doing right now, decided in one place. The traffic tracker says which level stands and whether it just got worse; `annunciator.h` says how that sounds and how it is felt, as a tune of notes each with a pitch, a length and the silence after it, and it is the only thing allowed to hand the buzzer from one voice to another.

`ports::Annunciator::tone()` opens a continuous tone that runs until `silence()`, so a tune is over at its last note and nothing here can hold the tone open. A note with no silence after it runs straight into the next pitch. The policy issues a command only when the tone, or its pitch, changes, because a PWM re-armed on every service pass is a click and not a tone.

## The advisory says sky-blip, twice

The advisory is two calls, each a low note then a high one, so every call rises. A tone that changes pitch is noticed against a steady noise more readily than a held one, and a cockpit is a steady noise. The low note is long and the high one short, the way the name is said: sky-blip. The second call's blip is long, so the advisory ends on a note that sounds finished rather than cut off.

The rhythm is written on a grid, `kTuneSixteenthMs`, and every length is a whole number of it: sky 2, blip 1, rest 3, sky 2, long blip 3. The first-fix jingle is written on the same grid, each note 1 with a gap of 1 or 3 and the last note held for 3, so both tunes keep one tempo and end on the same held length. The grid is `kShortestBlipEarCanPlaceMs`, because the blip is one grid step and cannot be shorter than the ear can place. Changing the grid changes the tempo of both and keeps their proportions.

The two pitches are A7 and C#8, 3520 Hz and 4435 Hz, an equal-tempered major third, so the call is in tune rather than an arbitrary pair of frequencies. They sit either side of where the transducer is loudest. The part LilyGO fits is not identified in its own material; it is a passive piezo driven straight off one PWM pin, and commodity 9x9 mm SMD piezo sounders resonate at 4000 Hz +/- 500 Hz, so whichever way a given unit's resonance falls, one note of every call is near it. A bench sweep with a sound level meter is what settles the real peak, and the pair moves to the third that straddles it then.

## The motor says the calls, and only the calls

A call is the notes that run together between two silences. The traffic voice pulses the motor at the start of each call, for as long as the call sounds: 270 ms, then 450 ms, with the 270 ms rest between them, the same `kAdvisory*Ms` figures the buzzer plays. A pilot under a headset feels the rhythm others hear. The pulse is a length handed to `ports::Annunciator::vibrate()`, whose own timer ends it, so a loop that stops running never leaves the motor on. The fix tune is never felt: the motor is reserved for traffic, so a pilot who feels it knows what it means without looking.

The ERM on the T-Echo Plus takes time to spin up and to stop. Whether a 270 ms pulse is felt through a harness, and whether the 270 ms rest reads as a gap, is a bench question that has not been measured.

## Loudness is the port's, the tune is not

The tune decides pitch and timing and never level. The pilot's volume reaches the port as `ports::Loudness`, `Soft` or `Loud`, and what that means in duty cycle is the platform's: a square wave's fundamental goes as sin(pi x duty), so half duty is the most one pin can give and a tenth of the period is about 10 dB under it. A volume of off never reaches the port: the service treats it like alarms switched off, buzzer and motor both.
