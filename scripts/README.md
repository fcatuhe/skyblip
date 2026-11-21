# scripts

This directory holds the host tooling. The Python scripts run on the standard library, except `blip.py`, which needs a Bluetooth stack and says so in its shebang, and `make_wordmark.py`, which needs numpy and Pillow. The `.rb` scripts are Ruby, each a thin entry point over its logic in `lib/`, tested under `.github/test/`.

| Script | What it does |
|---|---|
| [`blip.py`](blip.py) | the bench CLI: talk to a device over BLE, measure the link, offload a capture |
| [`blip_records.py`](blip_records.py) | the record decoders `blip.py` uses, importable on their own |
| [`blip_protocols.py`](blip_protocols.py) | `blip.py protocols`: print the radio protocol versions a unit carries |
| [`blip_capture.py`](blip_capture.py) | `blip.py capture`: arm or stop a unit's diagnostics capture and wait for the press's verdict |
| [`blip_frames.py`](blip_frames.py) | the companion link's CBOR codec, with the names `schemas/link.v1.json` gives its numbers |
| [`link_registry.py`](link_registry.py) | write the C++, test and JavaScript files generated from `schemas/link.v1.json`. The C++ opens with `// clang-format off`, so the generator alone decides its layout |
| [`power_budget.py`](power_budget.py) | turn a power run into a power budget: what the cell spent, and on what |
| [`link_budget.py`](link_budget.py) | turn two units' captures of one link into a path loss and an e.r.p. estimate |
| `mkuf2.py` | build a board's drag-and-drop install image, refusing to write outside its factory bootloader window |
| `build_local.sh` | build the device images of the products it is given, `skyblip_go` by default, off a committed ref, from a bare machine |
| `build_image.sh` | build, sign and check one product's images in a ready workspace. `build_local.sh`, CI and a release share it |
| `make_wordmark.py` | rasterise the wordmark into `firmware/ui/widgets/wordmark.cpp` |
| `simulator_scenarios.rb` | bundle `firmware/scenarios/*.json` into the `scenarios.json` each release carries for the simulator |
| `dev_release.rb` | publish every product image of a `firmware.yml` run as one development pre-release and prune the old ones |
| `firmware_tree.rb` | print the firmware version `firmware/VERSION` holds (`version firmware`) or every product that declares a board, as JSON (`products firmware`), for the workflows and `build_image.sh` |
| `release_files.rb` | stage a production release from its images, as `dev_release.rb` stages a pre-release: each product's images named after the version, the simulator, one `NOTICE.md` with every license text, `PROTOCOLS.md` and `SHA256SUMS` |
| `upstream_pins.rb` | print a version `.github/pins/action.yml` pins for Dependabot (`version emscripten-core/emsdk`), or fail while the vendored doctest or the Zephyr in `firmware/west.yml` differs from its pin (`check`) |
| `emsdk.rb` | print the pinned Emscripten version and its `emcc`, for `make browser`, installing them under `$EMSDK` or `~/.local/opt/emsdk` as needed |
| `deploy_order.rb` | tell the server deploy whether production already runs a newer commit |
| `behavior_index.py`, `tuning_index.py` | generate `docs/BEHAVIOR.md` with `docs/behavior/`, and `docs/TUNING.md` |
| [`conformance.py`](conformance.py) | after `make -C firmware test`, generate the ADS-L and EN 300 220 report (`adsl`) or the DO-260B report (`adsb`) |
| `spec_to_md.py`, `etsi_to_md.py`, `layout_to_md.py` | convert the specification PDFs to the markdown under `docs/reference/` |
| `fetch_etsi.py`, `fetch_rtca.py` | download the ETSI standards and the DO-260B draft into `docs/reference/`, check their SHA-256 and convert them. The PDFs are git-ignored |
| `check_*.py`, `size_check.py` | the gates CI runs. `build_image.sh` runs `check_imu_image.py`, `check_image_product.py`, `check_bootloader_keys.py` and `size_check.py` on the images it signs. Each script's docstring says what it fails on |
| `test_*.py` | the Python self-checks: `python3 -m unittest discover -s scripts` runs them all, with no radio, no network and no sleeps |

## blip.py

`blip.py` does from a terminal what a phone or the simulator page does, with numbers instead of a UI: watch the traffic picture leave the device, measure a link, and pull a flight or a diagnostics capture into a file.

### Install

The shebang runs the script through [uv](https://docs.astral.sh/uv/), which installs `bleak` on first use: `./scripts/blip.py scan`. Without it, run `uv run --with bleak scripts/blip.py scan`.

macOS goes through CoreBluetooth and asks for Bluetooth permission the first time. Linux goes through BlueZ over D-Bus, with `bluetoothd` running and no pairing. Use Linux for anything that needs two centrals at once, because macOS gives one process one connection to a peripheral.

### Watch and command a device

- `./scripts/blip.py stream --seconds 30` subscribes to Nordic UART (`6e400003`), reassembles lines as the browser client does, and prints each NMEA sentence with a host timestamp. A sentence with a bad checksum is flagged, never dropped.
- `./scripts/blip.py command '{"command":"diagnostics"}'` writes one command, as JSON with the registry's full names, to the config endpoint (`69c21302`) and prints every reply frame decoded back to names, with its round trip. An enumerated key takes its value's name (`{"command":"set","units":"metric"}`) and a byte string takes hex. A multi-frame reply ends when a frame stops saying `"more": true`. A command the unit asks about on its glass, such as `gnss_cold_start`, is answered `pending`, and `blip.py` holds the link up to 35 s for the press, because the unit cancels a prompt whose link drops.
- `./scripts/blip.py protocols` prints each radio protocol stack the unit carries with its version, as `ADS-L 0.1.0`.
- `./scripts/blip.py --name "skyBlip Bravo" capture power_run` arms a capture with the profile `full`, `power_run` or `flight_run`, and `capture stop` stops it. The unit asks for a press, on its glass or its lamp, and `blip.py` waits up to 35 s for the verdict.

`blip_frames.py` reads [`schemas/link.v1.json`](../schemas/link.v1.json) as it stands, so a key the registry gains needs no edit here. Its decoder and encoder refuse what the unit's own refuse, as `schemas/link.v1.vectors.json` lists, and `blip.py` stops on a `FrameError` instead of guessing.

### Benchmark a link

`./scripts/blip.py bench --seconds 20 --samples 10` prints the connect time to GATT ready, the negotiated MTU, the `status` round trips, the multi-frame throughput of `diagnostics`, and the stream's sentence counts, bad checksums, `$PFLAU` cadence and notification sizes. The round trip is stamped before the write goes out, because a write with response returns when the peer's controller acknowledged it, about half the exchange.

### Fetch a capture

```
./scripts/blip.py fetch --log diagnostics --list
./scripts/blip.py fetch --log diagnostics --all --out capture.ndjson
./scripts/blip.py decode capture.ndjson --summary
```

`fetch` speaks the log endpoint (`69c21303`): `list` for the sessions, then `read` for chunks of 24-byte records. Each session opens with a header line naming its record count, whether the device closed it and whether it is truncated, and each record follows as one NDJSON line. Flights records follow [`schemas/flight_log.v1.schema.json`](../schemas/flight_log.v1.schema.json) in write order, so sort on `utc_s`, and are verified against their CRC-16-CCITT. A diagnostics record has no CRC of its own, so it is checked only for a known type and a whole 24 bytes.

`--window N` counts chunks, clamped to 1 to 8 as the device clamps it. `fetch` consumes every chunk of a read before the next command goes out, so there is nothing to resync.

A dead fetch resumes: run the same line again, and `fetch` reads the tail of `--out` and restarts at the record after the last one kept. A record keeps its index only until the unit restarts (`firmware/core/comms/README.md`), so `fetch` first reads that last record back and stops if the device holds another record at its index. Fetch such a session into a new file. `--restart` ignores what the file holds.

`fetch` never writes the last record of a session the device reports as unclosed and not `recording`: the power stopped there, and a diagnostics record can be a torn write that still decodes. A `torn` line takes its place, and `decode --summary` says so first. A session marked `"truncated": true` lost its opening `boot` and `config` records to the ring, so nothing says which build or settings produced it. Records the ring recycles between two reads are printed and listed as missing.

The device refuses a command while flying and a command from a second app while another holds the claim, with `acknowledge: false` and a reason, and `blip.py` stops on it. A reply wider than the link's payload is answered `small_mtu` (`firmware/core/comms/README.md`), and `blip.py` says what to try. A diagnostics capture lives in RAM and does not survive a power cycle, so fetch it before switching the device off.

### Reading what came back

`decode` prints one line per record, and `--type gnss` narrows it to one type. `--summary` prints the caveats first, then the counts per type, the span, every missing index and every `gap` record, where the ring refused records. The session and `torn` lines survive `--type`, so a filter cannot hide a hole.

## power_budget.py

The board senses no current, only the cell's voltage, so the budget meets two halves here. The `duty` records say how long each consumer was on, the `CONSUMERS` table says what one costs, and the `power` records say how far the cell fell over the same span. The last line prints the gap between the modelled and measured draw as the table's error.

### One run

1. Charge the unit to full and unplug it.
2. Arm a power run from the briefing app's device page or with `scripts/blip.py capture power_run`, and allow it with a press. It records `boot`, `config`, `power` and `duty` every 30 s, which keeps about 120 hours, enough for an unattended run to cutoff.
3. Leave the unit in the posture you are measuring until it takes itself down.
4. Fetch and read it:

```
./scripts/blip.py fetch --log diagnostics --all --out run.ndjson
python3 scripts/power_budget.py run.ndjson --pack-mah 2400
```

`FLIGHT RUN` records the same and runs the glass as in flight, but keeps the ground cadence on air and writes no flight (`firmware/core/diagnostics/README.md`). Pass the capacity on the command line, because the board cannot tell the 2400 mAh T-Echo Plus pack from the 850 mAh T-Echo one.

### What it prints

Caveats come first, from the same helpers as `blip.py decode --summary`, then the posture split and one row per consumer with its milliamp-hours, average milliamps and share. A row marked `(est)` has no datasheet figure behind it. A part the unit may not have, such as the IMU hub, is charged only when the `boot` record's `capabilities` name it.

`measured` is what the cell spent over the last stretch on battery alone and on one clock, since the first records count seconds since boot and the rest UTC. A stretch from 99% or more to the `power_level=flat` record is a whole run and spent the pack. Otherwise the fraction comes from the discharge curve in `core/power/battery.cpp`, and the script says so. A gauge that did not fall prints no measured line. `residual` is modelled minus measured.

### The table

Three rows carry a datasheet figure for the part as this firmware drives it: the L76K tracking current, the SX1262's boosted receive on its DC-DC, and the transmit current at +14 dBm through the PA configuration `sx1262.h` writes. Every other row is an estimate. Replace one with a meter in series with the cell, such as a Nordic PPK II or a Joulescope, one consumer at a time, and rewrite its row of `CONSUMERS` and its source string.

### The counters

The duty counters wrap at 65536, and a reader subtracts two records, which crosses one wrap correctly. The firmware emits them at least every 60 s (`kDutyPeriodCeilingMs`), so the script subtracts a pair only when nothing hides between them: it refuses a pair more than 60 s apart, on two clocks, or with a missing index, an unreadable slot, a `gap` or a `boot` between them. It also drops an interval on `external_power` or `charging`, because time on USB is not drain.

Posture is read off the transmitter's air time: parked keys about 58 ms a minute, airborne about 318, and the threshold is 160. A flight run keys as parked, so its intervals read `simulated flight`, and `transmit not sent` prints what a flier would add, outside the modelled total.

## link_budget.py

The e.r.p. assertion in `firmware/hardware/parts/sx1262/sx1262.h` chains unmeasured figures: `kConductedDbm`, the antenna's vendor peak gain and `kFeedLossCentiDb`. This script estimates them from two units' captures of one link and a tape measure, pairing each burst one unit sent with the other's reception of it.

### One run, in a garden

1. Charge both units and fit the same antenna on each.
2. Stand them upright on two supports at the same height, with nothing between them or within a couple of metres. Measure each whip's middle above the ground and the distance between the whips.
3. Switch both on, wait for `TX ON` on the status page and `PPS LOCK` on the radio log page, then arm a `full` capture on each.
4. After 30 minutes, stop both captures and fetch each while still switched on:

```
./scripts/blip.py --address <unit a> fetch --log diagnostics --out a.ndjson
./scripts/blip.py --address <unit b> fetch --log diagnostics --out b.ndjson
python3 scripts/link_budget.py a.ndjson b.ndjson --distance 16.7 --height 1.2 \
    --antenna-a ANT-868-CW-QW-SMA --antenna-b ANT-868-CW-QW-SMA
```

The ground reflection is the largest error. At `quiet_distance_m()` it arrives half a wave late and can only strengthen the path: 16.7 m apart at 1.2 m up, 11.6 m at 1.0 m. At any other geometry the script prints how far the ground can move the path. The antenna, distance and height come from the command line because nothing on the board knows which whip is screwed on. Add an unknown antenna to `ANTENNAS` with its source. The script prints the distance each unit's GNSS put the other at, which catches a mistyped `--distance`.

### What it prints

Caveats come first, then one block per direction: bursts sent and heard, their level, the path loss against free space, the ground band and the sender's e.r.p. with its interval. The e.r.p. is the level heard plus free-space loss, less the receiver's antenna gain and feed loss, less 2.15 dB for a dipole. It never uses the sender's programmed power, so TXACC (DS table 3-9) is inside the measurement. The level is corrected for `packet_rssi_dbm()` truncating half-dB steps upwards. The interval adds every applicable term of `TERMS` in quadrature with the 95% interval of the mean. The RSSI term is the largest, because nothing calibrates the receiver and the SX1262 datasheet states no absolute RSSI accuracy.

### What it sets

The last block sets the two constants from the unit that radiates more:

| Line | Rule |
|---|---|
| `kFeedLossCentiDb` | the loss the chain may claim once the uncertainty is spent: the chain with no feed loss, less the higher unit's e.r.p. ceiling, never below 0 |
| `kConductedDbm` | holds while that ceiling is at or under 14 dBm, otherwise comes down by what it is over |

A third unit at the top of TXACC would sit 2 dB higher, and whether the assertion carries that margin is the owner's decision. Put the printed `kFeedLossCentiDb` into `sx1262.h` and move `firmware/test/hardware/test_sx1262.cpp` with it.

## The decoders

`blip_records.py` holds one table per record type, a tuple of `(name, codec)` pairs where a codec is a closure over an offset (`uint16(4)`, `int8(13)`, `enum8(9, SOURCE)`, `flag(2)`). Add a record type as one entry in `DIAGNOSTICS_TYPES`, and a field as one pair. The tables match `firmware/core/diagnostics/payload_sensed.cpp`, `payload_decided.cpp` and `firmware/core/flight/log_record.cpp` byte for byte, and take their names from `schemas/diagnostics_log.v1.schema.json`. `test_blip.py` holds them to the schema.

```python
import blip_records as records
for record in records.read_ndjson("capture.ndjson"):
    ...
```
