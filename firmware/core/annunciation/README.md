# core/annunciation

What the buzzer should be doing right now, decided in one place. The traffic tracker says which level stands and whether it just got worse; `pattern.h` says how that sounds, as a tune of notes each with a pitch, a length and the silence after it, and it is the only thing allowed to hand the buzzer from one voice to another.

`ports::Annunciator::tone()` opens a continuous tone that runs until `silence()`, so a tune is over at its last note and nothing here can hold the tone open. A note with no silence after it runs straight into the next pitch. The policy issues a command only when the tone, or its pitch, changes, because a PWM re-armed on every service pass is a click and not a tone.

## The advisory says sky-blip, twice

The advisory is two calls, each a low note then a high one, so every call rises. A tone that changes pitch is noticed against a steady noise more readily than a held one, and a cockpit is a steady noise. The low note is long and the high one short, the way the name is said: sky-blip. The second call's blip is long, so the advisory ends on a note that sounds finished rather than cut off.

The rhythm is written on a grid, `kTuneSixteenthMs`, and every length is a whole number of it: sky 2, blip 1, rest 3, sky 2, long blip 3. The first-fix jingle is written on the same grid, each note 1 with a gap of 1 or 3 and the last note held for 3, so both tunes keep one tempo and end on the same held length. The grid is `kShortestBlipEarCanPlaceMs`, because the blip is one grid step and cannot be shorter than the ear can place. Changing the grid changes the tempo of both and keeps their proportions.

The two pitches are A7 and C#8, 3520 Hz and 4435 Hz, an equal-tempered major third, so the call is in tune rather than an arbitrary pair of frequencies. They sit either side of where the transducer is loudest. The part LilyGO fits is not identified in its own material; it is a passive piezo driven straight off one PWM pin, and commodity 9x9 mm SMD piezo sounders resonate at 4000 Hz +/- 500 Hz, so whichever way a given unit's resonance falls, one note of every call is near it. A bench sweep with a sound level meter is what settles the real peak, and the pair moves to the third that straddles it then.

## Loudness is the port's, the pattern is not

The pattern decides pitch and timing and never level. The pilot's volume reaches the port as `ports::Loudness`, `Soft` or `Loud`, and what that means in duty cycle is the platform's: a square wave's fundamental goes as sin(pi x duty), so half duty is the most one pin can give and a tenth of the period is about 10 dB under it. A volume of off never reaches the port: the service treats it like alarms switched off for the buzzer, and the haptic still pulses, because under a headset the motor is the alarm that gets through.
