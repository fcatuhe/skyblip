# parts/ssd1681

The GDEH0154D67 e-paper on its SSD1681 controller, driven over `io::Spi` and `io::Gpio` by `Ssd1681`, with `model.h` standing in for the glass on the host. Which panel lot is glued on is `panel.h`'s question, answered from a signature the board port reads before any driver owns the pins (`hardware/README.md`).

## Names

The command constants are spelled in full. Where the datasheet's command table words it differently, this is the pair.

| Our name | Datasheet name |
|---|---|
| `kSoftwareReset` (0x12) | SW RESET |
| `kWriteRamPrevious` (0x26) | Write RAM (RED), the bank a partial refresh compares against |
| `kDisplayUpdateControl2` (0x22) | Display Update Control 2 |
| `kBorderWaveform` (0x3C) | Border Waveform Control |
| `kSetRamXRange` (0x44) | Set RAM X address Start / End position |
| `kSetRamYRange` (0x45) | Set RAM Y address Start / End position |
| `kSetRamXCounter` (0x4E) | Set RAM X address counter |
| `kSetRamYCounter` (0x4F) | Set RAM Y address counter |
| `kDeepSleep` (0x10) | Deep Sleep mode |
