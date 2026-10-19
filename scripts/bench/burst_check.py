"""Burst verdicts, keying, the dwell race signature, queued-ahead share, position and name delivery.

usage: python3 scripts/bench/burst_check.py <capture.ndjson>...
"""
import collections
import statistics
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records

WORD_MASK = 0xFFFFFFFF
EDGES = (205, 400, 800)


def mix(value):
    value ^= value >> 16
    value = (value * 0x7FEB352D) & WORD_MASK
    value ^= value >> 15
    value = (value * 0x846CA68B) & WORD_MASK
    value ^= value >> 16
    return value


def report(path):
    records = [record for record in blip_records.read_ndjson(path) if record.get("type") not in blip_records.NOTE_TYPES]
    address = next(record["address"] for record in records if record["type"] == "config")
    ground = mix(address) % 10
    first_s, last_s = records[0]["at_s"], max(record["at_s"] for record in records)
    by_type = collections.defaultdict(list)
    for record in records:
        by_type[record["type"]].append(record)
    print("== %s  ground %d  span %d s" % (path.split("/")[-1], ground, last_s - first_s))

    bursts = [burst for burst in by_type["burst"] if burst["source"] != "received" and burst.get("address") == 0 or burst["verdict"] != "received"]
    own = [burst for burst in by_type["burst"] if burst["verdict"] not in ("received", "named", "unframed")]
    heard = collections.Counter(burst["verdict"] for burst in by_type["burst"] if burst["verdict"] in ("received", "named", "unframed"))
    print("  heard", dict(heard))
    print("  verdicts", dict(collections.Counter((burst["verdict"], "name" if burst["callsign"] else "pos") for burst in own)))
    keyed = sorted(burst["tx_keyed_us"] for burst in own if burst["verdict"] == "transmitted")
    if keyed:
        print("  tx_keyed_us median %d p99 %d max %d, >=2000: %d" % (
            statistics.median(keyed), keyed[min(len(keyed) - 1, int(len(keyed) * .99))], keyed[-1],
            sum(keyed_us >= 2000 for keyed_us in keyed)))
    for burst in own:
        if burst["verdict"] != "transmitted" or burst["tx_keyed_us"] >= 2000:
            print("   odd burst", burst["at_s"] - first_s, {key: burst[key] for key in ("into_ms", "verdict", "band", "callsign", "tx_keyed_us")})

    race = [dwell for dwell in by_type["dwell"] if dwell["start_ms"] in (400, 800) and dwell["phase_ms"] - dwell["start_ms"] in (0, 1)
            and not dwell["burst_armed"]]
    print("  race signature (400/800, phase==start|+1, not burst_armed): %d" % len(race))
    for dwell in race[:5]:
        print("   ", dwell["at_s"] - first_s, {key: dwell[key] for key in ("start_ms", "phase_ms", "state", "band", "refusal", "armed")})

    for edge in EDGES:
        dwells = [dwell for dwell in by_type["dwell"] if dwell["start_ms"] == edge]
        if not dwells:
            continue
        ahead = [dwell for dwell in dwells if dwell["phase_ms"] < edge or (edge == 205 and dwell["phase_ms"] >= 700)]
        rest = [dwell for dwell in dwells if dwell not in ahead]
        late = [dwell for dwell in rest if dwell["phase_ms"] - edge > 1 and not dwell["burst_armed"]]
        print("  start %d: %d dwells, %d queued ahead (%.1f%%), at edge %d, joined in flight %d, late %s" % (
            edge, len(dwells), len(ahead), 100 * len(ahead) / len(dwells),
            sum(dwell["phase_ms"] - edge in (0, 1) for dwell in rest),
            sum(dwell["burst_armed"] and dwell["phase_ms"] - edge > 1 for dwell in rest),
            sorted(dwell["phase_ms"] - edge for dwell in late)))
    print("  refusals", dict(collections.Counter(dwell["refusal"] for dwell in by_type["dwell"])))

    sent = collections.defaultdict(list)
    for burst in own:
        if burst["verdict"] == "transmitted":
            sent[burst["at_s"]].append(burst)
    gnss = {gnss_record["at_s"]: gnss_record for gnss_record in by_type["gnss"]}
    windows = missed = 0
    missed_list = []
    for second in range(first_s + 1, last_s):
        if second % 10 != ground:
            continue
        windows += 1
        positions = [burst for around in (second - 1, second, second + 1) for burst in sent[around] if not burst["callsign"]]
        if not positions:
            missed += 1
            gnss_record = gnss.get(second)
            missed_list.append((second - first_s, (second // 10) & 1, gnss_record and gnss_record["fix_valid"], gnss_record and gnss_record["solution_phase_ms"]))
    print("  all-seconds position windows %d, missed %d  (rel_s, slot, fix_valid, nav_ms): %s" % (
        windows, missed, missed_list))

    names = [burst for burst in own if burst["callsign"] and burst["verdict"] == "transmitted"]
    if names:
        bad = []
        for burst in names:
            second = burst["at_s"] - 1
            positions = [position for position in sent[second] if not position["callsign"]]
            ok = second % 10 == ground and burst["into_ms"] < 200 and positions
            if not ok:
                bad.append((burst["at_s"] - first_s, burst["into_ms"], second % 10, len(positions)))
        gaps = collections.Counter(later["at_s"] - earlier["at_s"] for earlier, later in zip(names, names[1:]))
        into = sorted(burst["into_ms"] for burst in names)
        slot_of = collections.Counter(((burst["at_s"] - 1) // 10) & 1 for burst in names)
        print("  names %d, into_ms %d..%d, by slot of ground second %s, spacing %s, misplaced %s" % (
            len(names), into[0], into[-1], dict(slot_of), dict(gaps), bad))
        name_s = {burst["at_s"] - 1 for burst in names}
        no_name = [second - first_s for second in range(first_s + 1, last_s - 1) if second % 10 == ground and second not in name_s]
        print("  ground seconds without a name: %s" % no_name)

    print("  link", dict(collections.Counter(record["action"] for record in by_type["link"])),
          "max drops", max((record["drops"] for record in by_type["link"]), default=0))


for path in sys.argv[1:]:
    report(path)
