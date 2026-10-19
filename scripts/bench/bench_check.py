"""Per-build bench checks: keying, burst ends, dwell ends, reception, PPS, GNSS, noise, reinits.

usage: python3 scripts/bench/bench_check.py <E68BD9.ndjson> <0B1B2C.ndjson> [more pairs...]
Files are read one by one; the cross-match pairs every file with every other file.
"""
import collections
import statistics
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records

WORD_MASK = 0xFFFFFFFF
SLOT = {0: (450, 790), 1: (800, 990)}
SLOT0_LAST_FROM_BUILD = {844: 787, 856: 789, 863: 790, 874: 791}
SLOT1_LAST_FROM_BUILD = {856: 992, 863: 992, 874: 993}


def mix(value):
    value ^= value >> 16
    value = (value * 0x7FEB352D) & WORD_MASK
    value ^= value >> 15
    value = (value * 0x846CA68B) & WORD_MASK
    value ^= value >> 16
    return value


def quantile(values, fraction):
    return values[min(len(values) - 1, int(len(values) * fraction))] if values else None


def stats(values):
    ordered = sorted(values)
    if not ordered:
        return "n=0"
    return "n=%d min %d med %d p99 %d max %d" % (len(ordered), ordered[0], statistics.median(ordered), quantile(ordered, .99), ordered[-1])


def load(path):
    raw = list(blip_records.read_ndjson(path))
    records = [record for record in raw if record.get("type") not in blip_records.NOTE_TYPES]
    return records


def windows_for(build):
    slots = dict(SLOT)
    for since, last in sorted(SLOT0_LAST_FROM_BUILD.items()):
        if build >= since:
            slots[0] = (450, last)
    for since, last in sorted(SLOT1_LAST_FROM_BUILD.items()):
        if build >= since:
            slots[1] = (800, last)
    return slots


def integrity(records):
    by_session = collections.defaultdict(list)
    for record in records:
        by_session[record["session"]].append(record["index"])
    out = []
    for session, indices in by_session.items():
        counts = collections.Counter(indices)
        duplicates = [i for i, count in counts.items() if count > 1]
        ordered = sorted(counts)
        holes = [(previous, following) for previous, following in zip(ordered, ordered[1:]) if following != previous + 1]
        out.append("session %d: %d records, index %d..%d, dups %d %s, holes %d %s" % (
            session, len(indices), ordered[0], ordered[-1], len(duplicates), duplicates[:5], len(holes), holes[:5]))
    return out


def report(path, records):
    boot = [record for record in records if record["type"] == "boot"]
    build = boot[0]["fw_build"] if boot else 0
    address = next(record["address"] for record in records if record["type"] == "config")
    ground = mix(address) % 10
    slots = windows_for(build)
    first_s, last_s = records[0]["at_s"], max(record["at_s"] for record in records)
    print("== %s  addr %06X  build %s  boots %d  ground %d  span %d s" % (
        path.split("/")[-1], address, [boot_record["fw_build"] for boot_record in boot], len(boot), ground, last_s - first_s))
    for line in integrity(records):
        print("  " + line)
    types = collections.Counter(record["type"] for record in records)
    for record_type in ("gap", "unreadable", "end"):
        if types[record_type]:
            print("  !! %d %s records" % (types[record_type], record_type))

    # keying
    own = [record for record in records if record["type"] == "burst" and record["verdict"] == "transmitted"]
    print("  verdicts own", dict(collections.Counter(record["verdict"] for record in records
                                                    if record["type"] == "burst" and record["verdict"] != "received"
                                                    and record["verdict"] != "named" and record["verdict"] != "unframed")))
    for kind, selects in (("pos", lambda burst: not burst["callsign"]), ("name", lambda burst: burst["callsign"])):
        selected = [burst for burst in own if selects(burst)]
        if not selected:
            continue
        print("  %-4s keyed_us %s" % (kind, stats([burst["tx_keyed_us"] for burst in selected])))
        print("  %-4s SetTx->TxDone_us %s" % (kind, stats([burst["tx_span_us"] - burst["tx_keyed_us"] for burst in selected])))
        print("  %-4s span_us %s" % (kind, stats([burst["tx_span_us"] for burst in selected])))

    # burst ends
    for slot in (0, 1):
        window_start, window_end = slots[slot]
        instants, done, logged = [], [], []
        for burst in own:
            if burst["callsign"]:
                continue
            second = burst["at_s"]
            if second % 10 != ground or (second // 10) & 1 != slot:
                continue
            instant = window_start + mix(address ^ mix(second)) % (window_end - window_start + 1)
            instants.append(instant)
            done.append(instant + (burst["tx_span_us"] + 999) // 1000)
            logged.append(burst["into_ms"])
        if instants:
            print("  slot %d window %d..%d: instant max %d, instant+span max %d, record into_ms max %d (n=%d)"
                  % (slot, window_start, window_end, max(instants), max(done), max(logged), len(instants)))
    names = [burst for burst in own if burst["callsign"]]
    if names:
        into = [1000 + burst["into_ms"] for burst in names]
        key = [1000 + burst["into_ms"] - (burst["tx_span_us"] + 999) // 1000 for burst in names]
        print("  name: record into_ms(+1000) max %d, record-span (deadline lower bound) max %d, n=%d"
              % (max(into), max(key), len(names)))
    bad = [burst for burst in own if not burst["callsign"] and burst["at_s"] % 10 != ground]
    if bad:
        print("  !! %d position bursts off the ground second" % len(bad))

    # dwell ends
    dwells = [record for record in records if record["type"] == "dwell"]
    ends = collections.defaultdict(collections.Counter)
    for dwell in dwells:
        ends[dwell["state"]][(dwell["start_ms"], dwell["end_ms"])] += 1
    for state, counts in sorted(ends.items()):
        print("  dwell %-14s (start,end) %s" % (state, dict(counts.most_common(4))))
    noise = collections.defaultdict(list)
    for dwell in dwells:
        noise[dwell["band"]].append(dwell["noise_dbm"])
    for band, levels in sorted(noise.items()):
        sampled = [level for level in levels if level != 0]
        print("  noise %s: %d dwells, %d sampled, %s" % (band, len(levels), len(sampled), stats(sampled)))
    print("  refusals", dict(collections.Counter(dwell["refusal"] for dwell in dwells)))

    # reinits and late switches
    switches = [record for record in records if record["type"] == "switch"]
    slow = [record for record in switches if record["took_us"] > 3000 or record["late"]]
    print("  reinit/late switches %d: %s" % (len(slow), collections.Counter(
        (record["change"], record["into_ms"], record["late"]) for record in slow).most_common(6)))

    # PPS
    pps = [record for record in records if record["type"] == "pps"]
    if pps:
        holdover = [record["holdover_events"] for record in pps]
        print("  pps %d: holdover_events %d..%d, since_edge 65535: %d, unlocked %d, since_edge==1000: %d, "
              "stamped >=990ms: %d, error_us %s" % (
                  len(pps), min(holdover), max(holdover), sum(record["since_edge_ms"] == 65535 for record in pps),
                  sum(not record["locked"] for record in pps), sum(record["since_edge_ms"] == 1000 for record in pps),
                  sum(record["into_ms"] >= 990 for record in pps), stats([record["error_us"] for record in pps])))

    # GNSS
    gnss = [record for record in records if record["type"] == "gnss"]
    if gnss:
        lost = [record for record in gnss if not record["fix_valid"]]
        flips = sum(previous["fix_valid"] and not following["fix_valid"] for previous, following in zip(gnss, gnss[1:]))
        seconds = sorted({record["at_s"] for record in gnss})
        missing = [(previous - first_s, following - previous) for previous, following in zip(seconds, seconds[1:]) if following - previous > 1]
        print("  gnss %d: fix invalid %d (losses %d), rejects %s, missing seconds %s" % (
            len(gnss), len(lost), flips, dict(collections.Counter(record["reject"] for record in gnss)), missing[:10]))
        print("  gnss sats_in_view %s" % stats([record["sats_in_view"] for record in gnss]))
    pages = collections.Counter(record["page"] for record in records if record["type"] == "screen")
    print("  screen pages (records) %s" % dict(pages))
    return address, own


def cross(name_a, records_a, address_a, own_a, name_b, records_b):
    rx = [record for record in records_b if record["type"] == "burst" and record["verdict"] in ("received", "named")
          and record.get("address") == address_a]
    overlap_start = max(records_a[0]["at_s"], records_b[0]["at_s"]) + 1
    overlap_end = min(max(record["at_s"] for record in records_a), max(record["at_s"] for record in records_b)) - 1
    heard_at = sorted(record["at_s"] + record["into_ms"] / 1000 for record in rx)
    import bisect
    hit = collections.Counter()
    total = collections.Counter()
    missed = []
    for burst in own_a:
        if not overlap_start <= burst["at_s"] <= overlap_end:
            continue
        kind = "name" if burst["callsign"] else "pos"
        total[kind] += 1
        sent_at = burst["at_s"] + burst["into_ms"] / 1000
        i = bisect.bisect_left(heard_at, sent_at - 0.030)
        if i < len(heard_at) and heard_at[i] <= sent_at + 0.030:
            hit[kind] += 1
        else:
            missed.append((burst["at_s"] - overlap_start, burst["into_ms"], kind))
    lengths = collections.Counter(record["len"] for record in rx)
    rssi = [record["rssi_dbm"] for record in rx]
    unframed = sum(1 for record in records_b if record["type"] == "burst" and record["verdict"] == "unframed"
                   and overlap_start <= record["at_s"] <= overlap_end)
    print("  %s -> %s over %d s: %s of %s heard, rx len %s, rssi %s, unframed at rx %d, missed %s" % (
        name_a, name_b, overlap_end - overlap_start, dict(hit), dict(total), dict(lengths), stats(rssi), unframed, missed[:10]))


def main():
    paths = sys.argv[1:]
    loaded = []
    for path in paths:
        records = load(path)
        address, own = report(path, records)
        loaded.append((path.split("/")[-1], records, address, own))
    print("== reception cross-match (within 30 ms)")
    for first in loaded:
        for second in loaded:
            if first is not second:
                cross(first[0], first[1], first[2], first[3], second[0], second[1])


if __name__ == "__main__":
    main()
