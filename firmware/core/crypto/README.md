# core/crypto

`ed25519.{h,cpp}` is the one door to signatures: RFC 8032 Ed25519, plain, with no context, which is the variant ADS-L E.4 names. It derives a public key from a 32-byte seed, signs and verifies, and nothing else.

Behind it is Monocypher 4.0.3, vendored in `vendor/monocypher/` with its optional `monocypher-ed25519` (`vendor/monocypher/LOCAL-CHANGES.md`). Call only `crypto_ed25519_*`. The main header's `crypto_eddsa_*` hashes with BLAKE2b, is not RFC 8032, and would verify nothing skyblip.eu signs.

Monocypher's secret key is the seed followed by the public key. The wrapper builds it on the stack for one signature and wipes it after, so a caller holds a seed and never the 64 bytes. The seed itself stays in UICR, which any firmware on the unit can read (`core/identity/README.md`).

The cost is stack as much as flash. Measured with `-fstack-usage` on the host, a check reaches about 1.6 KB below `crypto_ed25519_check` and a signature about 1.3 KB, on top of the frame the caller formats. Both run on the main thread: once at boot to verify the stored unlock code, then at most once per identity command. That is why `products/common/prj.conf` gives the main thread 6 KB. Nothing here runs on the Bluetooth or the radio thread.
