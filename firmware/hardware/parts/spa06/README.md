# hardware/parts/spa06

The Goertek SPA06-003 barometer of the Seeed MeshTracker X1. A trigger commands one pressure measurement at 64 times oversampling and returns. A later pass sees the pressure in, reads it and commands one temperature measurement. A pass after that reads the temperature and compensates both. The sample is dated `kConversionMidpointMs`, 52 ms, after the trigger. It reaches `core/flight` as millipascals, on the same path as the BME280's (`hardware/README.md`, `core/flight/README.md`).

The source is the SPA06-003 datasheet, Ver2.0 of 2023-12-05, cited below as DS, at <https://cdn-shop.adafruit.com/product-files/6420/spa06-003.PDF> (Seeed's copy is the same file). Goertek marks it confidential, so it is cited and not kept in the tree.

Nothing here has run on a unit. Every address, register, code, scale factor and timing is the datasheet's or SoftRF's, and carries a `TODO` naming #169, whose bench checks them. `boards/seeed/meshtracker_x1/board.h` triggers it once a second on the PPS edge for skyBlip Bravo.

## Why command mode

Background mode (DS 4.1) measures on the part's own timer, so a sample cannot be dated against PPS. Command mode starts when asked and returns to standby when done, like the BME280's forced mode. It measures one type per command, so the pressure goes first and its temperature follows, the order background mode uses.

Zephyr 4.5.0 has no SPA06 driver, and a sensor-API fetch would sleep through the conversion in the service loop anyway ([`../bme280/README.md`](../bme280/README.md)).

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

64 times is the datasheet's high-precision mode and its sports use case (DS table 5, PRS_CFG 0x26). 128 times takes twice as long for the same 0.3 Pa (DS table 8), and 32 times takes half as long for 0.4 Pa. The part has no IIR filter, so a conversion that is not read buys nothing: trigger once a second, on the PPS edge.

A conversion takes the pressure time, one pass, the temperature time and one more pass: about 104 + 10 + 3.6 + 10 ms with `runtime::kServicePassCeilingMs`, and about 140 ms if the pressure takes 115 ms. That fits `boards::kBarometerConversionCeilingMs`, 150 ms, with little margin. A conversion that misses it is counted in the board's `barometer_faults()`, and the next second triggers again.

## Compensation

DS 4.6.1 and 4.6.2 are floating point. The driver keeps the tree's integer arithmetic: the pressure polynomial runs by Horner in Q16, one rounded division by kP per term, and every product stays under 2^61 for any 24-bit raw word. The temperature is `c0 / 2 + c1 * Traw / kT`, in tenths of a degree. The bounds the reading must fall inside are the BME280's.

Each rounded division is off by at most half a Q16 step, 1/131072 Pa, and every later step multiplies that error by the scaled pressure x = Praw / kP. With X = |x| and T = |Traw / kT|, the pressure polynomial carries at most 0.5 (1 + X + X² + X³) steps, the temperature polynomial 0.5 (1 + X + X²) steps times T, and its last division half a step more. The result then rounds to the millipascal. Where X and T stay under 1, that is under 0.07 mPa before the rounding. At the ends of the 24-bit range, X is 8.06 and T is 16, and the whole error stays under 14.2 mPa.

The datasheet gives no worked example, so the test oracle is `model.h`, which transcribes 4.6.1 in double precision. The coefficients are a fixture, because no unit's set is published.

## Recovery

Every poll reads PRS_CFG to CFG_REG in one burst. A part that reset measures at 1x unshifted when next commanded, and nothing in its result says so. A configuration that is not ours, or a failed transfer, drops the part back to unconfigured, and the next trigger brings it up again first.

The ready flags clear when their result is read (DS 7.5). A trigger reads all six result bytes first, so a flag left by a conversion nobody read is not taken for the new one.

Bring-up writes MEAS_CTRL to idle first, because another firmware may leave the part in background mode: Adafruit's driver, which SoftRF uses, starts it at 200 measurements a second.

## Bench

These wait for #169's bench and a unit. Put each figure in the table above.

- **The facts:** the X1's part answers at 0x77 with id 0x11. Record the 21 coefficient bytes from 0x10 and make them the model's fixture. COEF_RDY and SENSOR_RDY are set within 4 ms and 2 ms of power-up (DS table 2).
- **Timing:** log the time from the pressure command to PRS_RDY over a few hundred conversions, against 104.4 ms typical and 115 ms at most.
- **Noise:** log a minute or more of published pressures on a unit at rest. The RMS of the one-second difference, divided by √2, is the noise per conversion, against the datasheet's 0.3 Pa. The correlation between two units side by side is the room's share, which no filter removes.
- **Lag:** lift one unit 1.8 m with the other on the ground as the reference. With no filter on the part, the reading should close on the new altitude within one published second.
- **Pressure altitude:** it agrees with a reference barometer within the datasheet's 0.3 hPa absolute accuracy (DS table 2).
