# parts/sx1262

The Semtech SX1262 radio, driven over `io::Spi` and `io::Gpio` by `Sx1262`, with `model.h` standing in for the chip on the host. The opcodes, registers and their arguments live in the `sx1262::` namespace beside the driver, cited by section of the datasheet (DS) where they are used.

The Go stages TX in STDBY_XOSC. It writes seven sync register bytes in one WriteRegister command (DS 13.2.1). The configured 56 bits fit the 64-bit GFSK limit (DS Table 13-54). The SX1262 sends eight hardware preamble chips before the register. The register starts with `D5 55 99` and follows it with the Manchester sync word. The leading `11` isolates the hardware's eight chips from the seven `01` repetitions that precede the two `1001` markers, regardless of the hardware preamble's phase. The FIFO contains the Manchester length and packet. The driver restores the 16-bit shared receive sync and usual receive parameters before starting RX again.

The model puts the preamble ahead of the sync and FIFO on transmit. The model can emit either alternating phase. Its sync matcher hears a peer with five repetitions after the driver restores receive, but the model does not implement the silicon preamble detector. An SDR capture still needs to confirm the transmit chip stream and the interval from the end of CCA to first RF energy. Minimum-preamble reception still needs a hardware test.

## A failed transfer

Every command, register access and buffer read returns the `Status` of its SPI transfers, and a sequence stops at the first that fails. A failed transfer is `Status::Down`. Once the part is brought up, it also owes a reinitialisation, which the no-RX rope in `service()` pays, as it pays one for a BUSY line that never fell. So `begin()` and `configure_radio()` refuse, `stage_tx()` leaves nothing for `key_tx()` to key, and `start_receive()` does not claim RX. `poll()` reports a `Fault` when the IRQ status, its clear or the payload could not be read, rather than act on bytes the part never sent, and a frame whose packet status could not be read carries no RSSI. `instant_rssi_dbm()` returns a `Result`, so a clear channel assessment whose level read failed calls the channel busy with no level (`platform/clear_channel.h`).

## Names

The constants are spelled in full. Where the datasheet spells one differently, this is the pair.

| Our name | Datasheet name |
|---|---|
| `kStandbyOnRcOscillator`, `kFallbackStandbyOnRcOscillator` | STDBY_RC |
| `kStandbyOnCrystalOscillator`, `kFallbackStandbyOnCrystalOscillator` | STDBY_XOSC |
| `kFallbackFrequencySynthesis` | FS, in SetRxTxFallbackMode (DS 13.1.15) |
| `kSetDio3AsTcxoControl` | SetDio3AsTcxoCtrl |
| `kSetModulationParameters` | SetModulationParams (DS 13.4.6) |
| `kSetPacketParameters` | SetPacketParams (DS 13.4.6) |
| `kSetDioIrqParameters` | SetDioIrqParams (DS 13.3.1) |
| `kGetInstantRssi` | GetRssiInst (DS 13.5.2) |
| `kErrorCrystalOscillatorStart` | XOSC_START_ERR in OpError (DS 13.5.5) |
| `kAddressFilteringOff` | AddrComp off, in SetPacketParams |
| `kOcpLimitSteps` | the OCP register, 2.5 mA a step (DS 5.1) |
| `kTimeoutTicksCeiling` | the SetTx timeout, 24 bits of 15.625 us steps (DS 13.4.1) |
| `RadioConfig::deviation_hz`, `ports::RfPlan::deviation_hz` | fdev |
| `bitrate_word`, `deviation_word` in `configure_modulation()` | br and fdev, as SetModulationParams takes them |
