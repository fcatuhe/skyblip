# core/annunciation

What the buzzer should be doing right now, decided in one place. The traffic tracker says which level stands and whether it just got worse; `pattern.h` says how that sounds (on times, off times, how many, how often it is said again), and it is the only thing allowed to hand the buzzer from one voice to another.

`ports::Annunciator::tone()` opens a continuous tone that runs until `silence()`, so a level with no off time is a level that never stops: every pattern here ends by itself. The policy issues a command only when the tone, or its pitch, changes, because a PWM re-armed on every service pass is a click and not a tone.

## The advisory warbles across the piezo's peak

Each beep of the advisory pair is two pitches, the low one for the first half and the high one for the second, so the beep rises. A tone that changes pitch is noticed against a steady noise more readily than a held one, and a cockpit is a steady noise.

The pitches bracket where the transducer is loudest. The part LilyGO fits is not identified in its own material; it is a passive piezo driven straight off one PWM pin, and commodity 9x9 mm SMD piezo sounders resonate at 4000 Hz +/- 500 Hz. `kAdvisoryWarbleLowHz` and `kAdvisoryWarbleHighHz` sit 400 Hz either side of that centre, so whichever way a given unit's resonance falls, half of every beep is near it. A bench sweep with a sound level meter is what settles the real peak, and both figures move to it then.

Each pitch lasts half a beep, 125 ms. That is above `kShortestPhaseMs`, so the 10 ms service loop places the step to within one pass, and long enough for the ear to hear two pitches rather than a buzz.

## Loudness is the port's, the pattern is not

The pattern decides pitch and timing and never level. The pilot's volume reaches the port as `ports::Loudness`, `Soft` or `Loud`, and what that means in duty cycle is the platform's: a square wave's fundamental goes as sin(pi x duty), so half duty is the most one pin can give and a tenth of the period is about 10 dB under it. A volume of off never reaches the port: the service treats it like alarms switched off for the buzzer, and the haptic still pulses, because under a headset the motor is the alarm that gets through.
