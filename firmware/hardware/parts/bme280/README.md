# hardware/parts/bme280

The barometer converts four times a second in forced mode behind the part's IIR 4, triggered on the PPS phases 0, 250, 500 and 750 ms and read once a second on a later pass. Consumption matters most, and every way of asking Zephyr's driver for a sample sleeps through the conversion in the service loop.

Its address is disputed: our BOM says 0x76, LilyGO's README and SoftRF say 0x77. `Bme280::kAddressPrimary` and `kAddressAlternate` are both, the devicetree declares both, and the board takes whichever answers.

## Why not Zephyr's driver

Zephyr 4.4.2's `bme280_sample_fetch_helper` (`drivers/sensor/bosch/bme280/bme280.c:164-213`) writes `ctrl_meas` in forced mode, `k_sleep`s the expected sample time, then polls the status register every 3 ms. At x16 / x2 / x1 that cost the service loop 43 ms at every PPS edge on the bench. The async sensor API runs the same helper on an RTIO work-queue thread, a 1 KB stack and a thread to stop a sleep we do not need.

The driver carries the compensation instead, transcribed from DS 4.2.3 rev 1.1, with each left shift on a signed term written as a multiply. Its test oracle is the model, which builds raw words from DS 8.1's double-precision formulas, not the integer ones. The two Bosch paths part by up to 13 `fine_temperature` counts at -20 °C, about 0.45 Pa, which is the test's tolerance.

## Names

The compensation uses the datasheet's names in full words:

| Our name | Datasheet name |
|---|---|
| `Trim::temperature_1` to `temperature_3`, the model's `kTemperatureTrim1` to `kTemperatureTrim3` | t1 to t3 |
| `Trim::pressure_1` to `pressure_9`, the model's `kPressureTrim1` to `kPressureTrim9` | p1 to p9 |
| `raw_temperature`, `raw_pressure` | adc_t, adc_p |
| `fine_temperature`, `fine_temperature_from()` | t_fine |
| `first_term`, `second_term` | var1, var2 |
| `kRegisterControlMeasurement` | ctrl_meas (0xF4) |
| `kRegisterControlHumidity` | CtrlHum (0xF2) |

## The setting

Forced mode, four conversions a second (`runtime::kBarometerConversionPeriodMs`): pressure x16, temperature x2, humidity skipped because nothing reads it, IIR filter 4 (`kFilter4`, config code 0b010). Standby does not apply in forced mode. The filter advances on every forced conversion, so the three unread conversions of each second feed the published one.

| Figure | Value | Source |
|---|---|---|
| Measurement time | 37.5 ms typical, 43.2 ms at most | DS 9.1: 1 + 2 x 2 + (2 x 16 + 0.5) |
| Pressure phase midpoint | 21.25 ms after the trigger | 1 + 2 x 2 + (2 x 16 + 0.5) / 2 |
| Raw noise per conversion | 1.75 Pa | build 895 at 4 / 4: 0.75 and 0.79 Pa on a one-second difference |
| IIR delay | 3 conversions, 0.75 s | coefficient 4 at 4 Hz |
| Noise on a one-second difference | 0.77 Pa, 6.4 cm/s of climb | derivation below |
| Current | about 0.1 mA | DS table 12: 24.9 µA per conversion a second, x16 / x2 |

The derivation, for coefficient c at f conversions a second and raw noise σ. The filter output carries σ·√(1/c / (2 − 1/c)), 0.66 Pa at c = 4. Two outputs a second apart correlate by (1 − 1/c)^f, 0.32 at f = 4. Their difference carries √(2(1 − that)) of the output, 0.77 Pa, which at 8.26 cm/Pa is 6.4 cm/s.

| Conversions a second / IIR | Filter delay | Climb noise | Still seconds off zero in G.1.9 | Current |
|---|---|---|---|---|
| 1 / off | none | 20 cm/s | 75 % | 25 µA |
| 1 / 4, build 892, measured | 3 s | 3.5-3.7 cm/s | 8-10 % | 25 µA |
| 2 / 4 | 1.5 s | 5.1 cm/s | 22 % | 50 µA |
| 4 / 8, build 896, measured | 1.75 s | 4.3 cm/s, 3.7 of it the part's | 13 % | 0.1 mA |
| **4 / 4, build 895, measured** | **0.75 s** | **6.2 and 6.6 cm/s** | **32 %** | **0.1 mA** |
| 26 / 16, normal mode (DS table 9) | 0.57 s | 3.3 cm/s | 6 % | 0.65 mA |

4 / 4 is the owner's pick: it reacts faster than 4 / 8, which reached 75 % of a 1.8 m lift about 2.6 s after it began where 4 / 4 takes 1.2 s, at the cost of noise the page's 10 ft/min step absorbs. About 0.27 Pa of the bench noise is the room's air, which no filter removes without lag. 1 / off reacts no quicker once `IndicatedRate`'s 2 s damping sits on top, at three times the noise. 26 / 16 is the only setting both quicker and quieter, at 6.5 times the current.

Reading the three unpublished conversions buys nothing while the chip's filter is the estimator: their effect is already in the published one. It would cost a status poll per pass and a burst each.

Getting past the trade between lag and noise means turning the chip's filter off, reading all four conversions and running a software Kalman filter in `core/flight`, as XCSoar and OpenVario do. Nobody has measured it here.

## Timing

Each trigger is one write of `ctrl_meas`, and the pass returns.

- On PPS lock the board triggers on the PPS-window pass and on the first pass into each later quarter of the second. Without lock, it triggers every 250 ms from the last trigger, and the published one falls on the board's own second.
- A trigger waits for the previous conversion to be read or given up.
- Only the edge conversion is read. Each later pass reads three bytes from 0xF3 (status, `ctrl_meas`, config) and bursts 0xF7..0xFC once `measuring` and `im_update` are clear, about 40 ms in, well before the next trigger overwrites the result registers.
- The sample is dated `kConversionMidpointMs`, 21 ms, after the edge trigger, the same offset from PPS every second.
- A published conversion not read within `runtime::kBarometerConversionCeilingMs`, 150 ms like Zephyr's `BME280_MEASUREMENT_TIMEOUT_MS`, is given up and counted in `barometer_faults()`, as is any trigger the part refused. An unpublished one is deemed done at the same ceiling, unread.

## Recovery

Every trigger rewrites `ctrl_meas`, so a part that reset still converts at our oversampling. A reset loses `config`, and with it the IIR filter, and nothing in the result says so. Writing `config` on every trigger would reset the filter each second (DS 3.4.4), so the driver reads it back with the status byte instead. A `config` that is not ours, a failed transfer, or the 0x80000 of a measurement that never ran drops the part back to unconfigured. The next trigger brings it up again first: that second reports nothing, and the filter starts over.
