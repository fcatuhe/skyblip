# scripts

Host tooling. Everything here is Python 3 on the standard library, except `blip.py`, which needs a Bluetooth stack and says so in its shebang.

| Script | What it does |
|---|---|
| [`blip.py`](blip.py) | the bench CLI: talk to a device over BLE, measure the link, offload a capture |
| [`blip_records.py`](blip_records.py) | the record decoders `blip.py` uses, importable on their own |
| [`power_budget.py`](power_budget.py) | turn a power run into a power budget: what the cell spent, and on what |
| `mkuf2.py` | build the drag-and-drop install image, and refuse to build a dangerous one |
| `build_local.sh` | build the device image off a committed ref |
| `behavior_index.py`, `tuning_index.py`, `spec_to_md.py` | generate `docs/` out of the tree |
| `check_*.py`, `size_check.py` | the structural gates CI runs |
| `test_mkuf2.py`, `test_blip.py`, `test_blip_offload.py`, `test_power_budget.py` | the Python self-checks, run by the `firmware` workflow |

## blip.py

What a phone or the simulator page does, from a terminal, with numbers instead of a UI. It is for the bench: seeing the traffic picture as it leaves the device, measuring what a link actually costs, and pulling a flight or a diagnostics capture off the device into a file an analysis script can read.

### Install

There is none. The shebang runs the script through [uv](https://docs.astral.sh/uv/), which installs `bleak` into a throwaway environment on first use:

```
./scripts/blip.py scan
```

Without the shebang, or on a machine where `scripts/` is not executable: `uv run --with bleak scripts/blip.py scan`.

macOS goes through CoreBluetooth and asks for Bluetooth permission the first time the terminal uses it. Linux goes through BlueZ over D-Bus (`bluetoothd` running, no pairing needed). Use Linux for anything that needs two centrals at once, such as watching what the claim does to a second app: macOS gives one process one connection to a peripheral and will not hand out a second.

### Watch a device

```
./scripts/blip.py stream --seconds 30
```

Subscribes to Nordic UART (`6e400003`), reassembles lines the way the browser client does (a notification is a slice of a byte stream, not a sentence), verifies every NMEA checksum and prints each sentence with a host timestamp. A sentence whose checksum does not match is printed and flagged, never dropped.

`./scripts/blip.py cmd '{"cmd":"diag"}'` writes one JSON command to the config endpoint (`69c21302`) and prints every reply frame with its round trip. A multi-frame reply ends when a frame stops saying `"more": true`, so `diag` needs no `--parts` and will not silently truncate when the device grows a sixth group.

### Benchmark a link

```
./scripts/blip.py bench --seconds 20 --samples 10
```

Connect time to GATT ready, the negotiated MTU and the payload it leaves, the distribution of `status` round trips, the multi-frame throughput of `diag`, then the stream: sentence counts by talker, bad checksums, `$PFLAU` cadence and its jitter, notification sizes and the gap between notifications.

The round trip is stamped before the write goes out, not when `write_gatt_char` returns. Writing with a response returns when the peer's controller acknowledged the write, which is roughly half the exchange, and a version of this that stamped afterwards reported round trips of half a millisecond.

### Fetch a capture

```
./scripts/blip.py fetch --log diagnostics --list
./scripts/blip.py fetch --log diagnostics --all --out capture.ndjson
./scripts/blip.py decode capture.ndjson --summary
```

`fetch` speaks the log endpoint (`69c21303`): `list` for the session count and one frame per session, then `read` for base64 chunks of 24-byte records. Each session opens with a header line naming its record count, whether the device closed it and whether it is truncated, and each record follows as one NDJSON line carrying its store, session and index. Flights records are verified against the CRC-16-CCITT they were written with; diagnostics records carry no CRC of their own (the sector label holds it, `core/store/sector.h`), so a diagnostics slot is checked for a known type and a whole 24 bytes and nothing more.

`count` on a `read` is chunks, the way `core/comms/log_link.cpp` parses it, so `--window N` is chunks too: clamped to 1 to 8 here exactly as the device clamps it, 8 by default, which is 96 records over a link that negotiated a 498-byte MTU. A read is answered by that many chunks or by fewer ending in `eof`, and `fetch` consumes every one of them before the next command goes out. There is nothing to resync from, and nothing that resyncs.

A dead fetch resumes. The next command is the acknowledgement in this protocol, so the recovery is to ask again from the last index kept: run the same line, and `fetch` reads the tail of `--out`, skips the sessions already whole and restarts the interrupted one at the record after its last line. `--restart` ignores what the file holds.

### The two records a fetch will not hand you

A session the device reports as unclosed stopped where the power did, and a diagnostics record has no checksum of its own, so its last slot can be a torn write that still decodes as a plausible record. `fetch` does not write it. In its place goes a `torn` line naming the index it dropped and why, and `decode --summary` says so before it says anything else. Flights are untouched by this: a flight record carries a CRC and the firmware has already dropped a torn one.

A session the device reports as truncated lost its beginning to the sector allocator, and with it the `boot` and `config` records a capture opens on. The header line carries `"truncated": true`, the summary names it, and an analysis reading that corpus knows it cannot say which build or which settings produced it.

Two things the device will say no to: a command while it is flying (an offload is thousands of round trips beside a receiver anchored to a PPS edge) and a command from a second app while another holds the claim. Both come back as `ack: false` with a reason, and `blip.py` stops with that reason rather than retrying.

A diagnostics capture lives in RAM and is armed from the device's own diagnostics screen. It does not survive a power cycle, so fetch it before switching the device off.

### Reading what came back

`decode` prints one line per record, `--type gnss` narrows it to one type, and `--summary` gives the caveats first, then the counts per type, the span, every index missing from the file and every `gap` record in full. A `gap` record is the ring saying it had to refuse records right there: a corpus with invisible holes teaches the wrong number with total confidence, which is why the holes are records and why the summary prints them. The session and `torn` lines survive `--type` for the same reason: a filter must not be able to hide what the corpus cannot vouch for.

## power_budget.py

A board that senses no current cannot report milliamps, and this one senses none: the only electrical quantity on it is the divider across the cell. So the budget is assembled from two halves that meet here. The `duty` records say how long each consumer was on, the table at the top of the script says what one costs, and the `power` records say how far the cell actually fell over the same span. The gap between the modelled draw and the measured one is the table's error, printed as the last line rather than hidden.

### One run, end to end

Charge the unit to full and unplug it. Arm `POWER RUN` on the CAPTURE page, which is the row that records `boot`, `config`, `power` and `duty` and nothing else, every 30 s: the page prints `KEEPS 188H25 ROLLING` against the full capture's `1H08`, and 188 hours is what makes an unattended run to cutoff survive in the ring. Then leave it alone in the posture you are measuring until it takes itself down.

```
./scripts/blip.py fetch --log diagnostics --all --out run.ndjson
python3 scripts/power_budget.py run.ndjson --pack-mah 2400
```

The capacity comes from the command line because the device cannot know it: LilyGO fits 2400 mAh on the T-Echo Plus and 850 mAh on the plain T-Echo, on the same footprint, and no reading on the board can tell the two apart.

### What it prints, and in which order

Caveats first, the way `blip.py decode --summary` does and out of the same helpers, because a corpus with holes in it teaches the wrong number with total confidence. A session the device did not close and the tail record it dropped, a run whose opening sector the ring recycled, every missing index and unreadable slot, every `gap` record in full, why the run is not a whole one when it is not, every interval dropped and why, and how many of the table's currents cite a datasheet. None of them was metered on this board.

Then the time the Duty intervals cover, the stretch the cell was measured over, the posture split, and one row per consumer that drew anything, with its milliamp-hours, its average milliamps and its share. A row marked `(est)` has no datasheet figure behind it, only an estimate.

Two lines close it. `measured` is what the cell spent over the last stretch it ran on battery alone and on one clock. A capture outlives the first fix, so its first records count seconds since boot and the rest count UTC, and two instants on different clocks cannot be subtracted: the stretch is timed on the clock the run ended on. When that stretch starts at the gauge's 99% or more and ends on the `level=cutoff` record the parking capture writes, the run is whole and the pack is what it spent, which needs no curve at all. Otherwise it is a fraction of the pack read off the two textbook curves in `core/power/battery.cpp`, which the script says out loud because that is the assumption the run was supposed to replace. A gauge that did not fall gives no measured line at all, never a zero or a negative draw. `residual` is modelled minus measured, and the percentage beside it is how much of the discharge the table explains.

### The table, and what retires an estimate

Three rows carry a datasheet figure for the part as this firmware drives it: the L76K tracking current, the SX1262's boosted receive on its DC-DC, and the transmit current for +14 dBm through the PA configuration `sx1262.h` actually writes. Every other row is an estimate, and the honest ones to attack first are the ones with the largest share.

What replaces an estimate is a meter in series with the cell, a Nordic PPK II or a Joulescope, with one consumer moving at a time. When a figure comes back, it changes in one place: its row of `CONSUMERS`, with its source string rewritten from a datasheet reference to the bench that measured it.

### Two things the reader has to know about the counters

They wrap at 65536 rather than saturating, because a reader subtracts two records and unsigned subtraction crosses a wrap correctly as long as one interval moves less than that. The firmware bounds the emitting cadence at 60 s (`kDutyMaxPeriodMs`) and asserts every period that emits the record against it, which bounds two consecutive records and nothing more. So the script subtracts a pair only when it can see everything between them: it refuses a pair more than 60 s apart, on two clocks, or with a missing index, an unreadable slot, a `gap` or a `boot` between them, and a pair it keeps cannot reach the wrap. A `boot` inside a session is the ring recycling under it, so it drops the Duty interval and not the cell's measured stretch. When the parking capture writes its pair on the same instant as the last regular one, the later Duty stands for both. A test holds its 60 s to the firmware's constant. It also drops an interval whose `power` record has `external_power` or `charging` set, because time on USB is not drain.

Posture is read off the air the transmitter spent, because a power run records no flight record to ask. Parked is three position bursts and three callsign bursts in every 30 s, about 58 ms of air a minute, because the callsign burst is not gated on flight state. Airborne is thirty and three, about 318. The threshold sits at 160, and a device with no callsign set, which drops the callsign bursts from both figures (about 29 and 289), still lands on the right side of it.

## The decoders

`blip_records.py` holds one table per record type: a tuple of `(name, codec)` pairs, where a codec is a small closure over an offset (`u16(4)`, `i8(13)`, `enum8(9, SOURCE)`, `flag(2)`). Adding a record type is one entry in `DIAG_TYPES`, and adding a field is one pair in its tuple. The tables are byte-exact against `firmware/core/diag/payload_sensed.cpp`, `payload_decided.cpp` and `firmware/core/flight/log_record.cpp`, and the field names and enum spellings are `schemas/diagnostics_log.v1.schema.json`'s.

Import it directly for analysis, the CLI is not in the way:

```python
import blip_records as records
for record in records.read_ndjson("capture.ndjson"):
    ...
```

## Tests

```
python3 scripts/test_blip.py
python3 scripts/test_mkuf2.py
```

No radio, no network, no sleeps, stdlib only. `test_blip.py` covers the decoders against handcrafted bytes and against vectors the firmware's own encoders produced, the NMEA checksum and line reassembly, the chunk framing and the resume logic. It also checks the tables against the schema: a type name or a field name that drifts fails there. It loads `test_blip_offload.py` with it, which answers a `read` the way `log_link.cpp` does and fails a host that waits for a frame the device never sent or sends a command while a reply to the last one is still queued.
