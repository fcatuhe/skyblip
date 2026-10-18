# Monocypher: local changes

Vendored: **Monocypher 4.0.3**, dual-licensed BSD-2-Clause or CC0 (`LICENCE.md`, `AUTHORS.md`, both as shipped).

Source: <https://monocypher.org/download/monocypher-4.0.3.tar.gz>, the same bytes as the GitHub release asset. SHA-512 `40904ada5c7ee4f7741733e38b69a30a4b0561cbffba5ffe7c2dce16136d540251ec0d9056ff606510d3b5b708fb8a40db7e0870d4a0b2dc17ba2bfb880f8965`, as published beside the tarball, and its BLAKE2b checksum matches too.

The four source files are byte-identical to the tarball's. One thing moved: `src/optional/monocypher-ed25519.{c,h}` sit beside `src/monocypher.{c,h}` in this one directory, so the Ed25519 header's `#include "monocypher.h"` resolves without a second include path in the host Makefile and the Zephyr build.

## Only RFC 8032 Ed25519 is called

`core/crypto/ed25519.cpp` is the one caller, and it calls `crypto_ed25519_*` alone. The main header's `crypto_eddsa_*` hashes with BLAKE2b, which is not RFC 8032 and verifies nothing the server signs. The host suite checks the RFC 8032 section 7.1 vectors against this copy (`test/core/test_ed25519.cpp`).

## How it is built

The host build compiles both `.c` files as C++ with the rest of the tree, so one compiler and one set of sanitizers instrument every object. The Zephyr build compiles them as C, with `BLAKE2_NO_UNROLLING`, Monocypher's one flash knob, in case BLAKE2b is ever linked. Nothing else is configured.

## On upgrade

Replace the four files and the two licence files from the new tarball, check its published checksum, and run `make test`: the RFC vectors and `schemas/activation.v1.vectors.json` both fail on a library that signs differently.
