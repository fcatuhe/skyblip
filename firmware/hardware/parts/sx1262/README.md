# parts/sx1262

The Semtech SX1262 radio, driven over `io::Spi` and `io::Gpio` by `Sx1262`, with `model.h` standing in for the chip on the host. The opcodes, registers and their arguments live in the `sx1262::` namespace beside the driver, cited by section of the datasheet (DS) where they are used.

## Names

The constants are spelled in full, so the datasheet's own spelling is what a reader searches for when a comment cites a section. Where the two differ, this is the pair, the right column spelled as this tree's comments or earlier identifiers cited it.

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
