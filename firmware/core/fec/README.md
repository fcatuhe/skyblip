# core/fec

The checks and codes the protocols and the stores are built from: the ADS-L CRC, the CRC-16 and CRC-32 the stores seal records with, Manchester coding, the XXTEA scrambler and the RS(255,223) code of the O-band uplink. None of them knows what the bytes mean.

The names in the code are written out. A reader holding the textbook or the reference implementation looks for the names on the right.

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

## XXTEA, `scramble.cpp`

| Our name | Reference name |
|---|---|
| `mix_key0` | `MX`, with the key fixed at zero |
| `next`, `previous` | y and z |
| `index` | p |
