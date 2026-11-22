# fuzz

This directory holds seven libFuzzer harnesses, one per door a stranger's bytes come in through, each running the production code under ASan and UBSan.

| Harness | Door | Input |
|---|---|---|
| `fuzz_air` | the radio: ADS-L, ALP-TAS and the G.3 uplink | a receiver's time and position, a transmitter's frame or uplink packet, then noise |
| `fuzz_adsb` | the 1090 MHz radio: DF17 | a transmitter's frame, then chip noise |
| `fuzz_adsb_assembly` | the 1090 MHz radio: one aircraft's messages after another | a receiver's reference and own address, then frames, each after a gap of time |
| `fuzz_nmea` | the L76K's UART | the byte stream, as the parser's `feed()` sees it |
| `fuzz_link` | the companion link's Config and Log writes | one action after another, on a whole product |
| `fuzz_peer_bravo` | the peer endpoint on Bravo: claims from any central | one action after another, on a whole Bravo |
| `fuzz_peer_go` | the peer endpoint on the Go: health and observations from anything named like a Bravo | one action after another, on a whole Go |

`make fuzz` builds all seven with clang in `build/fuzz/` and runs each for 30 to 90 s. `FUZZ_SECONDS` sets all seven, and `make fuzz-link FUZZ_SECONDS=600` runs one. `make fuzz ABI=ilp32` builds them for i386 with `-m32 -funsigned-char`, a 32-bit `long` and an unsigned `char` as on the nRF52, to catch overflows the 64-bit host cannot reach. CI runs both builds.

## What each harness does to reach the code

A mutation that breaks a checksum stops there, so each harness seals what an honest transmitter would, and leaves the corruption to the fuzzer:

- `fuzz_air` encodes a frame, raw, sealed by its CRC or Reed-Solomon parity, or built from ALP-TAS fields, and XORs the rest of the input over its chips. Only the encoded ALP-TAS path gets past the decrypt check.
- `fuzz_adsb` sends the input's frame raw or as a sealed DF17, then XORs the rest over the chips.
- `fuzz_adsb_assembly` seals DF17 frames from 256 addresses, more than the cache holds, with gaps of up to 65 s. It aborts when the cache holds more than `kMaxAircraft`, a counter falls or a position lies past a pole. It calls `abort`, not `__builtin_trap`, because libFuzzer does not save the SIGTRAP the trap raises on aarch64.
- `fuzz_nmea` rewrites the two hex digits after every `*` to the checksum of the sentence before it.
- `fuzz_link` boots a product with no panel and one logged flight, and connects two apps.
- `fuzz_peer_bravo` boots a Bravo with a fix and connects two centrals, the first pinned when the input's first byte is odd.
- `fuzz_peer_go` boots a Go with no panel and a fix, and connects it to a Bravo: pinned when the input's first byte is odd, otherwise picked from a scan and waiting for the health that pins it.

## The link's input

The input is sixteen actions at most, back to back. A frame is binary CBOR, so no byte can mark the end of one: each action is its byte, then a write's length in two bytes, little-endian, then the write.

| Byte | Action |
|---|---|
| `C`, `c` | a Config write from the first or the second app |
| `L`, `l` | a Log write from the first or the second app |
| `P` | the pilot confirms whatever prompt is standing: wait for the answer to arm, press once. No length, no write |

Any other byte is skipped. A write longer than an `events::RxFrame` is dropped, as the ATT layer refuses it on silicon. The logged flight's session id, `1785628800`, is in the dictionary. The seeds were written with `scripts/blip_frames.py`, and the activation is signed with the test key of `schemas/activation.v1.vectors.json`.

## The peer endpoint's input

The input is a byte for the setup, then sixteen actions at most in the link's format: each action is its byte, then a frame's length in two bytes, little-endian, then the frame. A frame longer than an `events::RxFrame` is dropped, as the ATT layer refuses it on Bravo and the Go's central skips it.

| Harness | Byte | Action |
|---|---|---|
| `fuzz_peer_bravo` | `G`, `g` | a write from the first or the second central |
| | `P` | a press on Bravo's button. No length, no frame |
| | `S` | a second with a fix |
| | `A` | an aircraft heard 1 km north, both halves of its position |
| | `D` | the first central drops and connects again |
| `fuzz_peer_go` | `N` | a notification from the Bravo |
| | `S` | a second with a fix |
| | `W` | a second without one |
| | `D` | the link drops and the Go connects again |
| | `U` | the pilot unpairs on the Go |

Any other byte is skipped. The seeds were written with `scripts/blip_frames.py`.

## Seeds and dictionaries

`corpus/<name>/` holds the seeds and `<name>.dict` the tokens the parsers compare against. New inputs go to `build/fuzz/corpus/<name>/`, which CI caches. The radio harnesses need neither, because each is the transmitter.

## When one crashes

CI uploads `build/fuzz/crashes/`. Reproduce with `build/fuzz/fuzz_link build/fuzz/crashes/link-crash-<sha>`, and land the fix with a regression case whose comment names the harness that found it.

## What these cannot see

Only a raw ALP-TAS frame reaches a track of 360 to 511 and the two constants behind the decrypt check, which the encoder cannot write.
