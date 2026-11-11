# hardware/parts/ysn8900

The YXC YSN8900 real-time clock of the Seeed MeshTracker X1, on its own I2C bus. The driver finds it and nothing more: `answers()` is a zero-length write to 0x32, the probe `boards/lilygo/t_echo_plus/i2c_scan.h` uses. Nothing keeps time on it yet. A board reports its RTC as `ports::Inventory::rtc_fitted`: the T-Echo's scan sets it from the PCF8563 at 0x51, and the X1's board sets it from this probe once its platform has raised the RTC's enable.

## Sources

SoftRF line numbers are at commit d091924, the checkout these were read from. Later commits move them.

| Fact | Value | Source |
|---|---|---|
| Address | 0x32 | SoftRF `platform/nRF52.h:154` |
| Bus | SDA P0.26, SCL P0.27, enable P0.09, interrupt P0.31 | SoftRF `platform/iomap/Seeed_X1.h:32-36` |
| Probe | the enable driven high, then a zero-length write | SoftRF `platform/nRF52.cpp:940-941` and `1202-1205` |
| Chip id | 0xD2 at register 0x20 | [mverch67/YSN8900ERTC](https://github.com/mverch67/YSN8900ERTC)'s README, from register dumps of a production part |

[YXC's sheet for the part](https://www.yqmec.com/comdata/67511/productfujian/2025081816145768a2e101a8b9f.pdf), linked from [its distributor's page](https://www.yqmec.com/YSN8900-PG11072997), is one page: pins, supply and a ±5 or ±3.4 ppm TCXO. It gives no address and no register map. mverch67/YSN8900ERTC says the map follows Epson's RX8900 from 0x00 to 0x1F. It is GPL-3.0, so it is read for facts only. SoftRF's RX8900 driver for the X1 is commented out (`platform/nRF52.cpp:26`, `1920`).

The driver does not read the chip id. No datasheet gives it, and the address answering is the evidence SoftRF and the T-Echo's scan already take.

## Bench

These wait for #169's bench and a unit.

- With P0.09 high, 0x32 acknowledges on P0.26 and P0.27. With it low, record whether it still does.
- Read register 0x20 and record its value against the 0xD2 above.
