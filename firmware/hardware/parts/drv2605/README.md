# parts/drv2605

The TI DRV2605 haptic driver at I2C 0x5A, with its enable line on a GPIO, driven by `Drv2605` and stood in for by `model.h` on the host. Why the T-Echo Plus pin is an enable and not a motor drive is the header's opening note.

## Names

The register constants are spelled in full. The register map follows SensorLib's `HapticDriver_DRV2605_Reg.hpp`, the vendor library SoftRF uses. Where our name differs from the reference's, this is the pair, the right column spelled as this tree's comments or earlier identifiers cited it.

| Our name | Reference name |
|---|---|
| `kRegisterRealTimePlaybackInput` (0x02) | RtpInput |
| `kRealTimePlaybackFullScale` | full scale in RTP mode |
| `kRegisterWaveSequence1`, `kRegisterWaveSequence2` (0x04, 0x05) | WaveSeq1, WaveSeq2 |
| `kRegisterAudioMaximumInput` (0x13) | AudioMax |
