# hardware/parts/bme280

The barometer converts four times a second in forced mode behind the part's IIR 8, triggered on the PPS phases 0, 250, 500 and 750 ms and read once a second on a later pass, because consumption matters most and every way of asking Zephyr's driver for a sample sleeps through the conversion in the service loop.

## Why not Zephyr's driver

Zephyr 4.4.2's `bme280_sample_fetch_helper` (`drivers/sensor/bosch/bme280/bme280.c:164-213`) writes `ctrl_meas` in forced mode and `k_sleep`s `BME280_EXPECTED_SAMPLE_TIME_MS`, then polls the status register every 3 ms. At x16 / x2 / x1 that was 43 ms of the service loop at every PPS edge (bench loop record, worst tick 43 ms in `board_poll`, phase 950-999 ms). The async sensor API runs the same helper on an RTIO work-queue thread, which buys a 1 KB stack and a thread to stop a sleep we do not need.

What the driver costs instead is the compensation, transcribed from DS 4.2.3 rev 1.1 with each left shift on a signed term written as a multiply. Its oracle in `test/hardware/test_bme280.cpp` is the model, which builds raw words by searching DS 8.1's double-precision formulas, not the integer ones. The two Bosch paths part by up to 13 `t_fine` counts at -20 °C, about 0.45 Pa, which is the test's tolerance and a bias rather than jitter.

## The setting

Forced mode, four conversions a second (`runtime::kBaroConversionPeriodMs`): pressure x16, temperature x2, humidity skipped because nothing reads it, IIR filter 4 (`kFilter4`, config code 0b010). Standby does not apply in forced mode. The filter memory advances on every forced conversion, so the three unread conversions of each second feed the one that is published.

| Figure | Value | Source |
|---|---|---|
| Measurement time | 37.5 ms typical, 43.2 ms at most | DS 9.1: 1 + 2 x 2 + (2 x 16 + 0.5) |
| Pressure phase midpoint | 21.25 ms after the trigger | 1 + 2 x 2 + (2 x 16 + 0.5) / 2 |
| Raw noise per conversion | 1.75 Pa | build 895 at 4 / 4: 0.75 and 0.79 Pa on a one-second difference |
| IIR delay | 3 conversions, 0.75 s | coefficient 4 at 4 Hz |
| Noise on a one-second difference | 0.77 Pa, 6.4 cm/s of climb | derivation below |
| Current | about 0.1 mA | DS table 12: 24.9 µA per conversion a second, x16 / x2 |

The derivation, for coefficient c at f conversions a second and raw noise σ. The filter output carries σ·√(1/c / (2 − 1/c)), 0.66 Pa at c = 4. Two outputs a second apart correlate by (1 − 1/c)^f, 0.32 at f = 4. Their difference carries √(2(1 − that)) of the output, 0.77 Pa, which at 8.26 cm/Pa is 6.4 cm/s. The raw noise comes out of the same arithmetic run backwards on a bench: 1.60 to 1.67 Pa from build 892's six captures at 1 / 4, 1.70 and 1.80 Pa from build 895's two at 4 / 4. The 4 / s figure is the one used for 4 / s settings.

| Conversions a second / IIR | Filter delay | Climb noise | Still seconds off zero in G.1.9 | Current |
|---|---|---|---|---|
| 1 / off | none | 20 cm/s | 75 % | 25 µA |
| 1 / 4, build 892, measured | 3 s | 3.5-3.7 cm/s | 8-10 % | 25 µA |
| 2 / 4 | 1.5 s | 5.1 cm/s | 22 % | 50 µA |
| 4 / 8, build 896, measured | 1.75 s | 4.3 cm/s, 3.7 of it the part's | 13 % | 0.1 mA |
| **4 / 4, build 895, measured** | **0.75 s** | **6.2 and 6.6 cm/s** | **32 %** | **0.1 mA** |
| 26 / 16, normal mode (DS table 9) | 0.57 s | 3.3 cm/s | 6 % | 0.65 mA |

Build 895 ran 4 / 4 and the noise was the prediction's: 0.75 and 0.80 Pa on a one-second difference, a quarter of still seconds showing ±10 ft/min, a third broadcasting a non-zero G.1.9 rate. Build 896 tried 4 / 8 for the noise at the same current, and kept the lag and not enough of the quiet. Two units at rest side by side read 0.51 and 0.52 Pa on a one-second difference, correlated by 0.27 between them: about 0.27 Pa of that was the room's air, which no filter setting removes without lag, and 0.44 Pa the part's, as predicted. The page still showed ±10 ft/min in 18 to 20 % of still seconds, against a quarter at 4 / 4. A 1.8 m lift of one unit, the other on the ground as the room's reference, closed on the new altitude by 0.60 a second, IIR 8's 0.59 and not IIR 4's 0.32, and reached 75 % of it about 2.6 s after the lift began, where 4 / 4 takes 1.2 s. The owner kept the reaction over the quiet and went back to 4 / 4 at the page's 10 ft/min step. 1 / off reacts no quicker than 4 / 4 once `IndicatedRate`'s 2 s damping is on top, at three times the noise. 26 / 16 is still the only setting both quicker and quieter, at 6.5 times the current.

Reading the three unpublished conversions buys nothing a consumer sees while the chip's filter is the estimator: their effect is already in the edge conversion, and the board publishes one sample a second. It would cost a status poll per pass and a burst each, for a reset noticed within 250 ms instead of within a second.

## Next

The filter delay and the noise are one trade as long as the part's IIR is the estimator. The way past it is to read all four conversions with the chip's filter off and run a software Kalman on pressure, position and rate, as XCSoar and OpenVario do: the rate becomes a state rather than a difference of two smoothed values, and its lag follows the noise model rather than a fixed coefficient. On units with the BHI260, fusing its vertical acceleration into that filter, as har-in-air's ESP32 vario does, carries the rate through the first second of a climb that pressure alone only shows a second later. Both cost bus traffic on every conversion and code in `core/flight`, and neither has been measured here.

## Timing

Each trigger is one write of `ctrl_meas` and the pass returns. On PPS lock the board triggers on the PPS-window pass and on the first pass into each later quarter of the second. Without lock, it triggers every 250 ms from the last trigger, with the published one on the board's own second. A trigger waits for the previous conversion to be read or given up. Only the edge conversion is read: each later pass reads three bytes from 0xF3 (status, `ctrl_meas`, config) and bursts 0xF7..0xFC once `measuring` and `im_update` are clear, which is about 40 ms in, well before the 250 ms trigger overwrites the result registers. The sample is dated `kConversionMidpointMs`, 21 ms, after the edge trigger: the same offset from PPS every second, one sample a second as before. A published conversion not read within `runtime::kBaroConversionCeilingMs`, 150 ms like Zephyr's `BME280_MEASUREMENT_TIMEOUT_MS`, is given up and counted in `baro_faults()`, as is any trigger the part refused. An unpublished one is deemed done at the same ceiling, unread.

## Recovery

Every trigger rewrites `ctrl_meas`, so a part that reset still converts at our oversampling. What a reset loses is `config`, and with it the IIR filter: the conversion goes on, unfiltered, and nothing in the result says so. Writing `config` on every trigger would reset the filter each second (DS 3.4.4), so the driver reads it back with the status byte instead. A `config` that is not ours, a failed transfer, or the 0x80000 of a measurement that never ran drops the part back to unconfigured, and the next trigger brings it up again first: that second reports nothing, and the filter starts over from the next conversion.
