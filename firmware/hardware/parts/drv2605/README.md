# parts/drv2605

The TI DRV2605 haptic driver at I2C 0x5A, with its enable line on a GPIO, driven by `Drv2605` and stood in for by `model.h` on the host. Why the T-Echo Plus pin is an enable and not a motor drive is the header's opening note.

## Names

The register constants are spelled in full. The register map follows SensorLib's `HapticDriver_DRV2605_Reg.hpp`, the vendor library SoftRF uses, so its names are the ones a reader searches for.

| Our name | SensorLib name |
|---|---|
| `kRegisterRealTimePlaybackInput` (0x02) | DRV2605_REG_RTPIN |
| `kRealTimePlaybackFullScale` | the RTP_INPUT value 0x7F, full scale signed |
| `kRegisterWaveSequence1`, `kRegisterWaveSequence2` (0x04, 0x05) | DRV2605_REG_WAVESEQ1, DRV2605_REG_WAVESEQ2 |
| `kRegisterSustainPositive`, `kRegisterSustainNegative` (0x0E, 0x0F) | DRV2605_REG_SUSTAINPOS, DRV2605_REG_SUSTAINNEG |
| `kRegisterAudioMaximumInput` (0x13) | DRV2605_REG_AUDIOMAX |
| `kAudioMaximumInputDefault` | the 0x64 the reference bring-up writes there |
