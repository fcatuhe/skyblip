# core/crypto

`ed25519.{h,cpp}` is the one door to signatures: RFC 8032 Ed25519, plain, with no context, which is the variant ADS-L E.4 names. It derives a public key from a 32-byte seed, signs and verifies, and nothing else.

Behind it is Monocypher 4.0.3, vendored in `vendor/monocypher/` with its optional `monocypher-ed25519` (`vendor/monocypher/LOCAL-CHANGES.md`). Only `crypto_ed25519_*` is called: the main header's `crypto_eddsa_*` hashes with BLAKE2b, is not RFC 8032, and would verify nothing skyblip.eu signs. `test/core/test_ed25519.cpp` holds the wrapper to all five vectors of RFC 8032 section 7.1, and `test/core/test_identity.cpp` to the exchange in `schemas/activation.v1.vectors.json`, which the server's suite checks too.

Monocypher's secret key is the seed followed by the public key. The wrapper builds it on the stack for one signature and wipes it after, so a caller holds a seed and never the 64 bytes.

The cost is stack as much as flash. Measured with `-fstack-usage` on the host, a check reaches about 1.6 KB below `crypto_ed25519_check` (`crypto_eddsa_check_equation` alone is 1152 bytes) and a signature about 1.3 KB, on top of the registration frame the caller formats. On the device both run on the main thread, inside the service loop: once at boot to verify the stored unlock code, then at most once per activation command, which is why `products/skyblip_go/prj.conf` gives that thread 6 KB where it had 4. Nothing here runs on the Bluetooth or the radio thread.
