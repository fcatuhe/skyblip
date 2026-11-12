# parts/drv2605

The TI DRV2605 haptic driver at I2C 0x5A, with its enable line on a GPIO, driven by `Drv2605` and stood in for by `model.h` on the host. Why the T-Echo Plus pin is an enable and not a motor drive is the header's opening note.

## ERM or LRA

The part cannot tell which motor it drives, so the board names it with `Drv2605::Actuator`. The T-Echo Plus passes `Erm` and keeps the bring-up it always had: ERM library A, open loop, no calibration. The X1 passes `Lra` (`kHapticActuator` in its `pins.h`). Meshtastic's X1 variant defines `DRV2605_USE_LRA` (`variants/nrf52840/seeed_mesh_tracker_X1/variant.h:76-78` at `6d41e279`), and the first unit, whose smoke image read device ID 7, a DRV2605L, only buzzed weakly under the ERM drive. No source names the X1's motor part, so its rated voltage and resonant frequency are unknown. Every value below is from TI's DRV2605L datasheet, SLOS854D, and unverified until #169 benches the motor.

`begin()` drives an LRA in closed loop, where the auto-resonance engine tracks the resonant frequency by itself (8.3.2.1). It writes the registers below over whatever another firmware left, since EN low keeps them (8.4.1.3). A part whose OTP is programmed (OTP_STATUS, 8.6.24) keeps its RATED_VOLTAGE, OD_CLAMP, calibration results and FEEDBACK gains, which the datasheet's initialization skips calibration for (9.3.1 step 4), and only gets N_ERM_LRA set.

| Register | Value | Source |
|---|---|---|
| FEEDBACK (0x1A) | 0xB6: N_ERM_LRA 1, FB_BRAKE_FACTOR 3, LOOP_GAIN 1, and BEMF_GAIN kept as the part holds it, 2 at power-up, because it scales A_CAL_BEMF (8.6.19) | 8.6.20's power-up gains with N_ERM_LRA set, the value Meshtastic writes on the X1 (`src/main.cpp:806-820` at `6d41e279`). 8.5.6 step 3 suggests FB_BRAKE_FACTOR 2 and LOOP_GAIN 2 instead. 8.6.20 calls both fine for most actuators, and 0xB6 is the one known to run on this board |
| RATED_VOLTAGE (0x16) | 0x3E | 8.6.16's power-up value, about 1.5 Vrms near 208 Hz by equation 5 of 8.5.2.1 |
| OD_CLAMP (0x17) | 0x8C | 8.6.17's power-up value, 3.0 V peak by equation 9 of 8.5.2.2 |
| CONTROL1 (0x1B) | 0x93: STARTUP_BOOST, DRIVE_TIME 19 | 8.6.21's power-up value, a 2.4 ms half period, so a 208 Hz first guess the engine corrects (8.5.1.1) |
| CONTROL2 (0x1C) | 0xF5: BIDIR_INPUT, BRAKE_STABILIZER, SAMPLE_TIME 3, BLANKING_TIME 1, IDISS_TIME 1 | 8.6.22's power-up value, which 8.5.6 step 3 also asks |
| CONTROL3 (0x1D) | LRA_OPEN_LOOP and DATA_FORMAT_RTP cleared | 8.6.23: auto-resonance rather than a fixed period, and signed RTP data |
| CONTROL4 (0x1E) | 0x30: ZC_DET_TIME 0, AUTO_CAL_TIME 3, OTP_PROGRAM clear | 8.5.6 steps 3f and 3k. OTP_PROGRAM would burn the OTP once and for good (8.5.7) |
| CONTROL5 (0x1F) | 0x80: LRA_AUTO_OPEN_LOOP off | 8.6.25's power-up value |
| LIBRARY_SEL (0x03) | 6, the LRA library | 8.6.4. Real-time playback does not use it, but it leaves the part in a defined state. Meshtastic selects 1 |

Real-time playback sends signed 0x7F, which in bidirectional closed loop is full scale: the rated voltage at steady state, with overdrive and braking held under OD_CLAMP (8.5.8.1.3, Figure 28).

## Calibration

`calibrate()` runs the auto-calibration of 8.5.6 on an LRA that `begin()` brought up: MODE 7, GO set, GO polled every 10 ms until it clears, then DIAG_RESULT read from STATUS, which clears on read (8.6.1). It drives the motor for 1000 to 1200 ms (AUTO_CAL_TIME 3, 8.6.24), so only the smoke image calls it: on Bravo that buzz would be felt outside any call, and the motor is reserved for traffic (`../../../core/annunciation/README.md`). Bravo drives the results the part holds, as Meshtastic does, until #169 records a unit's results and the firmware can write them back at bring-up (8.5.6 step 6b).

`kCalibrationCeilingMs`, 1500 ms, bounds the poll. A GO still set past it is cleared and the calibration counts as failed. Standby, which `calibrate()` always ends in, stops the part at once (8.4.1.4). `calibration()` keeps what the part reported, A_CAL_COMP (0x18), A_CAL_BEMF (0x19) and BEMF_GAIN, even when it failed, so the smoke image can print it. A calibration that fails or hangs writes back the A_CAL_COMP, A_CAL_BEMF and FEEDBACK the part held before it, because A_CAL_BEMF only means something with its own BEMF_GAIN (8.6.19).

The model powers up with the register file of 8.6, Table 3, keeps OTP_STATUS read-only, flags any write of OTP_PROGRAM, and runs a calibration the moment GO is set in mode 7 with the part awake and enabled. A case can make it fail (`calibration_converges`) or hang (`calibration_hangs`).

## Names

The register constants are spelled in full. The register map follows SensorLib's `HapticDriver_DRV2605_Reg.hpp`, the vendor library SoftRF uses. Where our name differs from the reference's, this is the pair, the right column spelled as this tree's comments or earlier identifiers cited it.

| Our name | Reference name |
|---|---|
| `kRegisterRealTimePlaybackInput` (0x02) | RtpInput |
| `kRealTimePlaybackFullScale` | full scale in RTP mode |
| `kRegisterWaveSequence1`, `kRegisterWaveSequence2` (0x04, 0x05) | WaveSeq1, WaveSeq2 |
| `kRegisterAudioMaximumInput` (0x13) | AudioMax |
| `kRegisterRatedVoltage`, `kRegisterOverdriveClamp` (0x16, 0x17) | RATED_VOLTAGE, OD_CLAMP |
| `kRegisterCalibrationCompensation`, `kRegisterCalibrationBackEmf` (0x18, 0x19) | A_CAL_COMP, A_CAL_BEMF |
| `kStatusDiagnosticResult` | DIAG_RESULT |
