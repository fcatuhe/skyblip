# core/fec

The CRC-32 the settings blob is sealed with (`core/settings/blob.cpp`). The air-frame checks and codes live in [`protocols/common/`](../../protocols/common/README.md) and [`protocols/adsl/`](../../protocols/adsl/README.md). The sector store and the flight log seal their records with `wire::crc16_ccitt` from `protocols/common/`.
