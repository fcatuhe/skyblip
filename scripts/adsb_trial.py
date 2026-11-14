#!/usr/bin/env python3
"""Compare the receiver profiles of a skyBlip Bravo reception trial, and replay its sampled frames.

    python3 scripts/adsb_trial.py run.ndjson
    python3 scripts/adsb_trial.py run.ndjson --metric aircraft
    python3 scripts/adsb_trial.py run.ndjson --replay --slips

Temporary: #293 removes it with the trial. run.ndjson is what `blip.py fetch --log diagnostics`
wrote from a capture that ran beside an `adsb_trial`. Each `adsb_slot` record is one slot of one
profile. The first table rates each profile over all its slots. The second compares two profiles
over adjacent slots only, so that the traffic, which changes by the minute, is close to the same on
both sides. --replay decodes the sampled `adsb_chips` frames under several repair policies.
firmware/products/skyblip_bravo/services/adsb_trial.md says how to run a trial and read this.
"""
import argparse
import collections
import json
import math
import pathlib
import statistics
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import adsb_rejects  # noqa: E402

METRICS = ("positions", "folded", "aircraft", "frames")
# INFO: fc 13nov26 a slot that counted under half of the usual is a start, a stop or a clock jump
SHORT_SLOT_SHARE = 0.5
Z_95 = 1.96
POLICIES = ("strict", "pair", "flip", "slip")


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("capture", help="the NDJSON blip.py fetch wrote")
    parser.add_argument("--metric", choices=METRICS, default="positions",
                        help="what the paired comparison counts per minute, positions by default")
    parser.add_argument("--replay", action="store_true",
                        help="decode the sampled frames under each repair policy")
    parser.add_argument("--slips", action="store_true",
                        help="with --replay, also try a slip of one or two chips, which is slow")
    args = parser.parse_args(argv[1:])
    lines = read(args.capture)
    for line in report(lines, args.metric):
        print(line)
    if args.replay:
        print()
        for line in replay_report(frames_of(lines), args.slips):
            print(line)
    return 0


def read(path):
    with open(path, encoding="utf-8") as source:
        return [json.loads(line) for line in source if line.strip()]


def label_of(slot):
    return "%d dBm, gain %s, boost %d, %s, %s" % (
        slot["threshold_dbm"], slot["gain_step"] or "agc", slot["boost"],
        "0x18" if slot["front_end_raw"] else "0x75",
        "manchester" if slot["manchester"] else "chips")


def slots_of(lines):
    """Each slot with its session, its label and its rates per minute."""
    slots = []
    for line in lines:
        if line.get("type") != "adsb_slot" or line["counted_s"] == 0:
            continue
        slot = dict(line)
        slot["label"] = label_of(line)
        slot["session"] = line.get("session")
        for metric in METRICS:
            slot[metric + "_per_min"] = 60.0 * line[metric] / line["counted_s"]
        slots.append(slot)
    if not slots:
        return slots
    usual = statistics.median(slot["counted_s"] for slot in slots)
    return [slot for slot in slots if slot["counted_s"] >= SHORT_SLOT_SHARE * usual]


def report(lines, metric):
    slots = slots_of(lines)
    if not slots:
        return ["no adsb_slot record: did a capture run beside the trial, and was it fetched?"]
    out = ["profile: slots, minutes, frames/min, crc %, folded/min, positions/min, aircraft/slot, "
           "slots with a FIFO drop"]
    by_label = collections.defaultdict(list)
    for slot in slots:
        by_label[slot["label"]].append(slot)
    for label, group in sorted(by_label.items()):
        frames = sum(slot["frames"] for slot in group)
        minutes = sum(slot["counted_s"] for slot in group) / 60.0
        out.append("%s: %d, %.1f, %.1f, %.1f, %.1f, %.1f, %.1f, %d" % (
            label, len(group), minutes, frames / minutes,
            100.0 * sum(slot["crc"] for slot in group) / frames if frames else 0.0,
            sum(slot["folded"] for slot in group) / minutes,
            sum(slot["positions"] for slot in group) / minutes,
            statistics.mean(slot["aircraft"] for slot in group),
            sum(slot["fifo_dropped"] for slot in group)))
    out.append("")
    out.extend(paired(slots, metric))
    return out


def paired(slots, metric):
    """B less A in metric per minute, over each two adjacent slots of one session."""
    diffs = collections.defaultdict(list)
    for first, second in zip(slots, slots[1:]):
        if first["session"] != second["session"] or first["label"] == second["label"]:
            continue
        a, b = sorted((first, second), key=lambda slot: slot["label"])
        diffs[(a["label"], b["label"])].append(b[metric + "_per_min"] - a[metric + "_per_min"])
    if not diffs:
        return ["no two adjacent slots ran different profiles, so nothing compares"]
    out = ["%s per minute, B less A over adjacent slots: pairs, mean, 95%% interval, B ahead"
           % metric]
    for (a, b), values in sorted(diffs.items()):
        mean = statistics.mean(values)
        spread = (Z_95 * statistics.stdev(values) / math.sqrt(len(values))
                  if len(values) > 1 else float("nan"))
        out.append("A %s | B %s: %d, %+.1f, %+.1f to %+.1f, %d of %d" % (
            a, b, len(values), mean, mean - spread, mean + spread,
            sum(value > 0 for value in values), len(values)))
    return out


def frames_of(lines):
    """Each sampled frame as (label, verdict, rssi, 27 chip bytes), labelled by the slot after it."""
    frames, waiting, head = [], [], None
    for line in lines:
        kind = line.get("type")
        if kind == "session":
            waiting, head = [], None
        elif kind == "adsb_chips" and not line["tail"]:
            head = line
        elif kind == "adsb_chips" and head is not None and line["seq"] == head["seq"]:
            chips = bytes.fromhex(head["chips"]) + bytes.fromhex(line["chips"])[:13]
            waiting.append((head["chips_verdict"],
                            head["rssi_dbm"] if head["rssi_valid"] else None, chips))
            head = None
        elif kind == "adsb_slot":
            frames.extend((label_of(line),) + frame for frame in waiting)
            waiting = []
    frames.extend(("open slot",) + frame for frame in waiting)
    return frames


def policies_passed(chips, slips):
    """Which policies accept the frame as a DF17, and the address each one reads."""
    stream = adsb_rejects.stream_of(chips)
    bits, doubtful = adsb_rejects.decode(stream)
    value = adsb_rejects.number(bits)
    residue = adsb_rejects.remainder(value)
    passed = {}
    if residue == 0:
        passed.update(dict.fromkeys(POLICIES, value))
    else:
        flip = next((bit for bit in range(1, adsb_rejects.BITS + 1)
                     if adsb_rejects.SYNDROMES[bit] == residue), None)
        if flip is not None:
            fixed = value ^ (1 << (adsb_rejects.BITS - flip))
            passed["flip"] = fixed
            if flip in doubtful:
                passed["pair"] = fixed
        if slips and next(adsb_rejects.slips(stream), None) is not None:
            passed["slip"] = None
    return {policy: (value >> (adsb_rejects.BITS - 32)) & 0xFFFFFF if value is not None else None
            for policy, value in passed.items()
            if value is None or value >> (adsb_rejects.BITS - 5) == adsb_rejects.EXTENDED_SQUITTER}


def replay_report(frames, slips):
    if not frames:
        return ["no adsb_chips record: frames are sampled only while a capture runs"]
    policies = POLICIES if slips else POLICIES[:3]
    counts = collections.defaultdict(collections.Counter)
    addresses = collections.defaultdict(set)
    for label, _, _, chips in frames:
        counts[label]["frames"] += 1
        for policy, address in policies_passed(chips, slips).items():
            counts[label][policy] += 1
            if address is not None:
                addresses[policy].add(address)
    out = ["replay, DF17 accepted: frames, " + ", ".join(policies)]
    for label, count in sorted(counts.items()):
        out.append("%s: %d, %s" % (label, count["frames"],
                                   ", ".join(str(count[policy]) for policy in policies)))
    sound = addresses["strict"]
    for policy in ("pair", "flip"):
        unheard = sorted(addresses[policy] - sound)
        out.append("addresses only %s finds, never in a sound frame: %s" % (
            policy, ", ".join("%06x" % address for address in unheard) or "none"))
    return out


if __name__ == "__main__":
    sys.exit(main(sys.argv))
