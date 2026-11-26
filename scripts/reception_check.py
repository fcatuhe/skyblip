#!/usr/bin/env python3
"""Find the seconds a Bravo heard no traffic in a Full capture, and say what its records blame.

    ./scripts/blip.py --name "skyBlip Bravo" fetch --log diagnostics --all --out run.ndjson
    python3 scripts/reception_check.py run.ndjson

Bravo writes a receiver and a reception record each second of a Full capture
(firmware/core/diagnostics/README.md, The receiver and reception records). Their counters cross
as low bits since boot, so this subtracts each pair of seconds, as power_budget.py does Duty's,
and never across a gap, a boot or a hole longer than MAX_STEP_S.
"""
import argparse
import collections
import pathlib
import statistics
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip_ndjson as ndjson  # noqa: E402

WIDE = 1 << 16
NARROW = 1 << 8
WIDE_COUNTERS = ("frames", "part_packets", "crc", "unsupported", "folded", "positions",
                 "capture_refused")
NARROW_COUNTERS = ("frames_dropped", "rearms", "resets", "part_resets", "command_errors",
                   "missed_edges")
MAX_STEP_S = 5
DETECTION_THRESHOLD_DBM = -110
NOISE_RISE_DB = 6
BREAKS = ("gap", "boot")

Second = collections.namedtuple("Second", "at_s utc receiver reception counts")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("capture", help="the NDJSON blip.py fetch wrote, gzipped or not")
    parser.add_argument("--min-seconds", type=int, default=10,
                        help="the shortest silence reported as an outage (default 10)")
    args = parser.parse_args(argv)
    sessions = read(args.capture)
    if not sessions:
        print("no receiver records: arm a Full capture on a Bravo running this firmware",
              file=sys.stderr)
        return 1
    for session, records in sessions.items():
        for line in report(session, seconds_of(records), args.min_seconds):
            print(line)
    return 0


def read(path):
    sessions = collections.OrderedDict()
    for line in ndjson.read_ndjson(path):
        if ndjson.is_note(line) or line.get("log") not in (None, "diagnostics"):
            continue
        if "index" not in line or line.get("error"):
            continue
        sessions.setdefault(line.get("session"), []).append(line)
    for records in sessions.values():
        records.sort(key=lambda record: record["index"])
    return collections.OrderedDict((session, records) for session, records in sessions.items()
                                   if any(record["type"] == "receiver" for record in records))


def seconds_of(records):
    seconds = []
    before = None
    receiver = None
    for record in records:
        if record["type"] in BREAKS:
            before = receiver = None
        elif record["type"] == "receiver":
            receiver = record
        elif record["type"] == "reception" and receiver is not None:
            pair = dict(receiver, **record)
            if before is not None and steps(before, pair):
                seconds.append(Second(pair["at_s"], pair.get("utc_dated", False), receiver,
                                      record, counts(before, pair)))
            before = pair
            receiver = None
    return seconds


def steps(before, after):
    return (before.get("utc_dated") == after.get("utc_dated")
            and 0 < after["at_s"] - before["at_s"] <= MAX_STEP_S)


def counts(before, after):
    out = {}
    for field in WIDE_COUNTERS:
        if field in before and field in after:
            out[field] = (after[field] - before[field]) % WIDE
    for field in NARROW_COUNTERS:
        out[field] = (after[field] - before[field]) % NARROW
    # INFO: fc 26nov26 the part's own count restarts at any reset of the part, so that second has none
    if out["part_resets"] or out["resets"]:
        out.pop("part_packets", None)
    return out


def report(session, seconds, min_seconds):
    lines = ["session %s: %d seconds measured" % (session, len(seconds))]
    if not seconds:
        return lines
    noise = [second.receiver["noise_dbm"] for second in seconds if "noise_dbm" in second.receiver]
    floor = statistics.median(noise) if noise else None
    lines += whole_run(seconds, floor)
    outages = silences(seconds, min_seconds)
    blind = unexported(seconds, min_seconds)
    lines.append("%d reception outages of %d s or more, %d stretches with traffic and no fix"
                 % (len(outages), min_seconds, len(blind)))
    for run in outages:
        lines.append(describe(run, "no position placed", verdicts(run, floor)))
    for run in blind:
        lines.append(describe(run, "traffic placed, no own fix, so nothing reached an EFB", []))
    return lines


def whole_run(seconds, floor):
    total = collections.Counter()
    for second in seconds:
        total.update(second.counts)
    span = seconds[-1].at_s - seconds[0].at_s
    lines = ["  %s to %s, %d s" % (stamp(seconds[0]), stamp(seconds[-1]), span)]
    lines.append("  frames %d, crc %d, unsupported %d, folded %d, positions %d"
                 % tuple(total[field] for field in ("frames", "crc", "unsupported", "folded",
                                                    "positions")))
    lines.append("  " + ", ".join("%s %d" % (field, total[field]) for field in NARROW_COUNTERS))
    if floor is not None:
        lines.append("  noise floor median %d dBm" % floor)
    modes = collections.Counter(second.receiver.get("part_mode", "unprobed") for second in seconds)
    lines.append("  part mode " + ", ".join("%s %d s" % item for item in sorted(modes.items())))
    return lines


def silences(seconds, min_seconds):
    runs = []
    run = []
    heard_before = False
    for second in seconds:
        if second.counts.get("positions", 0) == 0:
            if heard_before:
                run.append(second)
            continue
        if len(run) >= min_seconds:
            runs.append(run)
        run = []
        heard_before = True
    if len(run) >= min_seconds:
        runs.append(run)
    return runs


def unexported(seconds, min_seconds):
    runs = []
    run = []
    for second in seconds:
        if not second.reception.get("fix_valid") and second.reception.get("tracked", 0) > 0:
            run.append(second)
            continue
        if len(run) >= min_seconds:
            runs.append(run)
        run = []
    if len(run) >= min_seconds:
        runs.append(run)
    return runs


def verdicts(run, floor):
    total = collections.Counter()
    for second in run:
        total.update(second.counts)
    probed = [second.receiver for second in run if second.receiver.get("probed")]
    found = []
    resets = [receiver["part_reset"] for receiver in probed
              if receiver.get("part_reset", "none") != "none"]
    if total["part_resets"] or resets:
        found.append("the LR2021 reset itself (%s): a brown-out or its rail, and it lost its "
                     "1090 MHz configuration" % (resets[0] if resets else "source not seen"))
    modes = sorted({receiver["part_mode"] for receiver in probed
                    if receiver.get("part_mode") != "receive" and receiver.get("listening")})
    if modes:
        found.append("the LR2021 left receive behind the driver's back: %s" % ", ".join(modes))
    thresholds = sorted({receiver["threshold_dbm"] for receiver in probed
                         if receiver.get("threshold_dbm") != DETECTION_THRESHOLD_DBM})
    if thresholds or any(not receiver.get("front_end_kept") for receiver in probed):
        found.append("the LR2021 lost what the driver wrote: threshold %s dBm, 0xF30150 %s"
                     % (",".join(str(value) for value in thresholds) or "kept",
                        "lost" if any(not receiver.get("front_end_kept") for receiver in probed)
                        else "kept"))
    if any(not second.receiver.get("listening") for second in run):
        found.append("the driver was not listening")
    if total["frames"] == 0 and total["part_packets"] > 0:
        found.append("the part decoded %d packets the driver never read: IRQ edge or FIFO"
                     % total["part_packets"])
    if total["missed_edges"]:
        found.append("%d wakes found frames with no IRQ edge" % total["missed_edges"])
    if total["frames"] == 0 and total["part_packets"] == 0:
        noise = [receiver["noise_dbm"] for receiver in probed if "noise_dbm" in receiver]
        rise = statistics.median(noise) - floor if noise and floor is not None else 0
        if rise >= NOISE_RISE_DB:
            found.append("nothing decoded, noise %+d dB over the run's median: jammed or desensed"
                         % rise)
        elif not found:
            found.append("nothing decoded and the noise as usual: the antenna, the front end or "
                         "an empty sky")
    if total["frames"] and total["crc"] >= total["frames"] - total["unsupported"]:
        found.append("%d frames heard, every one refused for parity" % total["frames"])
    if total["folded"] and not total["positions"]:
        found.append("%d messages folded and no position decoded" % total["folded"])
    if total["rearms"] or total["resets"]:
        found.append("the driver re-armed %d and reset %d times" % (total["rearms"],
                                                                     total["resets"]))
    references = sorted({second.reception.get("reference") for second in run} - {"own", "go"})
    if references:
        found.append("Bravo had no position to place traffic against (reference none)")
    return found


def describe(run, what, found):
    head = "  %s, %d s: %s" % (stamp(run[0]), run[-1].at_s - run[0].at_s + 1, what)
    return "\n".join([head] + ["    - " + line for line in found])


def stamp(second):
    return ndjson.utc_text(second.at_s) if second.utc else "+%ds" % second.at_s


if __name__ == "__main__":
    sys.exit(main())
