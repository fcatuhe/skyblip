# core/fec

The CRC-32 the settings blob is sealed with (`core/settings/blob.cpp`). The checks and codes the air frames are built from moved to `protocols/`: the 24-bit CRC, CRC-16 CCITT, Manchester coding, XXTEA and the erasure repair to [`protocols/common/`](../../protocols/common/README.md), and the RS(255,223) code of the O-band uplink to [`protocols/adsl/`](../../protocols/adsl/README.md). The sector store and the flight log seal their records with `wire::crc16_ccitt` from there.
