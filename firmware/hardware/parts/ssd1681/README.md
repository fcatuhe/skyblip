# parts/ssd1681

The GDEH0154D67 e-paper on its SSD1681 controller, driven over `io::Spi` and `io::Gpio` by `Ssd1681`, with `model.h` standing in for the glass on the host. Which panel lot is glued on is `panel.h`'s question, answered from a signature the board port reads before any driver owns the pins (`hardware/README.md`).

## Names

The command constants are spelled in full. Where the datasheet's command table words it differently, this is the pair, the right column spelled as this tree's earlier identifiers cited it.

| Our name | Datasheet name |
|---|---|
| `kSoftwareReset` (0x12) | SwReset |
| `kDisplayUpdateControl2` (0x22) | DisplayUpdateCtrl2 |
| `kSetRamXRange`, `kSetRamYRange` (0x44, 0x45) | SetRamXAddr, SetRamYAddr |
