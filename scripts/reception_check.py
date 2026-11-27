#!/usr/bin/env python3
"""Find the seconds a Bravo heard no traffic in a Full capture, and say what its records blame.

    ./scripts/blip.py --name "skyBlip Bravo" fetch --log diagnostics --all --out run.ndjson
    python3 scripts/reception_check.py run.ndjson

Bravo writes a receiver, a framing, a screening, a drain and a reception record each second of a Full
capture
(firmware/core/diagnostics/README.md, The receiver and reception records). Their counters cross
as low bits since boot, so this subtracts each pair of seconds, as power_budget.py does Duty's,
and never across a gap, a boot, an unreadable or missing record, or a hole longer than
MAX_STEP_S. An outage ends at such a break, because nothing says what the sky did inside it.
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
                 "capture_refused", "impossible_formats", "overflow_drops", "resync_drops",
                 "queue_drops", "in_flight", "split_counts", "drains", "drain_busy_ms",
                 "squitters", "all_calls", "replies", "parity_refused", "one_bit_errors",
                 "two_bit_errors", "unmatched", "irq_wakes", "empty_wakes", "poll_ms", "busy_wait_ms",
                 "transactions", "repaired", "repairs_refused")
NARROW_COUNTERS = ("rearms", "resets", "part_resets", "command_errors", "missed_edges",
                   "framing_resyncs", "stranded_resyncs", "short_resyncs")
FRAMING_COUNTERS = ("in_flight", "split_counts", "stranded_resyncs", "short_resyncs")
# the receiver thread is cooperative, so beyond this share of the CPU the loop and the link starve
DRAIN_BUSY_CEILING_PERMILLE = 300
# the LR2021's FIFO holds 18 frames and the queue to the decoder 64, so a peak this close is a loss
FIFO_FRAMES = 18
QUEUE_FRAMES = 64
PEAK_MARGIN_FRAMES = 2
MAX_STEP_S = 5
DETECTION_THRESHOLD_DBM = -106
NOISE_RISE_DB = 6
FLOOD_FACTOR = 3
LOOP_STALL_US = 100_000
BREAKS = ("gap", "boot")

Second = collections.namedtuple(
    "Second", "at_s utc elapsed_s stretch receiver reception counts loop framing screening drain")


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
        if "index" not in line:
            continue
        sessions.setdefault(line.get("session"), []).append(line)
    for records in sessions.values():
        records.sort(key=lambda record: record["index"])
    return collections.OrderedDict((session, records) for session, records in sessions.items()
                                   if any(record.get("type") == "receiver" for record in records))


def seconds_of(records):
    seconds = []
    before = None
    receiver = None
    loop = None
    framing = None
    screening = None
    drain = None
    stretch = 0
    last_index = None
    for record in records:
        if breaks(record, last_index):
            before = receiver = loop = framing = screening = drain = None
        last_index = record["index"]
        if record.get("error") or record["type"] in BREAKS:
            continue
        if record["type"] == "loop":
            loop = record
        elif record["type"] == "framing":
            framing = record
        elif record["type"] == "screening":
            screening = record
        elif record["type"] == "drain":
            drain = record
        elif record["type"] == "receiver":
            receiver = record
        elif record["type"] == "reception" and receiver is not None:
            pair = {**receiver, **(framing or {}), **(screening or {}), **(drain or {}), **record}
            if before is None or not steps(before, pair):
                stretch += 1
            else:
                seconds.append(Second(pair["at_s"], pair.get("utc_dated", False),
                                      pair["at_s"] - before["at_s"], stretch, receiver, record,
                                      counts(before, pair), loop, framing, screening, drain))
            before = pair
            receiver = loop = framing = screening = drain = None
    return seconds


def breaks(record, last_index):
    return (record.get("error") or record.get("type") in BREAKS
            or (last_index is not None and record["index"] != last_index + 1))


def steps(before, after):
    return (before.get("utc_dated") == after.get("utc_dated")
            and 0 < after["at_s"] - before["at_s"] <= MAX_STEP_S)


def counts(before, after):
    out = {}
    for field in WIDE_COUNTERS:
        if field in before and field in after:
            out[field] = (after[field] - before[field]) % WIDE
    for field in NARROW_COUNTERS:
        if field in before and field in after:
            out[field] = (after[field] - before[field]) % NARROW
    # INFO: fc 26nov26 a reset of the part restarts its own count, so that second has none
    if out.get("part_resets") or out.get("resets"):
        out.pop("part_packets", None)
    return out


def report(session, seconds, min_seconds):
    lines = ["session %s: %d s measured" % (session, duration(seconds))]
    if not seconds:
        return lines
    noise = [second.receiver["noise_dbm"] for second in seconds if "noise_dbm" in second.receiver]
    floor = statistics.median(noise) if noise else None
    usual = statistics.median(rate(second, "frames") for second in seconds)
    lines += whole_run(seconds, floor)
    outages = silences(seconds, min_seconds)
    blind = unexported(seconds, min_seconds)
    lines.append("%d reception outages of %d s or more, %d stretches with traffic and no fix"
                 % (len(outages), min_seconds, len(blind)))
    for run in outages:
        lines.append(describe(run, "no position placed", verdicts(run, floor, usual)))
    for run in blind:
        lines.append(describe(run, "traffic placed, no own fix, so nothing reached an EFB", []))
    return lines


def whole_run(seconds, floor):
    total = collections.Counter()
    for second in seconds:
        total.update(second.counts)
    lines = ["  %s to %s" % (stamp(seconds[0]), stamp(seconds[-1]))]
    lines.append("  frames %d, crc %d, unsupported %d, folded %d, positions %d"
                 % tuple(total[field] for field in ("frames", "crc", "unsupported", "folded",
                                                    "positions")))
    lines.append("  " + ", ".join("%s %d" % (field, total[field])
                                  for field in NARROW_COUNTERS if field not in FRAMING_COUNTERS))
    if any(second.framing for second in seconds):
        lines.append("  frames lost: %d to FIFO overflows, %d to framing resyncs, %d to the full "
                     "queue" % (total["overflow_drops"], total["resync_drops"],
                                total["queue_drops"]))
        lines.append("  framing: " + ", ".join("%s %d" % (field, total[field])
                                               for field in FRAMING_COUNTERS))
        lines.append("  receiver thread: %d drains, %.1f%% of the CPU"
                     % (total["drains"], total["drain_busy_ms"] / duration(seconds) / 10))
    lines += screened(seconds, total)
    lines += woken(seconds, total)
    if floor is not None:
        lines.append("  noise floor median %d dBm" % floor)
    modes = collections.Counter()
    for second in seconds:
        modes[second.receiver.get("part_mode", "unprobed")] += second.elapsed_s
    lines.append("  part mode " + ", ".join("%s %d s" % item for item in sorted(modes.items())))
    return lines


def screened(seconds, total):
    screens = [second.screening for second in seconds if second.screening]
    if not screens:
        return []
    kept = total["squitters"] + total["all_calls"] + total["replies"]
    gains = sorted({second.receiver["gain_step"] for second in seconds
                    if "gain_step" in second.receiver})
    return [
        "  screened: %d of %d frames to the decoder (%d squitters, %d all-calls, %d replies, "
        "%d one bit and %d two bits off, repaired), %d refused for parity" % (
            kept + total["one_bit_errors"] + total["two_bit_errors"], total["frames"],
            total["squitters"], total["all_calls"], total["replies"], total["one_bit_errors"],
            total["two_bit_errors"], total["parity_refused"]),
        "  replies with no address heard in clear: %d" % total["unmatched"],
        "  peaks: a wake found %d of %d frames in the FIFO, left %d of %d in the queue" % (
            max(screen["fifo_peak_frames"] for screen in screens), FIFO_FRAMES,
            max(screen["queue_peak_frames"] for screen in screens), QUEUE_FRAMES),
        "  gain step %s, boost %s" % (", ".join(str(gain) for gain in gains) or "unprobed",
                                      ", ".join(sorted({"on" if second.receiver.get("boosted")
                                                        else "off" for second in seconds}))),
    ]


def woken(seconds, total):
    if not any(second.drain for second in seconds):
        return []
    each_us = ((total["poll_ms"] - total["busy_wait_ms"]) * 1000 / total["transactions"]
               if total["transactions"] else 0)
    drains = [second.drain for second in seconds if second.drain]
    return [
        "  wakes: %d on the FIFO IRQ, %d empty, the longest %d us; poll %d ms (%d waiting on "
        "BUSY, %d SPI transactions, %d us each)" % (
            total["irq_wakes"], total["empty_wakes"],
            max(drain.get("worst_drain_us", 0) for drain in drains), total["poll_ms"],
            total["busy_wait_ms"], total["transactions"], each_us),
        "  repairs: %d kept for an aircraft held, %d refused" % (total["repaired"],
                                                                 total["repairs_refused"]),
    ]


def silences(seconds, min_seconds):
    return stretches(seconds, min_seconds, lambda second: second.counts.get("positions", 0) == 0,
                     after_traffic=True)


def unexported(seconds, min_seconds):
    return stretches(seconds, min_seconds,
                     lambda second: (not second.reception.get("fix_valid")
                                     and second.reception.get("tracked", 0) > 0))


def stretches(seconds, min_seconds, inside, after_traffic=False):
    runs = []
    run = []
    heard = False
    stretch = None
    for second in seconds:
        if second.stretch != stretch:
            keep(runs, run, min_seconds)
            run = []
            heard = False
            stretch = second.stretch
        if inside(second):
            if heard or not after_traffic:
                run.append(second)
            continue
        keep(runs, run, min_seconds)
        run = []
        heard = True
    keep(runs, run, min_seconds)
    return runs


def keep(runs, run, min_seconds):
    if run and duration(run) >= min_seconds:
        runs.append(run)


def duration(run):
    return sum(second.elapsed_s for second in run)


def rate(second, field):
    return second.counts.get(field, 0) / second.elapsed_s


def verdicts(run, floor, usual):
    total = collections.Counter()
    for second in run:
        total.update(second.counts)
    probed = [second.receiver for second in run if second.receiver.get("probed")]
    noise = [receiver["noise_dbm"] for receiver in probed if "noise_dbm" in receiver]
    rise = statistics.median(noise) - floor if noise and floor is not None else 0
    found = []
    resets = [receiver["part_reset"] for receiver in probed
              if receiver.get("part_reset", "none") != "none"]
    if total["part_resets"] or resets:
        found.append("the LR2021 reset itself (%s): a brown-out or its rail, and it lost its "
                     "1090 MHz configuration until the driver brought it up again"
                     % (resets[0] if resets else "source not seen"))
    modes = sorted({receiver["part_mode"] for receiver in probed
                    if receiver.get("part_mode") != "receive" and receiver.get("listening")})
    if modes:
        found.append("the LR2021 left receive behind the driver's back: %s" % ", ".join(modes))
    thresholds = sorted({receiver["threshold_dbm"] for receiver in probed
                         if receiver.get("threshold_dbm", DETECTION_THRESHOLD_DBM)
                         != DETECTION_THRESHOLD_DBM})
    if thresholds or any(not receiver.get("front_end_kept") for receiver in probed):
        found.append("the LR2021 lost its 1090 MHz setup: threshold %s dBm, 0xF30150 %s"
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
    if rise >= NOISE_RISE_DB:
        found.append("noise %+d dB over the run's median: jammed or desensed" % rise)
    frames_per_s = total["frames"] / duration(run)
    if usual and frames_per_s >= FLOOD_FACTOR * usual:
        found.append("%d frames a second, %d times the run's median of %d: noise past the "
                     "preamble detector" % (frames_per_s, frames_per_s / usual, usual))
    if total["framing_resyncs"]:
        found.append("the receive FIFO lost its framing and was emptied %d time%s"
                     % (total["framing_resyncs"], "" if total["framing_resyncs"] == 1 else "s"))
    if any(second.framing for second in run):
        lost = [(total[field], where) for field, where in
                (("overflow_drops", "FIFO overflows"), ("resync_drops", "framing resyncs"),
                 ("queue_drops", "the full queue")) if total[field]]
        if lost:
            found.append("frames lost to " + ", ".join("%s %d" % (where, count)
                                                       for count, where in lost))
        busy_permille = total["drain_busy_ms"] / duration(run)
        if busy_permille > DRAIN_BUSY_CEILING_PERMILLE:
            found.append("the receiver thread held %d%% of the CPU" % (busy_permille / 10))
    found += peaks(run)
    found += stalls(run)
    if total["frames"] == 0 and total["part_packets"] == 0 and not found:
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


def peaks(run):
    screens = [second.screening for second in run if second.screening]
    if not screens:
        return []
    found = []
    fifo = max(screen["fifo_peak_frames"] for screen in screens)
    if fifo >= FIFO_FRAMES - PEAK_MARGIN_FRAMES:
        found.append("a wake found %d of the FIFO's %d frames: the drain period is too long"
                     % (fifo, FIFO_FRAMES))
    queue = max(screen["queue_peak_frames"] for screen in screens)
    if queue >= QUEUE_FRAMES - PEAK_MARGIN_FRAMES:
        found.append("the queue to the decoder held %d of %d frames: the decoder fell behind"
                     % (queue, QUEUE_FRAMES))
    return found


def stalls(run):
    loops = [second.loop for second in run if second.loop]
    if not loops:
        return []
    worst = max(loops, key=lambda loop: loop["worst_pass_us"])
    if worst["worst_pass_us"] < LOOP_STALL_US:
        return []
    return ["the service loop stalled: a pass of %d ms in %s, as few as %d passes a second"
            % (worst["worst_pass_us"] // 1000, worst.get("worst_service", "?"),
               min(loop["passes"] for loop in loops))]


def describe(run, what, found):
    head = "  %s, %d s: %s" % (stamp(run[0]), duration(run), what)
    return "\n".join([head] + ["    - " + line for line in found])


def stamp(second):
    return ndjson.utc_text(second.at_s) if second.utc else "+%ds" % second.at_s


if __name__ == "__main__":
    sys.exit(main())
