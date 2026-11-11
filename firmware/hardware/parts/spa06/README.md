# hardware/parts/spa06

The Goertek SPA06-003 barometer of the Seeed MeshTracker X1. A trigger commands one pressure measurement at 64 times oversampling and returns. A later pass sees the pressure in, reads it and commands one temperature measurement. A pass after that reads the temperature and compensates both. The sample is dated `kConversionMidpointMs`, 52 ms, after the trigger. It reaches `core/flight` as millipascals, on the same path as the BME280's (`hardware/README.md`, `core/flight/README.md`).

The source is the SPA06-003 datasheet, Ver2.0 of 2023-12-05, cited below as DS. Adafruit publishes it at <https://cdn-shop.adafruit.com/product-files/6420/spa06-003.PDF>, and Seeed's copy for its Grove module, <https://files.seeedstudio.com/wiki/Grove-Barometer_Sensor-SPA06-003/res/Datasheet.PDF>, is the same file, byte for byte. Goertek marks it confidential, so it is cited and not kept in the tree.

Nothing here has run on a unit. Every address, register, code, scale factor and timing is the datasheet's or SoftRF's, and carries a `TODO` naming #169, whose bench checks them. `boards/seeed/meshtracker_x1/board.h` triggers it once a second on the PPS edge for skyBlip Bravo.

## Why command mode

The part has three modes (DS 4.1). Background mode measures on the part's own timer, so a sample cannot be dated against PPS. Command mode starts when asked and returns to standby when done, which is what forced mode is on the BME280. It measures one type per command, so the pressure goes first and its temperature follows, the order background mode uses (DS 4.1).

Zephyr 4.4.2 has no SPA06 driver under `drivers/sensor`. The BME280 README says why we do not want a sensor-API fetch that sleeps through the conversion in the service loop anyway.

## Names

| Our name | Datasheet name |
|---|---|
| `kRegisterPressure` (0x00), `kRegisterTemperature` (0x03) | PSR_B2..B0, TMP_B2..B0 |
| `kRegisterPressureConfig` (0x06), `kRegisterTemperatureConfig` (0x07) | PRS_CFG, TMP_CFG |
| `kRegisterMeasurementConfig` (0x08), `kRegisterConfig` (0x09) | MEAS_CFG, CFG_REG |
| `kCommandIdle`, `kCommandPressure`, `kCommandTemperature` | MEAS_CTRL 000, 001, 010 |
| `kCoefficientsReady`, `kSensorReady`, `kTemperatureReady`, `kPressureReady` | COEF_RDY, SENSOR_RDY, TMP_RDY, PRS_RDY |
| `kConfigPressureShift` | P_SHIFT |
| `kPressureScale`, `kTemperatureScale` | kP, kT |
| `raw_pressure`, `raw_temperature` | Praw, Traw |
| `temperature_offset`, `temperature_slope` | c0, c1 |
| `pressure_terms[0]` to `[4]` | c00, c10, c20, c30, c40 |
| `temperature_terms[0]` to `[3]` | c01, c11, c21, c31 |

## The setting

| Figure | Value | Source |
|---|---|---|
| Address | 0x77, or 0x76 with SDO pulled down. The driver tries both, like the BME280's | DS 4.3.1. SoftRF `platform/nRF52.h:151` at d091924 puts the X1's at 0x77 |
| Chip id | 0x11 at 0x0D | DS 7.10 |
| Pressure | 64 times (PM_PRC 0110), with P_SHIFT set because it is over 8 times | DS 7.3, 7.6 |
| Temperature | single (TMP_PRC 0000), from the ASIC's sensor (TMP_EXT 0), as the datasheet recommends | DS 7.4, 7.5 |
| Scale factors | kP 1040384, kT 524288 | DS table 4 |
| Pressure measurement | 104.4 ms typical, 115 ms at most | DS table 8, table 2 |
| Temperature measurement | 3.6 ms | DS 7.4 |
| Pressure noise per conversion | 0.3 Pa RMS, not yet measured | DS table 8 |
| Current at one conversion a second | 48.1 µA for the pressure | DS table 9 |

64 times is the datasheet's high-precision mode, and its own sports use case (DS table 5, PRS_CFG 0x26). 128 times takes twice as long for the same 0.3 Pa (DS table 8). 32 times takes half as long for 0.4 Pa. The part has no IIR filter, so a conversion that is not read buys nothing: the Bravo board should trigger once a second, on the PPS edge, not four times as the T-Echo's does for the BME280's filter.

A conversion takes the pressure time, one pass, the temperature time and one more pass: about 104 + 10 + 3.6 + 10 ms with `runtime::kServicePassCeilingMs`, and about 140 ms if the pressure takes the datasheet's 115 ms. That fits `runtime::kBarometerConversionCeilingMs`, 150 ms, with little margin. Bravo keeps that ceiling until the bench measures the conversion: one that misses it is counted in the board's `barometer_faults()`, and the next second triggers again.

## Compensation

DS 4.6.1 and 4.6.2 are floating point. The driver keeps the tree's integer arithmetic: the pressure polynomial runs by Horner in Q16, one rounded division by kP per term, and every product stays under 2^61 for any 24-bit raw word. The temperature is `c0 / 2 + c1 * Traw / kT`, in tenths of a degree. The bounds the reading must fall inside are the BME280's.

Each rounded division is off by at most half a Q16 step, 1/131072 Pa, and every later step multiplies that error by the scaled pressure x = Praw / kP. With X = |x| and T = |Traw / kT|, the pressure polynomial carries at most 0.5 (1 + X + X² + X³) steps, the temperature polynomial 0.5 (1 + X + X²) steps times T, and its last division half a step more. The result then rounds to the millipascal. Where X and T stay under 1, that is under 0.07 mPa before the rounding. At the ends of the 24-bit range, X is 8.06 and T is 16, and the whole error, the rounding included, stays under 14.2 mPa. `test_spa06.cpp` checks the bound against the double reference at both.

The datasheet gives no worked example. The test oracle is `model.h`, which transcribes 4.6.1 in double precision as the datasheet writes it, and finds the raw word for a pressure by bisection, as the BME280 model does. The driver reads back what the model was given within half a raw count, 37 mPa. The coefficients are a fixture, because no unit's set is published. A second invented set flips every sign but c00's, because a negative offset is not air, and so checks the sign extension of the 12, 16 and 20-bit fields of DS table 10.

## Recovery

Every poll reads PRS_CFG to CFG_REG in one burst. A part that reset measures at 1x unshifted when next commanded, and nothing in its result says so. A configuration that is not ours, or a failed transfer, drops the part back to unconfigured, and the next trigger brings it up again first.

The ready flags clear when their result is read (DS 7.5). A trigger reads all six result bytes first, so a flag left by a conversion nobody read is not taken for the new one.

Another firmware may leave the part in background mode. Adafruit's driver, which SoftRF uses, starts it at 200 measurements a second. Bring-up writes MEAS_CTRL to idle first.

## Bench

These wait for #169's bench and a unit.

- **The facts:** the X1's part answers at 0x77 with id 0x11. The 21 coefficient bytes from 0x10 are recorded, and the model's fixture becomes that set. COEF_RDY and SENSOR_RDY are set within 4 ms and 2 ms of power-up (DS table 2).
- **Timing:** the time from the pressure command to PRS_RDY, logged over a few hundred conversions, against 104.4 ms typical and 115 ms at most. Reading a result clears its flag.
- **Noise:** as for the BME280. A unit at rest logs a minute or more of published pressures. The RMS of the one-second difference, divided by √2, is the noise per conversion, to set against the datasheet's 0.3 Pa. Two units side by side separate the room's air: the correlation between their one-second differences is the share no filter removes. The figure goes in the table above.
- **Lag:** the 1.8 m lift of the BME280 README, one unit lifted and the other on the ground as the room's reference. With no filter on the part, the reading should close on the new altitude within one published second. The figure goes in the table above.
- **Pressure altitude:** the Bravo's diagnostics show it, and it agrees with a reference barometer to within the datasheet's 0.3 hPa absolute accuracy (DS table 2).
