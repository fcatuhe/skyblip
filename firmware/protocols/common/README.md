# protocols/common

This directory holds the wire arithmetic two or more stacks share, in `skyblip::wire`, with nothing but the standard library. `en300220.h` holds the figures of the standard the 868 MHz stacks transmit under, in `skyblip::en300220`.

| File | What it is | Used by |
|---|---|---|
| `bit_field.h` | a field as an offset and a width counted from bit 0 of the first byte, the order ADS-L B.3 numbers bits in | ADS-L, ALP-TAS |
| `exponential.h` | the variable-resolution code of ADS-L G.1.6, which the ALP-TAS frame uses with other widths | ADS-L, ALP-TAS |
| `crc.h` | the 24-bit CRC of ADS-L E.3.1, whose generator 0x1FFF409 is also Mode S parity, and CRC-16 CCITT | ADS-L, ADS-B, ALP-TAS |
| `manchester.{h,cpp}` | ADS-L C.2.1's line code, with every chip pair that is neither symbol marked, and `manchester_frame`, the sync word and a frame as they go on air | ADS-L, ADS-B, ALP-TAS |
| `xxtea.{h,cpp}` | the cipher of ADS-L E.2 | ADS-L, ALP-TAS |
| `repair.{h,cpp}` | the erasure repair every frame with a linear check shares | ADS-L, ALP-TAS |
| `little_endian.h` | integers least significant byte first, as ADS-L B.3 puts them on air | ADS-L, ALP-TAS |
| `geometry.{h,cpp}` | rounded division, sines, square root, longitude wrap and the flat-earth offset, with the tree's one size of the earth | ADS-L, ADS-B, ALP-TAS |
| `status.h` | `wire::Status`, the reason a stack refuses a frame | ADS-B, ADS-L, ALP-TAS |
| `en300220.h` | EN 300 220-2 V3.3.1 Table 4's band M, and EN 300 220-1 V3.1.1 clause 5.21's polite spectrum access | ADS-L, and skyBlip's `core/timing/channel.h` |
| `test/channel.h` | a deterministic bit-error channel for the stacks' tests | every stack's tests |

Both air frames are read and written as `BitField` tables of offsets, the ADS-L ones as G.1 prints them. An ADS-L field reads back the middle of the step its code names, and an ALP-TAS field the bottom of it. skyBlip includes `geometry.h` directly, so the traffic table and the ADS-B cache measure distance the same way.

## XXTEA, `xxtea.cpp`

ADS-L scrambles with the zero key `kXxteaZeroKey` and six rounds, and ALP-TAS runs the same cipher under its own key. The names map to the reference implementation's:

| Our name | Reference name |
|---|---|
| `mix` | `MX` |
| `after`, `before` | y and z |
| `index` | p |
| `key_word` | `key[(p & 3) ^ e]` |

## Erasure repair, `repair.cpp`

A CRC is linear: flipping bit n moves the residue by the bit's syndrome, whatever else the frame holds. `repair` searches the subsets of the bits the Manchester decoder marked for the fewest flips that clear the residue, preferring flips of marked bits only. The cost doubles per marked bit, so the caller caps them: up to 16 for ADS-L and 8 for ALP-TAS, both 6 by default. On ADS-L one more unmarked bit may be flipped, found by its syndrome, which a 24-bit CRC can afford and ALP-TAS's 16-bit one cannot. A frame no repair fits comes back as it arrived, with -1. `syndromes<Bits>` builds the table at compile time from the CRC itself.

## EN 300 220, `en300220.h`

ADS-L §D.3 asks for EN 300 220-1's polite spectrum access, so a copy of the ADS-L stack needs the standard's figures. They live in `common/` because a directory under `protocols/` is a versioned stack and stacks never include each other. `scripts/fetch_etsi.py` fetches the standards, which are not committed (`docs/reference/README.md`).

`cca_threshold_centi_dbm()` is Table 45: 15 dB over Table 32's sensitivity S below 100 mW e.r.p. and 11 dB from 100 to 500 mW, less the antenna's gain in dBd, as the clause's -5 dB example fixes the sign. `channel_clear()` is 5.21.1's rule: a level below the threshold is clear, one at it is not.
