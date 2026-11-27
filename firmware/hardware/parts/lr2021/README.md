# parts/lr2021

The Semtech LR2021 on skyBlip Bravo, a Seeed SenseCAP MeshTracker X1, receiving 1090 MHz Extended Squitter full time with its OOK modem. `Lr2021` drives it over `io::Spi` and `io::Gpio`, and `model.h` stands in for the chip on the host. The driver receives and never transmits.

## Sources

| Source | What it gives | How it is used |
|---|---|---|
| Semtech, LR2021/LR2022/LR2012 Datasheet, Final Rev. 2.1, 13 April 2026 (DS), read from [Digi-Key's copy](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/8924/600_62785538.LR20xxDatasheet_V2_1.pdf) | every opcode, argument layout, the status word, reset, BUSY, the FIFO, the IRQ bits, and the ADS-B detector settings of 16.3.8 | cited as `DS` and a section |
| RadioLib at commit `b087af9` (8.0.0-6-gb087af9), `src/modules/LR2021` and `src/modules/LR11x0/LR_common.cpp` (MIT) | how a read takes two frames, the bring-up order, the SetSleep layout and settle, the boot and command BUSY ceilings | followed, listed in `NOTICE.md` |
| SoftRF, commit `d091924`, `driver/radio/radiolib.cpp:2232-2359` and `platform/iomap/Seeed_X1.h` (GPL-3.0) | the 1090 MHz modem values, the register write at 0xF30150, the X1's TCXO voltage, its DIO8 IRQ and its pins | read for facts, no code copied |
| Semtech USP, commit `db5bbb0`, `lr20xx_driver/src/lr20xx_workarounds.c`, `lr20xx_system.c` and `lr20xx_radio_common.c` | the OOK threshold field, the SetSleep and CalibFE layouts | facts, no code copied |

SoftRF has received ADS-B on this hardware, so where its RadioLib 7.7.1 copy and `b087af9` differ, the driver sends the bytes SoftRF put on the wire. It departs from SoftRF in three places, on the evidence of the reception trial of #293 on Bravo D582: the part's Manchester decoding of 14-byte frames, cb-tfc's layout ([perryc/cb-tfc](https://github.com/perryc/cb-tfc)), a threshold of -110 dBm, and the threshold written after the frequency.

## What the driver sends

`begin()` holds NRESET low for 100 us (DS 4.2) and waits for BUSY to fall. It then reads the version, which must be 1.24 (DS 6.7.2), because BUSY falling proves only a pull-down and a rail. Then, in this order:

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
| SetRxPath | LF, no boost | SoftRF `:2655` |
| CalibFE | 273 on the LF path, then two zero words, 6 bytes | SoftRF `:1993` through its RadioLib copy's `setFrequency()`, which rounds 1090 MHz to the nearest 4 MHz. DS 6.4.2 calibrates only the non-zero words |
| SetRfFrequency | 1 090 000 000 Hz | DS 7.3.1 |
| SetOokModulationParams | 2 Mbps, no shaping, rx_bw 0 (3076 kHz), full depth | SoftRF `:2250-2281`, DS 16.3.1, table 11-4 |
| SetOokPacketParams | 16 chips of preamble, fixed length, 14 bytes, no CRC, Manchester inverted (0x09) | SoftRF's `beginOOK()` at `:2237` for the 16 and `setCRC(0, 0)` at `:2283-2290`. cb-tfc's `configure_es1090()` for the 14 bytes and `RADIOLIB_LR2021_OOK_MANCHESTER_ON_INV`. DS 16.3.2 |
| SetOokWhiteningParams | polynomial 0, off | SoftRF's `setEncoding(RADIOLIB_ENCODING_NRZ)` at `:2319`, which its RadioLib copy turns into this command (`LR2021_config.cpp:756-760`, `:812-822`). DS 16.3.9 |
| SetOokSyncword | none, 0 bits, MSB first | cb-tfc's `setSyncWord(NULL, 0)`, DS 16.3.4 |
| SetOokDetector | pattern 0x0285, 16 chips, no repeat, falling edge, no SFD | SoftRF `:2338`, DS 16.3.8 |
| SetAgcGainManual | 0, the AGC | SoftRF `:2339`, DS 7.3.9 |
| WriteRegMemMask32 | 0xF30E14, mask 0x07F00000, the threshold | SoftRF `:2350`, `lr20xx_workarounds.c` for the register and field, DS 22.1 for the order |
| WriteRegMemMask32 | 0xF30150, mask 0x000000FF, data 0x18 | SoftRF `:2359`. #293 measured it against 0x75, the part's own |
| GetErrors | | DS 6.7.4 |
| GetStatus | | DS 6.7.1: clears the reset source the NRESET pulse left, so a later one is a reset the driver never asked for |

`start_receive()` sends SetRxPath, SetDioIrqConfig with RxDone alone on the IRQ DIO, ClearIrq and SetRx with timeout 0xFFFFFF, continuous receive (DS 6.3.5): the part keeps listening after each frame.

Pattern 0x0285 read first bit first is `1010000101000000`, the 8 us preamble at two chips a microsecond, with pulses at 0, 1, 3.5 and 4.5 us (DS 16.3.8). With no sync word, the part decodes the 224 chips behind it as 112 Manchester bits and puts 14 bytes in the FIFO.

## Reading a frame

Each SPI frame opens on two status bytes whose command status is the result of the command before (DS 6.7.1). The driver counts a CMD_FAIL or CMD_PERR there as a `command_error` and runs on, as SoftRF does. A read is two frames: the command, then zeros that clock the answer out (DS 5.4.1.2). The FIFO read answers in its own frame (DS 5.4.1.3).

On the IRQ the driver takes a snapshot: GetAndClearIrqStatus, GetOokPacketStatus, GetOokRxStats, GetRxFifoLevel, GetAndClearFifoIrqFlags, GetOokRxStats again and GetOokPacketStatus again. It then reads one 14-byte frame per `poll()`, oldest first, as many as the part counted. Frames keep landing meanwhile, so the driver holds four rules:

- **An overflow stops the reading.** Nineteen frames fill the 256-byte FIFO with eighteen and 4 bytes of the nineteenth, so every later read would start 4 bytes off. The overflow flag latches whether or not its IRQ is enabled (DS 6.10.1, 6.10.2, 6.10.4). When it is set, the driver counts every frame the level held in `fifo_drops`, goes to STDBY_RC, clears the FIFO and restarts receive, before reading anything.
- **An empty FIFO proves nothing.** A frame that lands after GetAndClearIrqStatus is read in the same snapshot, and its own RxDone then finds nothing. That RxDone is neither a frame nor a drop, and the FIFO is not cleared, because a frame may be landing.
- **Every byte belongs to a frame.** See Framing, below.
- **A level is given only when it is certain.** GetOokPacketStatus describes the last packet (DS 16.3.7), which may have landed after the level was read. Only the newest frame of the snapshot carries the RssiHigh level, in half-dB steps, and only when both reads of the packet status agree.

## Framing

The receive FIFO is a byte stream with no mark between frames. In continuous receive the part appends each packet to it, and DS 5.3.1 leaves framing to the host: "the payload length needs to be read after each RxDone interrupt to determine which data belongs to which packet". Cutting it into 14-byte blocks by its level holds only while it holds whole frames. One stray byte shifts every later block across two frames, and nothing decodes until the FIFO is emptied. On bravo-d582, after a flood outdoors, frames and noise came back to normal indoors and nothing decoded for minutes, until a reboot (#550).

So the driver keeps a conservation law: every byte in the FIFO belongs to a frame the part counted or to the one frame still arriving. The part counts its packets itself (GetOokRxStats `pkt_rx`, DS 16.3.6). Two Full captures on bravo-d582 found it equal to the frames the driver read to 0.1% over 2.8 million frames, and never ahead of them. The count is taken once each time receive starts, in STDBY_RC where no frame can land, together with zero frames read.

At each snapshot the driver reads the count on both sides of the level. When the two counts differ, a frame closed between them, so the driver reads only the frames both the count and the level vouch for, and checks the next snapshot. When they agree, the frames counted and not yet read are whole, and the level minus 14 bytes each is the excess:

- **0:** the FIFO holds exactly those frames.
- **1 to 13:** a frame still arriving, or the remains of one that never closed. A Manchester bit lasts 1 us, so a frame still arriving grows a byte every 8 us. The driver reads the count and the level again after `kInFlightGrowthUs`, 24 us. If either moved, the bytes are a frame arriving, and the driver reads the whole frames ahead of it. If neither moved, they are remains.
- **Remains, negative, or 14 and more:** bytes that no counted frame owns, or counted frames whose bytes are gone. The driver counts the frames it cannot trust in `fifo_drops` and a `framing_resync`, goes to STDBY_RC, clears the FIFO and starts receive again with a new count.

The law holds whether the part writes a frame byte by byte or whole at RxDone, and whatever left the stray bytes: a frame abandoned in a flood, an overflow missed, an underflow or a driver fault. It costs one register read a snapshot and, only when the excess is not zero, a 24 us wait. Recovery comes on the snapshot after the fault, not after a timer, and receive never stops to frame a healthy stream. Each outcome is counted, `in_flight()`, `stranded_resyncs()`, `short_resyncs()` and `split_counts()`, and so are the frames lost to an overflow and to a resync, apart. A capture's `Framing` record carries them (`../../../core/diagnostics/README.md`, The receiver and reception records), so the first busy run says whether the part fills its FIFO as a frame arrives, the one figure the law leans on that no document gives.

On the SX1262 the Go reads ADS-L and FLARM with, the question never arises: each packet carries a sync word of 24 bits or more, and GetRxBufferStatus gives each packet's start and length. cb-tfc (`radio.cpp` `handle_es_rx`) starts receive again after every frame, which loses the frames that land in the restart. SoftRF (`radiolib.cpp` `lr20xx_receive`, `d091924e`) receives continuously, reads one packet per RxDone, and empties the FIFO only when 15 s pass without a frame, behind a 0x95 sync byte that keeps most noise out of the FIFO.

## The frame in the FIFO

One FIFO frame is 14 bytes, the 112 bits of a Mode S long frame, bit 1 in the high bit of the first byte:

- **Bits.** The part reads each chip pair behind the preamble as one bit, inverted Manchester, so a 1 is the chips `10`, a pulse then a gap. It has decided every bit and hands up no doubtful one.
- **Formats.** No sync word picks the format, so every downlink format the preamble detector catches lands, DF11 all-call replies and Mode S radar replies included. `read_frame()` hands every frame up, and the decoder tells them apart (`protocols/adsb/README.md`, Mode S: replies with no position). A 56-bit reply fills the first 7 bytes, and the 7 behind it hold whatever the part decided after it.
- **Fields.** Bits 1 to 5 are the DF. In a DF17, bits 6 to 8 are the capability, 9 to 32 the ICAO address, 33 to 88 the message, and 89 to 112 the parity. The 112 bits divided by the generator 0x1FFF409 leave 0 for a sound DF17, and the ICAO for a DF16.

The driver uses the part's Manchester decoding, not SoftRF's raw chips behind a 0x95 sync, because the chip clock slips inside a frame. Replayed offline in #293, 13 to 24% of raw chip frames passed the parity, against 86 to 97% in the Manchester layout.

## Re-arm and reset

`service()` re-arms a receiver that has handed up nothing for the silence ceiling it is given, 30 s from the Zephyr adapter. It goes through STDBY_RC, clears the FIFO and restarts receive, because SetRx fails on a part already in Rx (DS 6.3.5). A BUSY that stays high past its ceiling is a fault: the next `service()` resets the part and brings it up again.

RadioLib waits up to 3 s for BUSY after a reset and 1 s around any command (`LR_common.cpp` `reset()`, `Module.h` `spiConfig.timeout`). The datasheet gives no figure, so both ceilings are RadioLib's.

`sleep()` goes through STDBY_RC to a cold sleep. A BUSY that blocks the standby gets one reset first, which bounds the attempt at the 1 s command ceiling plus the 3 s boot ceiling. SetSleep carries the configuration byte and a 32-bit sleep time of 0, five bytes (DS 6.3.1), as Semtech's `lr20xx_system_set_sleep_mode()` and RadioLib's `LR2021::sleep()` send it. The part answers with no status, so the driver waits 1 ms, as RadioLib does, and reads BUSY, which stays high in sleep. Only then does `sleep()` answer `Ok`.

When the sleep fails, the Zephyr adapter leaves `parked()` false, and the shutdown's fallback is `abandon()`, which holds NRESET low: a part in reset receives nothing. A part whose `begin()` failed never gets a thread, so the adapter calls `park()` at once: a sleep if the part takes one, and NRESET held low if not. With BUSY stuck, `begin()` blocks for about 7 s: the 3 s boot wait, then `park()`'s 1 s standby attempt and 3 s reset. That is longer than the 5 s the service loop allows a silent service (`runtime::kServiceSilenceCeilingMs`), so Bravo calls it in `setup()`, before that supervision applies (`products/skyblip_bravo/README.md`).

The Zephyr adapter's thread is cooperative, so nothing preempts it between an IRQ and the FIFO read that empties nine frames' worth of FIFO in a millisecond. So it must never spin long on BUSY. The driver polls BUSY every 10 us for the first millisecond, since a command frees it in microseconds, and then every 1 ms. A 32768 Hz tick is 31 us, so the 1 ms steps sleep the thread and let the Bluetooth host and the service loop run.

### The receiver thread

The thread sleeps on the IRQ's rising edge, drains the FIFO, then sleeps out the rest of `kDrainPeriodUs`, 5 ms, before it waits for the next edge. Edges that come meanwhile wait in its semaphore, so the next drain takes every frame they announced in one snapshot.

Woken per frame, as it was before #550, a snapshot of 7 to 9 reads cost about 500 us a frame, measured on bravo-d582 as the service loop's busy time against the frame rate. The thread is cooperative, so it yielded only when the FIFO ran empty, and at 1500 frames a second, a flood outdoors, it held the CPU: the loop fell to 7 passes a second and the GNSS UART overran. Paced, it wakes at most 200 times a second whatever the sky does, and the FIFO's 18 frames hold a 5 ms period at up to 3600 frames a second, twice the flood bravo-d582 met (`static_assert`). Past that the FIFO overflows, and the frames are counted lost rather than the CPU. The thread's own busy time and its drains reach a capture's `Framing` record, because the loop's busy time counts it inside whichever service it interrupted.

If the GPIO driver refuses the trigger, at start or when a reset re-arms it, the thread polls the part once each period instead, and the diagnostics' `irq_armed` reads false.

The driver leaves out RadioLib's DC-DC workaround, `setDCDCworkaround()` (`67d7121`, #1864), as SoftRF does: neither selects the DC-DC converter, and SetRegMode's default is SIMO_OFF, the LDOs (DS 6.3.18).

## The probe

`sample()` asks the part about itself, and both adapters call it once a second while it receives. Every command is a read, and none leaves receive:

| Command | What it gives | Source |
|---|---|---|
| GetRssiInst | the level at 1090 MHz as NSS rises, the noise floor when no frame is on the air, in half-dB steps | DS 7.3.3 |
| GetOokRxStats | `pkt_rx`, the packets the part counted itself since its last power-on, reset or cold sleep | DS 16.3.6 |
| GetErrors | the error bits now, not only at bring-up | DS 6.7.4 |
| ReadRegMem32 at 0xF30E14 | the detection threshold the part holds, a 7-bit two's complement field less 74 | DS 6.2.3, `lr20xx_workarounds.c` |
| ReadRegMem32 at 0xF30150 | whether the low byte still holds the 0x18 the driver wrote | DS 6.2.3 |

The status word that opens every SPI frame gives the chip mode in bits 2:0 and the reset source in bits 7:4 (DS 6.7.1, table 6-38). Bring-up ends on GetStatus, which clears the reset source, so a probe that reads one counts it in `part_resets()` and clears it again. A brown-out resets the part as a power-on does (`Analog (POR/BRN)`), and the part comes back in STDBY_RC without its OOK modem, its threshold or its 0xF30150 byte. The probe reports all three, and `ports::ReceiverProbe` carries them to a capture (`../../../core/diagnostics/README.md`, The receiver and reception records).

A re-arm cannot recover from that reset: SetRx alone restarts receive on the part's power-on defaults, not on the 1090 MHz OOK modem, and Bravo stayed deaf until it booted again. So a probe that reads a reset source owes a reset, and the next `service()` brings the part up from NRESET, counted in `resets()`. On the Zephyr adapter that is within a second of the reset and a 250 ms health tick.

## Errors

GetErrors is read at the end of the bring-up. HF_XOSC_START_ERR (the TCXO never started) or PLL_LOCK_ERR (the synthesiser never locked) refuses the bring-up, because the part cannot receive. The other bits are kept in `device_errors()` for the bench. 1090 MHz is past the 150-960 MHz the LF path is specified to (DS tables 3-8 and 3-20), so a front-end or image calibration error there does not mean a dead receiver.

## The detection threshold

DS 22.1 says a host may override the OOK threshold the part computes from the bandwidth, after SetOokModulationParams, and names no register or formula. Semtech's driver writes the threshold in dB plus 74 into bits 26:20 of register 0xF30E14 (`lr20xx_workarounds_ook_set_detection_threshold_level`), with -106 dBm as the default at 3076 kHz. RadioLib writes the threshold plus 64, so by Semtech's formula cb-tfc's +40 is -98 dBm and SoftRF's -80 is -90.

`lr2021::kDetectionThresholdDbm` is -110, the pick of #293's trial. Against -106 dBm it decoded 58 more positions a minute (95% interval +6 to +110) and 8 more aircraft a minute. -114 dBm decoded no more positions and refused 56% of its frames.

Write the threshold after SetRfFrequency, as RadioLib's `beginOOK()` and cb-tfc do. Written before SetRxPath, CalibFE and SetRfFrequency, it behaved in #293 like the part's own threshold, so one of those commands most likely resets it. Which one is not known.

The probe reads the register back, so a capture shows the threshold the part holds. Across the overflow restarts of the bravo-d582 floods the register kept -110 dBm, so SetRxPath alone does not reset it. Why it stays fixed is in `../../../products/skyblip_bravo/README.md`, The detection threshold.

## Unverified until #169 benches a unit

Each value below comes from SoftRF, RadioLib or the datasheet, has no bench measurement on an X1 yet, and carries a `TODO` in `lr2021.h`.

- The TCXO at 1.6 V and SoftRF's start time of 32765. RadioLib meant one second in 30.52 us steps, and DS 6.11.3 counts 32 MHz periods, which makes it 1 ms. The LR2021 feeds its TCXO from its VTCXO pin (DS table 2-1, pin 6) and has no DIO3, the SX1262's TCXO line.
- The LF path and the CalibFE word 273 at 1090 MHz, outside the LF path's specified range.
- 2 Mbps in the 3076 kHz bandwidth, unshaped.
- RadioLib's BUSY ceilings of 3 s after a reset and 1 s around a command.
- The X1's pins from SoftRF `Seeed_X1.h`: NSS P0.12, SCK P0.11, MOSI P1.09, MISO P1.08, NRESET P1.10, BUSY P0.07, the IRQ on DIO8 at P1.01. The board holds them (`boards/seeed/meshtracker_x1/pins.h`).
- Whether GetRxFifoLevel counts a frame still arriving, which decides whether two frames 120 us apart both reach the FIFO.
- That BUSY stays high in sleep, which `sleep()` reads.
- That a ReadRegMem32 in receive loses no frame. GetRssiInst is meant for receive (DS 7.3.3), and the datasheet says nothing either way of a register read.
- That the FIFO overflow flag latches as DS 6.10.4 says, and that a part held in reset through SYSTEM OFF draws little enough to be the shutdown's fallback.
