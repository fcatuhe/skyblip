# fuzz

Three libFuzzer harnesses, one per door a stranger's bytes come in through. Each links every host source the suite links, under ASan and UBSan, and runs the production code unchanged.

| Harness | Door | Input |
|---|---|---|
| `fuzz_air` | the radio: ADS-L, ALP-TAS and the O-band uplink | a receiver (UTC, position), a transmitter's frame, then chip noise |
| `fuzz_nmea` | the L76K's UART | the byte stream, as the parser's `feed()` sees it |
| `fuzz_link` | the companion link's Config and Log writes | one write per line, on a whole product |

`make fuzz` builds all three in `build/fuzz/` and runs each for `FUZZ_SECONDS` (60 by default). `make fuzz-link FUZZ_SECONDS=600` runs one. libFuzzer ships with clang, so `FUZZ_CXX` defaults to `clang++`, and the objects live in their own tree because `-fsanitize=fuzzer-no-link` instruments every one of them.

`make fuzz ABI=ilp32` builds the same harnesses for i386 in `build/fuzz-ilp32/`, with `-m32 -funsigned-char`: a 32-bit `long` and an unsigned `char`, as on the nRF52. The 64-bit host cannot see an overflow that only a 32-bit `long` reaches, and the i386 build found one in its first minutes: the RMC date's seconds were a `long`, which a garbage date overflowed at once and a real one would have from 2038. CI runs both builds, the i386 one on an x86_64 runner with `g++-multilib`.

## What each harness does to reach the code

A mutation that breaks a checksum stops at the checksum, and the fuzzer never sees the parser behind it. Each harness therefore seals what an honest transmitter would seal, and leaves the corruption to the fuzzer:

- `fuzz_air` builds the frame a transmitter would send, seals its CRC or its Reed-Solomon parity when the input asks, Manchester-encodes it past the shared sync window, then XORs the rest of the input over the chips. Noise over a clean burst can reach any chip stream, so framing, the sync tail, the error map and the forward correction are all in play, then the decoders in the order `TrafficService` calls them.
- `fuzz_nmea` rewrites the two hex digits after every `*` to the checksum of the sentence before it. `nmea_checksum_ok` itself is the suite's to test.
- `fuzz_link` boots a product with no panel onto a log partition that already holds one flight, taxis it for three seconds so it is on the ground, and connects two apps.

## The link's input

One line per action, sixteen at most. The first character says who acts, the rest of the line is the write:

| First character | Action |
|---|---|
| `C`, `c` | a Config write from the first or the second app |
| `L`, `l` | a Log write from the first or the second app |
| `P` | the pilot confirms whatever prompt is standing: wait for the gesture to arm, press twice |

A write longer than an `events::RxFrame` is dropped, because the ATT layer refuses it on silicon. The logged flight's session id is `1785628800`, which is in the dictionary.

## Seeds and dictionaries

`corpus/<name>/` holds hand-written seeds and `<name>.dict` the words the parsers compare against. Both are read-only to a run: new inputs go to `build/fuzz/corpus/<name>/`, which CI caches between runs so the corpus keeps growing. `fuzz_air` needs none: the harness is the transmitter, so every input is already a framed burst.

## When one crashes

The input is in `build/fuzz/crashes/` (or `build/fuzz-ilp32/crashes/`), and CI uploads that directory. Run the harness on it to reproduce: `build/fuzz/fuzz_link build/fuzz/crashes/link-crash-<sha>`. The fix lands with a regression case in the suite beside the code, whose one-line comment names the harness that found it.

## What these cannot see

- An ALP-TAS frame is encrypted with a key taken from the second it was sent, so a mutation rarely gets past the decrypt check in `alptas_decode`, and the fields behind it are reached far less often than the rest.
- `fuzz_link` runs at about 60 inputs a second, because every input boots a product. A short CI run is a smoke test for this one, and the cached corpus is what makes it worth more over time.
