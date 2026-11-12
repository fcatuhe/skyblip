# parts/lr2021

The Semtech LR2021 on skyBlip Bravo, a Seeed SenseCAP MeshTracker X1, receiving 1090 MHz Extended Squitter full time with its OOK modem. `Lr2021` drives it over `io::Spi` and `io::Gpio`, and `model.h` stands in for the chip on the host. The driver receives and never transmits.

## Sources

| Source | What it gives | How it is used |
|---|---|---|
| Semtech, LR2021/LR2022/LR2012 Datasheet, Final Rev. 2.1, 13 April 2026 (DS), read from [Digi-Key's copy](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/8924/600_62785538.LR20xxDatasheet_V2_1.pdf) | every opcode, argument layout, the status word, reset, BUSY, the FIFO, the IRQ bits, and the ADS-B detector settings of 16.3.8 | cited as `DS` and a section |
| RadioLib at commit `b087af9` (8.0.0-6-gb087af9, six commits past the 8.0.0 tag), `src/modules/LR2021` and `src/modules/LR11x0/LR_common.cpp` (MIT) | how a read takes two frames, the bring-up order, the SetSleep layout and settle, the boot and command BUSY ceilings | followed, listed in `NOTICE.md` |
| SoftRF, commit `d091924`, `driver/radio/radiolib.cpp:2232-2359` and `platform/iomap/Seeed_X1.h` (GPL-3.0) | the 1090 MHz modem values, the register write at 0xF30150, the X1's TCXO voltage, its DIO8 IRQ and its pins | read for facts, no code copied |
| Semtech USP, commit `db5bbb0`, `lr20xx_driver/src/lr20xx_workarounds.c`, `lr20xx_system.c` and `lr20xx_radio_common.c` | the OOK threshold field (the threshold in dB plus 10 plus 64), the SetSleep and CalibFE layouts | facts, no code copied |

SoftRF drives the LR2021 through its own copy of RadioLib 7.7.1, which takes floats in MHz and kHz where `b087af9` takes integers. Where the two differ, this driver sends the bytes SoftRF's copy put on the wire, because SoftRF is the build that has received ADS-B on this hardware.

## What the driver sends

`begin()` resets the part, holding NRESET low for 100 us (DS 4.2), and waits for BUSY to fall. It then reads the version, which must be 1.24 (DS 6.7.2): BUSY falling proves a pull-down and a rail, and only a value read back proves the part. Then, in this order:

| Command | Arguments | Source |
|---|---|---|
| SetStandby | STDBY_RC | DS 6.3.2 |
| GetErrors, then ClearErrors if any | | RadioLib `setTCXO()` |
| SetTcxoMode | 1.6 V (tune 0x00), start time 32765 | SoftRF `radiolib.cpp:2133`, DS 6.11.3 |
| SetRxTxFallbackMode | STDBY_RC | RadioLib `config()`, DS 6.3.7 |
| ClearIrq | all | RadioLib `config()` |
| SetDioFunction | the IRQ DIO, IRQ, pull-down in sleep | RadioLib `config()`, DS 6.8.1 |
| Calibrate | 0x6F, every block | RadioLib `config()`, DS 6.4.1 |
| SetPacketType | OOK | DS 8.1.1 |
| SetOokModulationParams | 2 Mbps, no shaping, rx_bw 0 (3076 kHz), full depth | SoftRF `:2250-2281`, DS 16.3.1, table 11-4 |
| SetOokPacketParams | 16 chips of preamble, fixed length, 27 bytes, no CRC, no Manchester | the 16 of SoftRF's `beginOOK()` at `:2237`, its `setCRC(0, 0)` at `:2283-2290` and its fixed length at `:2314-2324`, DS 16.3.2 |
| SetOokWhiteningParams | polynomial 0, off | SoftRF's `setEncoding(RADIOLIB_ENCODING_NRZ)` at `:2319`, which its RadioLib 7.7.1 copy turns into `setWhitening(false)` and this command (`LR2021_config.cpp:756-760` and `:812-822`). The direct `setWhitening(false)` at `:2323` is commented out, but the command is still sent. DS 16.3.9 |
| SetOokSyncword | 0x95, 8 bits, MSB first | SoftRF `:2336-2337`, DS 16.3.4 |
| SetOokDetector | pattern 0x0285, 16 chips, no repeat, falling edge, no SFD | SoftRF `:2338`, DS 16.3.8 |
| SetAgcGainManual | 0, the AGC | SoftRF `:2339`, DS 7.3.9 |
| WriteRegMemMask32 | 0xF30E14, mask 0x07F00000, the threshold | SoftRF `:2350`, Semtech's `lr20xx_workarounds.c` for the register and the field, DS 22.1 for writing it after SetOokModulationParams |
| WriteRegMemMask32 | 0xF30150, mask 0x000000FF, data 0x18 | SoftRF `:2359` |
| SetRxPath | LF, no boost | SoftRF `:2655` |
| CalibFE | 273 on the LF path, then two zero words, 6 bytes | SoftRF `:1993` through its RadioLib copy's `setFrequency()`, which rounds 1090 MHz to the nearest 4 MHz and sends three words. DS 6.4.2 calibrates only the non-zero ones |
| SetRfFrequency | 1 090 000 000 Hz | DS 7.3.1 |
| GetErrors | | DS 6.7.4 |

`start_receive()` sends SetRxPath, SetDioIrqConfig with RxDone alone on the IRQ DIO, ClearIrq and SetRx with timeout 0xFFFFFF, which is continuous receive (DS 6.3.5): the part keeps listening after each frame without another command.

DS 16.3.8 gives the ADS-B detector settings itself. Pattern 0x0285 read first bit first is `1010000101000000`, the 8 us preamble at two chips a microsecond, with its pulses at 0, 1, 3.5 and 4.5 us. The sync word 0x95 is the chips of the bits `1000` that open DF16 and DF17, so the FIFO holds the 27 chip bytes after it, which is what `protocol::adsb_receive` takes (`core/protocol/README.md`).

## Reading a frame

Each frame opens on two status bytes, and their command status is the result of the command before (DS 6.7.1). The driver counts a CMD_FAIL or a CMD_PERR there as a `command_error`. It does not refuse to run on one, because SoftRF ignores every RadioLib return on this path and still receives. A read is two frames: the command, then zeros that clock the answer out behind the status (DS 5.4.1.2). The FIFO read is the one exception and answers in its own frame (DS 5.4.1.3).

On the IRQ the driver takes a snapshot: GetAndClearIrqStatus, GetOokPacketStatus, GetRxFifoLevel, GetAndClearFifoIrqFlags and GetOokPacketStatus again. It then reads one 27-byte frame from the FIFO per `poll()`, as many whole frames as the level held, oldest first. Frames keep landing between those commands, and the driver holds three rules about them:

- **An overflow stops the reading.** Ten frames fill the 256-byte FIFO with nine and 13 bytes of the tenth, and every later read would start 13 bytes off. The overflow flag latches whether or not its IRQ is enabled (DS 6.10.1, 6.10.2 and 6.10.4). When it is set, the driver counts every frame the level held, the cut one included, in `fifo_drops`, goes to STDBY_RC, clears the FIFO and starts receive again, before it reads anything.
- **An empty FIFO proves nothing.** A frame that lands after GetAndClearIrqStatus is read in the same snapshot, and its own RxDone then arrives with nothing behind it. That RxDone is neither a frame nor a drop, and the FIFO is not cleared, because a frame may be landing in it.
- **A level is given only when it is certain.** GetOokPacketStatus describes the last packet (DS 16.3.7), which may have landed after the level was read. The newest frame of the snapshot carries the RssiHigh level, in half-dB steps, only when both reads of the packet status agree. Every other frame carries none.

## Reading a dumped frame

A development image of Bravo prints each frame the decoder refused as `adsb reject ... chips=<54 hex digits>` (`products/skyblip_bravo/README.md`, Reading the rejects). The 54 digits are the 27 bytes of one FIFO frame, as the driver read them:

- **Chips.** The part consumes the 16-chip preamble and the 8-chip sync 0x95, so the 27 bytes are the 216 chips after them, at two chips a microsecond, the first chip in the high bit of the first byte.
- **Pairs.** Put 0x95 back in front and the 224 chips are 112 pairs, one per Mode S bit, bit 1 in chips 1 and 2. A 1 is `10` and a 0 is `01`. A pair of `00` or `11` is doubtful, and the decoder reads it as the inverse of its second chip, as SoftRF does.
- **Fields.** Bits 1 to 5 are the DF, 0x95 being the chips of `1000`, so a DF16 or a DF17. In a DF17, bits 6 to 8 are the capability, 9 to 32 the ICAO address, 33 to 88 the message, and 89 to 112 the parity. The 112 bits divided by the generator 0x1FFF409 leave 0 for a sound DF17, and the ICAO for a DF16.
- **Alignment.** The sync fixes where the frame starts, and nothing re-aligns it after. cb-tfc ([perryc/cb-tfc](https://github.com/perryc/cb-tfc), commit `6340f81`) tried this layout on the X1, raw chips behind a sync word, received nothing, and wrote that "the chip clock slips inside a frame". It moved to the part's own Manchester decoding of 14-byte frames. A slip would show as doubtful pairs gathering towards the end of the refused frames, and as frames that pass the CRC once one or two chips are dropped or added at the slip.

`scripts/adsb_rejects.py` does all of this for each line of a saved console, and tries one flipped bit and every slip of one or two chips. Nobody has yet read a dump from a unit, so whether the X1's chip clock slips under this driver is open.

## Re-arm and reset

`service()` re-arms a receiver that has handed up nothing for the silence ceiling it is given, 30 s from the Zephyr adapter. It goes through STDBY_RC, clears the FIFO and starts receive again, because SetRx fails on a part already in Rx (DS 6.3.5). SoftRF does the same after 15 s of silence, checked every 30 s (`radiolib.cpp:2660-2682`). A BUSY that stays high past its ceiling is a fault: the next `service()` resets the part and brings it up again.

`sleep()` goes through STDBY_RC to a cold sleep. A BUSY that blocks the standby gets one reset first, which bounds the attempt at the 1 s command ceiling plus the 3 s boot ceiling. SetSleep carries the configuration byte and a 32-bit sleep time of 0, five bytes (DS 6.3.1, where the sleep time is optional), as Semtech's `lr20xx_system_set_sleep_mode()` (`lr20xx_system.c:521-535`) and RadioLib's `LR2021::sleep()` (`LR2021.cpp:402-417`) send them. The part answers it with no status, so the driver waits 1 ms, as RadioLib does, and reads BUSY, which RadioLib notes stays high in sleep. `sleep()` answers `Ok` only then. Otherwise the Zephyr adapter leaves `parked()` false, and the shutdown's fallback past its own ceiling is `abandon()`, which holds NRESET low: a part in reset receives nothing. A part whose `begin()` failed never gets a thread, so the adapter calls `park()` at once: a sleep if the part takes one, and NRESET held low if not. With BUSY stuck, `begin()` therefore blocks for about 7 s: the 3 s boot wait, then `park()`'s 1 s standby attempt and its 3 s reset. That is longer than the 5 s the service loop allows a silent service (`runtime::kServiceSilenceCeilingMs`), so Bravo calls it in `setup()`, before that supervision applies (`products/skyblip_bravo/README.md`).

The Zephyr adapter's thread is cooperative, as the SX1262's is, so nothing preempts it between an IRQ and the FIFO read that empties nine frames' worth of FIFO in a millisecond. It must then never spin long on BUSY. A command frees BUSY in microseconds, so the driver polls every 10 us for the first millisecond, and then every 1 ms. A 32768 Hz tick is 31 us, so those 1 ms steps put the thread to sleep, and a stuck BUSY or a long calibration lets the Bluetooth host and the service loop run.

RadioLib added a DC-DC workaround, `setDCDCworkaround()`, in commit `67d7121` (#1864), after the 7.7.1 copy SoftRF runs. The driver leaves it out, as SoftRF does: neither selects the DC-DC converter, and SetRegMode's default is SIMO_OFF, the LDOs (DS 6.3.18).

RadioLib waits up to 3 s for BUSY after a reset and 1 s around any command (`LR_common.cpp` `reset()`, `Module.h` `spiConfig.timeout`). The datasheet gives no figure for either, so both are RadioLib's.

## Errors

GetErrors is read at the end of the bring-up. HF_XOSC_START_ERR means the TCXO never started and PLL_LOCK_ERR that the synthesiser never locked. Either refuses the bring-up, because the part cannot receive. The other bits are kept in `device_errors()` for the bench, and the bring-up goes on. 1090 MHz is past the 150-960 MHz the LF path is specified to (DS table 3-8 and table 3-20), so a front-end or image calibration error there is possible and is not, on its own, a dead receiver.

## The detection threshold

DS 22.1 says the part computes its OOK threshold from the receiver bandwidth, that a host may override it, and that the override is applied after SetOokModulationParams. It names no register and no formula. Semtech's driver supplies both: it writes the threshold in dB plus 74 into bits 26:20 of register 0xF30E14 (`lr20xx_workarounds.c`, `lr20xx_workarounds_ook_set_detection_threshold_level`), and computes -106 dBm as the default at 3076 kHz. RadioLib writes the threshold plus 64. SoftRF asks RadioLib for -80 dBm, so the field it writes reads -90 dBm by Semtech's formula. `lr2021::kDetectionThresholdDbm` is -90, which writes the same field SoftRF wrote. SoftRF marks its own figure `/* TODO */`, so the threshold is a parameter of `begin()` and the bench settles it.

## Unverified until #169 benches a unit

Every value below comes from SoftRF, RadioLib or the datasheet, and none has run on an X1 under this driver. Each carries a `TODO` beside it in `lr2021.h`.

- The TCXO at 1.6 V and SoftRF's start time of 32765. RadioLib meant one second in 30.52 us steps, and DS 6.11.3 counts 32 MHz periods, which makes it 1 ms. The LR2021 feeds its TCXO from its VTCXO pin (DS table 2-1, pin 6) under SetTcxoMode: it has no DIO3, which is the SX1262's TCXO line.
- The LF path and the CalibFE word 273 at 1090 MHz, outside the LF path's specified range.
- 2 Mbps in the 3076 kHz bandwidth, unshaped.
- The threshold of -90 dBm by Semtech's formula.
- The write of 0x18 into the low byte of 0xF30150, which no public document explains.
- RadioLib's BUSY ceilings of 3 s after a reset and 1 s around a command.
- The X1's pins from SoftRF `Seeed_X1.h`: NSS P0.12, SCK P0.11, MOSI P1.09, MISO P1.08, NRESET P1.10, BUSY P0.07, the IRQ on DIO8 at P1.01. The board, not this part, holds them (`boards/seeed/meshtracker_x1/pins.h`).
- That the chip clock holds over the 216 chips behind the sync, which cb-tfc found it does not (Reading a dumped frame).
- Whether GetRxFifoLevel counts a frame still arriving, which decides whether two frames 120 us apart both reach the FIFO.
- That BUSY stays high in sleep, as RadioLib notes, which is what `sleep()` reads.
- That the FIFO overflow flag latches as DS 6.10.4 says, and that a part held in reset through SYSTEM OFF draws little enough to be the shutdown's fallback.
