# core/fec

The checks and codes the protocols and the stores are built from: the ADS-L CRC, the CRC-16 and CRC-32 the stores seal records with, Manchester coding, the XXTEA cipher, the erasure repair both air frames share and the RS(255,223) code of the O-band uplink. None of them knows what the bytes mean.

The names in the code are written out. A reader holding the textbook looks for the names on the right.

## Reed-Solomon, `reed_solomon.cpp`

| Our name | Textbook name |
|---|---|
| `kCodewordBytes`, `kDataBytes`, `kParityBytes` | n, k, n - k |
| `antilogarithm_`, `logarithm_` | the exp and log tables of GF(2^8) |
| `generator_` | the generator polynomial g(x) |
| `locator`, `previous_locator` | Berlekamp-Massey's Λ(x) and B(x) |
| `register_length` | L |
| `shift` | m, the steps since B(x) last changed |
| `discrepancy`, `last_discrepancy` | δ and b |
| `evaluated` | Λ evaluated in the Chien search |
| `evaluator` | the error evaluator Ω(x) |
| `locator_derivative` | Λ'(x), the formal derivative in Forney's formula |
| `position_power`, `inverse_power` | X_k and its inverse |

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

It tries every subset of the marked bits, so the cost doubles with each one, and the caller caps how many it accepts: the ADS-L packet takes up to 16 and the ALP-TAS frame 8, both 6 by default. On an ADS-L frame one more bit nobody marked may be flipped, found by its syndrome alone, which the 24-bit CRC can afford and the 16-bit one of ALP-TAS cannot. Among the repairs that work the one with the fewest flips wins, and between two of the same size the one that touched only marked bits. A frame no repair fits comes back as it arrived, with -1.

`syndromes<Bits>` builds the table at compile time from the CRC itself: the syndrome of every bit of the frame, and the same values sorted, so the one unmarked bit a residue names is found by binary search. No table is copied in by hand.
