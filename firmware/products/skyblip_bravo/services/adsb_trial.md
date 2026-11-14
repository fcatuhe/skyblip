# The reception trial

Temporary, #293. A development Bravo compares settings of its LR2021 against each other on the same air, slot by slot, so the bench can pick the ones that decode the most. When the trial has picked them, the winning values become the driver's constants and everything listed under Removal goes.

## What it changes

A profile sets five things on the LR2021, the ones cb-tfc ([perryc/cb-tfc](https://github.com/perryc/cb-tfc), `firmware/cb-tfc/src/radio.cpp`) tunes on the same X1:

| Key | What it writes | The default, SoftRF's |
|---|---|---|
| `thresholds_dbm` | the OOK detection threshold, into 0xF30E14 (`../../../hardware/parts/lr2021/README.md`, The detection threshold) | 90, so -90 dBm |
| `gain_steps` | SetAgcGainManual: 0 runs the AGC, 1 to 13 fix the gain | 0 |
| `boosts` | the LF path boost in SetRxPath, 0 to 7 | 0 |
| `front_ends` | 0xF30150: 1 writes 0x18, which stops the AGC and the blanking of a saturated ADC, and 0 writes 0x75, the part's own | 1 |
| `layouts` | 0 is 27 bytes of raw chips behind the 0x95 sync, and 1 is the part's inverted Manchester decoding of 14 bytes with no sync word, cb-tfc's layout | 0 |

cb-tfc's overnight soak gives the first candidates. It asks RadioLib for a threshold of +40, which is -98 dBm here. It reports that RadioLib's +48 and above is deaf, and +48 writes the same field as -90 dBm, our default. It also reports that raw chips behind a sync word slip, and it receives with the Manchester layout instead.

In the Manchester layout the part decodes the bits, and the driver turns them back into the chips the decoder reads (`Lr2021::read_frame`). With no sync word, every downlink format lands, and the driver hands up only frames that open on `1000`, DF16 and DF17. It counts the rest in `Lr2021::foreign_frames()`.

## How Bravo runs it

`adsb_trial` (`schemas/link.v1.json`) carries up to eight profiles and a slot length of 5 to 255 s. A development image answers it at once with `adsb_trial`, with no prompt, on the ground and in flight. A release image and the Go answer `unknown_command`, because only `main.cpp` under `CONFIG_SKYBLIP_BRAVO_ADSB_TRIAL` calls `serve_adsb_trial()`. A frame with no key but the command stops the trial and puts SoftRF's tuning back. The plan lives in RAM, so a restart ends it.

`AdsbTrial` (`adsb_trial.h`) cuts the clock into slots of `slot_s` seconds: UTC seconds once the unit has a fix, seconds since boot before that. Each slot runs one profile. The order turns back on itself every cycle, so two profiles run A B B A, and three run A B C C B A. A drift in traffic then lands on every profile alike. At each slot's start the product retunes the receiver (`run_adsb_trial` in `../product.h`), and the slot counts nothing for `AdsbTrial::kRetuneSettleMs`, one second, so frames queued under the last profile are not charged to this one.

While a capture runs, the trial writes two diagnostics records (`../../../core/diagnostics/adsb_trial.h`):

- `adsb_slot` (22), as each slot ends: the profile, the frames the decoder took, its CRC refusals, its unsupported frames, the messages folded, the folds that left a position, the distinct aircraft, the seconds counted, and whether the receiver dropped a frame.
- `adsb_chips` (23), for at most `AdsbTrial::kChipsFramesPerSecond` frames a second, four: the frame's 27 chip bytes in a head of 14 and a tail of 13, with its level and what the decoder made of it. A frame belongs to the `adsb_slot` that follows it in the capture.

The trial runs without a capture, but then it leaves nothing to read.

## Running it from a laptop

`blip.py` sends the command as JSON with the registry's names, so a trial needs no tool of its own. On fc-thinkpad, with BlueZ and the unit named `skyBlip Bravo D582`:

```
./scripts/blip.py --name "skyBlip Bravo D582" capture full
./scripts/blip.py --name "skyBlip Bravo D582" command \
    '{"command":"adsb_trial","slot_s":30,"thresholds_dbm":[90,98]}'
```

The capture asks for two presses on the button, and the trial asks for none. Two profiles in 30 s slots give about 60 pairs an hour. The chips records fill the capture ring faster than a plain full capture does, so fetch often and read `blip.py decode --summary` for gaps. A fetch resumes where the last one stopped:

```
./scripts/blip.py --name "skyBlip Bravo D582" fetch --log diagnostics --all --out trial.ndjson
python3 scripts/adsb_trial.py trial.ndjson
python3 scripts/adsb_trial.py trial.ndjson --metric aircraft --replay
```

Before the next round, send a new plan. The capture keeps running, and each slot record names its own profile. Stop the trial with `{"command":"adsb_trial"}`, then stop the capture with `capture stop` and two presses.

Disconnect between fetches. The trial does not need the link. A link that stays up costs every profile the same, but a fetch is thousands of round trips on the same pass that drains the 1090 MHz queue.

## Reading the result

`scripts/adsb_trial.py` prints two tables. The first rates each profile over all its slots: frames, CRC refusals, folds, positions and aircraft. The second compares two profiles over adjacent slots only, as B less A in the metric per minute, with a 95% interval and how often B was ahead. Positions per minute is the default metric, because a position is what reaches the phone. A slot that counted under half the usual length is left out, since it is a start, a stop or the clock's move to UTC. Two slots from different capture sessions are never paired.

A profile is better when its interval stays above zero. A profile with more frames and the same number of positions only lets more noise in.

`--replay` decodes the sampled chips again under four policies: `strict` takes a frame whose parity holds as it came, `pair` also takes one wrong bit on a doubtful chip pair (what the firmware does), `flip` takes one wrong bit anywhere (what dump1090 does), and `slip` (with `--slips`, slow) takes a frame that passes once one or two chips are dropped or added. The last lines name the addresses that only `pair` or `flip` found, which no sound frame carried. Many of those mean the policy accepts noise.

## Removal

When the trial has picked the settings:

1. Write the winning values into the constants of `hardware/parts/lr2021/lr2021.h`. If the Manchester layout won, keep its packet parameters and `chips_of_bits` without the `Lr2021Layout` switch.
2. Delete the files: `services/adsb_trial.{h,cpp,md}`, `core/comms/adsb_trial.{h,cpp}`, `core/diagnostics/adsb_trial.{h,cpp}`, the tests `test_adsb_trial.cpp`, `test_adsb_trial_link.cpp`, `test_lr2021_trial.cpp` and `test_diagnostics_adsb_trial.cpp`, and `scripts/adsb_trial.py` and `scripts/test_adsb_trial.py`, with their step in `.github/workflows/firmware.yml`.
3. `git grep -n "adsb trial"` lists every other place, each under a `TODO` that names #293: `Lr2021Tuning` and `retune` in the driver and both receivers, the hooks in `AdsbService`, `product.h` and `main.cpp`, the members in `ConfigService`, and the record decoders in `scripts/blip_records.py` and `server/app/javascript/skyblip/diagnostics.js`.
4. In `schemas/link.v1.json`, remove the `adsb_trial` frame and the `adsb_trial` reason, and note in the registry's description that command code 37 and reason code 53 are retired and never reused. Then run `scripts/link_registry.py`.
5. Retire record types 22 and 23: remove them from `core/diagnostics/record.h` with a note that the numbers are never reused, from `schemas/diagnostics_log.v1.schema.json`, and from both decoders. The decoder tests then cover types 1 to 21 again.
6. Remove `CONFIG_SKYBLIP_BRAVO_ADSB_TRIAL` from `../Kconfig`, `../prj.conf` and `../release/app.conf`, and the trial's section from `../README.md`.
7. `simulator/world/json.cpp` gained `read_unsigned_array` for this command, the first host frame with an array. Delete it with `link_text.cpp`'s array branch unless another host frame carries an array by then.
8. Regenerate `docs/BEHAVIOR.md` and `docs/TUNING.md`.
