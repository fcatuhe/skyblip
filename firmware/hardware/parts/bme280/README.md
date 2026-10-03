# hardware/parts/bme280

The barometer converts four times a second in forced mode, triggered on the PPS phases 0, 250, 500 and 750 ms and read once a second on a later pass, because consumption matters most and every way of asking Zephyr's driver for a sample sleeps through the conversion in the service loop.

## Why not Zephyr's driver

Zephyr 4.4.2's `bme280_sample_fetch_helper` (`drivers/sensor/bosch/bme280/bme280.c:164-213`) writes `ctrl_meas` in forced mode and `k_sleep`s `BME280_EXPECTED_SAMPLE_TIME_MS`, then polls the status register every 3 ms. At x16 / x2 / x1 that was 43 ms of the service loop at every PPS edge (bench loop record, worst tick 43 ms in `board_poll`, phase 950-999 ms). The async sensor API runs the same helper on an RTIO work-queue thread, which buys a 1 KB stack and a thread to stop a sleep we do not need.

What the driver costs instead is the compensation, transcribed from DS 4.2.3 rev 1.1 with each left shift on a signed term written as a multiply. Its oracle in `test/hardware/test_bme280.cpp` is the model, which builds raw words by searching DS 8.1's double-precision formulas, not the integer ones. The two Bosch paths part by up to 13 `t_fine` counts at -20 °C, about 0.45 Pa, which is the test's tolerance and a bias rather than jitter.

## The setting

Forced mode, four conversions a second (`runtime::kBaroConversionPeriodMs`): pressure x16, temperature x2, humidity skipped because nothing reads it, IIR filter 4. Standby does not apply in forced mode. The filter memory advances on every forced conversion, so the three unread conversions of each second feed the one that is published.

| Figure | Value | Source |
|---|---|---|
| Measurement time | 37.5 ms typical, 43.2 ms at most | DS 9.1: 1 + 2 x 2 + (2 x 16 + 0.5) |
| Pressure phase midpoint | 21.25 ms after the trigger | 1 + 2 x 2 + (2 x 16 + 0.5) / 2 |
| Raw noise per conversion | 1.65 Pa | bench, from 0.44 Pa on a one-second difference behind IIR 4 at 1 Hz |
| IIR delay | 3 conversions, 0.75 s | coefficient 4 at 4 Hz |
| Noise on a one-second difference | 0.73 Pa, 6.0 cm/s of climb | derivation below |
| Current | about 0.1 mA | DS table 12: 24.9 µA per conversion a second, x16 / x2 |

The derivation, for coefficient c at f conversions a second and raw noise σ. The filter output carries σ·√(1/c / (2 − 1/c)), 0.62 Pa at c = 4. Two outputs a second apart correlate by (1 − 1/c)^f, 0.32 at f = 4. Their difference carries √(2(1 − that)) of the output, 0.73 Pa, which at 8.2 cm/Pa is 6.0 cm/s. The same arithmetic at 1 Hz gives 0.44 Pa and 8 % of still seconds off zero in ADS-L's 0.125 m/s unit, which is what the bench measured (0.44 to 0.45 Pa, 9 %).

| Conversions a second / IIR | Filter delay | Climb noise | Still seconds off zero in G.1.9 | Current |
|---|---|---|---|---|
| 1 / 4 (build 892, measured) | 3 s | 3.6 cm/s | 8 % | 25 µA |
| 2 / 4 | 1.5 s | 4.8 cm/s | 19 % | 50 µA |
| 4 / 8 | 1.75 s | 3.2 cm/s | 5 % | 0.1 mA |
| **4 / 4** | **0.75 s** | **6.0 cm/s** | **29 %** | **0.1 mA** |
| 26 / 16, normal mode (DS table 9) | 0.57 s | 3.1 cm/s | 4 % | 0.65 mA |

Four a second behind IIR 4 is the owner's choice: a quarter of the lag for four times a current that is still a quarter of a percent of the device's, and a noise that stays under one G.1.9 step. The price is that a still aircraft broadcasts ±0.125 m/s in about three seconds out of ten, where 1 Hz did in one out of twelve. 26 / 16 is the only setting both quicker and quieter, at 6.5 times the current.

Reading the three unpublished conversions buys nothing a consumer sees: their effect is already in the edge conversion through the filter, and the board publishes one sample a second. It would cost a status poll per pass and a burst each, for a reset noticed within 250 ms instead of within a second.

## Timing

Each trigger is one write of `ctrl_meas` and the pass returns. On PPS lock the board triggers on the PPS-window pass and on the first pass into each later quarter of the second. Without lock, it triggers every 250 ms from the last trigger, with the published one on the board's own second. A trigger waits for the previous conversion to be read or given up. Only the edge conversion is read: each later pass reads three bytes from 0xF3 (status, `ctrl_meas`, config) and bursts 0xF7..0xFC once `measuring` and `im_update` are clear, which is about 40 ms in, well before the 250 ms trigger overwrites the result registers. The sample is dated `kConversionMidpointMs`, 21 ms, after the edge trigger: the same offset from PPS every second, one sample a second as before. A published conversion not read within `runtime::kBaroConversionCeilingMs`, 150 ms like Zephyr's `BME280_MEASUREMENT_TIMEOUT_MS`, is given up and counted in `baro_faults()`, as is any trigger the part refused. An unpublished one is deemed done at the same ceiling, unread.

## Recovery

Every trigger rewrites `ctrl_meas`, so a part that reset still converts at our oversampling. What a reset loses is `config`, and with it the IIR filter: the conversion goes on, unfiltered, and nothing in the result says so. Writing `config` on every trigger would reset the filter each second (DS 3.4.4), so the driver reads it back with the status byte instead. A `config` that is not ours, a failed transfer, or the 0x80000 of a measurement that never ran drops the part back to unconfigured, and the next trigger brings it up again first: that second reports nothing, and the filter starts over from the next conversion.
