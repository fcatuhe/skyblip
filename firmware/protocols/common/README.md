# protocols/common

The wire arithmetic two or more stacks share, in namespace `skyblip::wire`. None of it knows what the bytes mean, and none of it includes anything but the standard library. `en300220.h` is the one file that is not wire arithmetic: it holds the figures of the radio standard the 868 MHz stacks transmit under, in namespace `skyblip::en300220`.

| File | What it is | Used by |
|---|---|---|
| `bit_field.h` | a field as an offset and a width counted from bit 0 of the first byte, the order ADS-L B.3 numbers bits in | ADS-L, ALP-TAS |
| `exponential.h` | the variable-resolution code of ADS-L G.1.6, which the ALP-TAS frame uses with other widths | ADS-L, ALP-TAS |
| `crc.h` | the 24-bit CRC of ADS-L E.3.1, whose generator 0x1FFF409 is also Mode S parity, and CRC-16 CCITT | ADS-L, ADS-B, ALP-TAS |
| `manchester.{h,cpp}` | ADS-L C.2.1's line code, a 1 as the chips 01 and a 0 as 10, with every chip pair that is neither symbol marked, and `manchester_frame`, the whole sync word and a frame as they go on air | ADS-L, ADS-B, ALP-TAS |
| `xxtea.{h,cpp}` | the cipher of ADS-L E.2 | ADS-L, ALP-TAS |
| `repair.{h,cpp}` | the erasure repair every frame with a linear check shares | ADS-L, ADS-B, ALP-TAS |
| `little_endian.h` | integers least significant byte first, as ADS-L B.3 puts them on air | ADS-L, ALP-TAS |
| `geometry.{h,cpp}` | rounded integer division, the sine table, an integer square root, longitude wrap and the flat-earth offset between two points | ADS-L, ADS-B, ALP-TAS |
| `status.h` | `wire::Status`, the reason a stack refuses a frame | ADS-B, ADS-L, ALP-TAS |
| `en300220.h` | ETSI EN 300 220-2 V3.3.1 Table 4's band M, and EN 300 220-1 V3.1.1 clause 5.21's polite spectrum access: Table 32's sensitivity limit, Table 45's CCA threshold and Table 48's timing limits | ADS-L, whose §D.3 cites EN 300 220-1, and skyBlip's `core/timing/channel.h` |
| `test/channel.h` | a deterministic bit-error channel and the xorshift behind it, for the stacks' tests | every stack's tests |

Both air frames are read and written field by field. A field is a `BitField`, so `adsl/packet.h` and `alptas/frame.cpp` hold tables of offsets rather than shift sequences, and the ADS-L offsets are the ones G.1 prints. An ADS-L field reads back the middle of the step its code names, and an ALP-TAS field the bottom of it.

skyBlip includes `geometry.h`, `little_endian.h` and `test/channel.h` directly rather than keeping copies of its own, so a distance the traffic table measures and one the ADS-B cache ranks by are the same arithmetic. `core/util/intmath.h` keeps only the arctangent, which no stack uses. `geometry.h` holds the tree's one size of the earth, `kMicrometresPerE7`.

## XXTEA, `xxtea.cpp`

`xxtea_encrypt` and `xxtea_decrypt` are the cipher of ADS-L E.2, with the key and the round count as arguments. ADS-L scrambles with the zero key `kXxteaZeroKey` and six rounds, the uplink does the same over its own words, and the ALP-TAS frame runs the same cipher under its own key. The loop walks the words in a ring, so the word after the last one is the first.

| Our name | Reference name |
|---|---|
| `mix` | `MX` |
| `after`, `before` | y and z |
| `index` | p |
| `key_word` | `key[(p & 3) ^ e]` |

## Erasure repair, `repair.cpp`

A CRC is linear: flipping bit n of a frame moves its residue by the same amount whatever else the frame holds, the bit's syndrome. `repair` takes a frame, the residue its check left, the syndrome of every bit and the bits the Manchester decoder marked as doubtful, and looks for the smallest set of flips that brings the residue to zero.

It walks the subsets of the marked bits depth first, one XOR per step, and stops as soon as a single flip clears the check. The cost doubles with each marked bit, so the caller caps how many it accepts: the ADS-L packet takes up to 16 and the ALP-TAS frame 8, both 6 by default. On an ADS-L frame one more bit nobody marked may be flipped, found by its syndrome alone, which the 24-bit CRC can afford and the 16-bit one of ALP-TAS cannot. An ADS-L frame with more marked bits than its cap still gets that one-bit repair. An ALP-TAS frame with too many is refused. Among the repairs that work the one with the fewest flips wins, and between two of the same size the one that touched only marked bits. A frame no repair fits comes back as it arrived, with -1.

`syndromes<Bits>` builds the table at compile time from the CRC itself: the syndrome of every bit of the frame, and the same values sorted, so the one unmarked bit a residue names is found by binary search. No table is copied in by hand.

## EN 300 220, `en300220.h`

ADS-L §D.3 asks for listen-before-talk "according to the Polite Spectrum Access rules laid out in ETSI EN 300 220-1", so a copy of the ADS-L stack needs the standard's figures, and `adsl/media_access.cpp` takes its minimum deferral period from here. They are in `common/` rather than in a directory of their own because a directory under `protocols/` is a stack, with a version that names what it puts on air, and one stack never includes another. The standards themselves are not in the repository: `scripts/fetch_etsi.py` fetches them (`docs/reference/README.md`).

`ten_log10_centi()` is the logarithm Table 32 needs at compile time, in hundredths of a dB: the integer part of log2 by shifting, sixteen fractional bits by squaring the mantissa, times 10 log10 2. `sensitivity_centi_dbm()` is Table 32's S = 10 log RB - 117 dBm, with RB in hertz. `cca_threshold_centi_dbm()` is Table 45: 15 dB over S below 100 mW e.r.p. and 11 dB from 100 to 500 mW, at an antenna of 0 dBd, plus the front end's own gain. The note under Table 45 asks for the limit to be "adjusted accordingly" for other antennas. A front end behind an antenna that gains G dB over a dipole hears every signal G dB louder than the dipole would, so its threshold moves up by G for the radiated threshold to stay at the limit, and down by G for an antenna below a dipole. The clause's example reads the same correction from the measured side: with a -5 dB antenna, the threshold measured at the front end is "increased by 5 dB" to give the radiated one. `channel_clear()` is 5.21.1's rule: a level below the threshold is clear, one at it is not.
