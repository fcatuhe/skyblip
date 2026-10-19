# parts/sx1262

The Semtech SX1262 radio, driven over `io::Spi` and `io::Gpio` by `Sx1262`, with `model.h` standing in for the chip on the host. The opcodes, registers and their arguments live in the `sx1262::` namespace beside the driver, cited by section of the datasheet (DS) where they are used.

## Names

The constants are spelled in full, so the datasheet's own spelling is what a reader searches for when a comment cites a section. Where the two differ, this is the pair.

| Our name | Datasheet name |
|---|---|
| `kStandbyOnRcOscillator` | STDBY_RC |
| `kStandbyOnCrystalOscillator` | STDBY_XOSC |
| `kFallbackFrequencySynthesis` | FS, in SetRxTxFallbackMode (DS 13.1.15) |
| `kFallbackStandbyOnRcOscillator` | STDBY_RC |
| `kFallbackStandbyOnCrystalOscillator` | STDBY_XOSC |
| `kSetDio3AsTcxoControl` | SetDIO3AsTCXOCtrl |
| `kTcxoVoltage1v8` | tcxoVoltage 0x02, 1.8 V |
| `kSetModulationParameters` | SetModulationParams (DS 13.4.6) |
| `kSetPacketParameters` | SetPacketParams (DS 13.4.6) |
| `kSetDioIrqParameters` | SetDioIrqParams (DS 13.3.1) |
| `kGetInstantRssi` | GetRssiInst (DS 13.5.2) |
| `kErrorCrystalOscillatorStart` | XOSC_START_ERR in OpError (DS 13.5.5) |
| `kCrystalHz` | the 32 MHz reference, FXTAL |
| `kAddressFilteringOff` | AddrComp off, in SetPacketParams |
| `kOcpLimitSteps` | the OCP register value, 2.5 mA a step (DS 5.1) |
| `kTimeoutTicksCeiling` | the 24-bit SetTx timeout, in 15.625 us steps (DS 13.4.1) |
| `RadioConfig::deviation_hz`, `ports::RfPlan::deviation_hz` | Fdev |
| `bitrate_word`, `deviation_word` in `configure_modulation()` | br and Fdev as SetModulationParams takes them |
| `pll_word` in `write_frequency()` | RFfreq, the SetRfFrequency argument |
