# parts/ssd1681

The GDEH0154D67 e-paper on its SSD1681 controller, driven over `io::Spi` and `io::Gpio` by `Ssd1681`, with `model.h` standing in for the glass on the host. Which panel lot is glued on is `panel.h`'s question, answered from an ID the board port reads before any driver owns the pins (`hardware/README.md`).

## Names

The command constants are spelled in full. Where the datasheet's command table spells one differently, this is the pair.

| Our name | Datasheet name |
|---|---|
| `kSoftwareReset` (0x12) | SwReset |
| `kDisplayUpdateControl2` (0x22) | DisplayUpdateCtrl2 |
| `kSetRamXRange`, `kSetRamYRange` (0x44, 0x45) | SetRamXAddr, SetRamYAddr |

## Panel identity

LilyGO fits more than one panel behind the same footprint and controller pinout, and they do not behave the same. SoftRF fingerprints them by clocking registers 0x2D (11 bytes) and 0x2E (10 bytes) out over a bit-banged half-duplex SPI, and lists the IDs it has seen on real T-Echos (`platform/nRF52.cpp:3775-3900`, table at 3855-3878). `panel.h` transcribes that table byte for byte, the two unnamed entries included.

No identification may refuse to drive the panel. GDEP015OC1 sits on a different controller and our init sequence is wrong for it, but its ID is all 0xFF, which an unwired read line also returns, and this board's e-paper MISO is a placeholder. Refusing on that read would blank the one page that can explain a dead device. An OC1 verdict is a procurement alarm on the self-test page.

## Re-ink

A partial refresh drives only the pixels that change, from the previous bank (0x26) to the new one (0x24). A pixel left alone gets no drive and greys a little on every refresh. So one partial in `kReinkPeriodPartials`, 60, writes 0x26 as the inverse of 0x24, which drives every pixel to its colour on the same partial waveform, with no flash. The count is of frames presented, not of seconds: `ScreenService` presents a frame only when it differs, at most once a second. A full refresh and a wipe to black start the count over. `Refresh::Redrive` asks for the same re-ink at once. `ScreenService` asks for it `kRedriveAfterChangeMs`, 10 s, after the page that follows a wipe, because that page's pixels still settle for seconds after its partial, even on a page where nothing changes.
