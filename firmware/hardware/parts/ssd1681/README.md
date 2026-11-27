# parts/ssd1681

The GDEH0154D67 e-paper on its SSD1681 controller, driven over `io::Spi` and `io::Gpio` by `Ssd1681`, with `model.h` standing in for the glass on the host. Which panel lot is glued on is `panel.h`'s question, answered from an ID the board port reads before any driver owns the pins (`hardware/README.md`).

## Names

The command constants are spelled in full. Where the datasheet's command table spells one differently, this is the pair.

| Our name | Datasheet name |
|---|---|
| `kSoftwareReset` (0x12) | SwReset |
| `kDisplayUpdateControl2` (0x22) | DisplayUpdateCtrl2 |
| `kSetRamXRange`, `kSetRamYRange` (0x44, 0x45) | SetRamXAddr, SetRamYAddr |

## A failed transfer

The panel answers nothing over SPI, so the driver learns of a lost byte only from the bus. A transfer the bus reports failed marks the frame. The driver then sends no MasterActivation, since the RAM holds part of a frame and the controller may be mid-command. The glass keeps the last frame it showed. The next frame re-initialises the panel from RES# and is a full refresh, because neither RAM bank is known.

## Panel identity

LilyGO fits more than one panel behind the same footprint and controller pinout, and they do not behave the same. SoftRF fingerprints them by clocking registers 0x2D (11 bytes) and 0x2E (10 bytes) out over a bit-banged half-duplex SPI, and lists the IDs it has seen on real T-Echos (`platform/nRF52.cpp:3775-3900`, table at 3855-3878). `panel.h` transcribes that table byte for byte, the two unnamed entries included.

No identification may refuse to drive the panel. GDEP015OC1 sits on a different controller and our init sequence is wrong for it, but its ID is all 0xFF, which an unwired read line also returns, and this board's e-paper MISO is a placeholder. Refusing on that read would blank the one page that can explain a dead device. An OC1 verdict is a procurement alarm on the self-test page.
