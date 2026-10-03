"""Burst verdicts, keying, the dwell race signature, queued-ahead share, position and name delivery.

usage: python3 scripts/bench/burst_check.py <capture.ndjson>...
"""
import collections
import statistics
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records as r

M = 0xFFFFFFFF
EDGES = (205, 400, 800)


def mix(x):
    x ^= x >> 16
    x = (x * 0x7FEB352D) & M
    x ^= x >> 15
    x = (x * 0x846CA68B) & M
    x ^= x >> 16
    return x


def report(path):
    recs = [x for x in r.read_ndjson(path) if x.get("type") not in r.NOTE_TYPES]
    addr = next(x["addr"] for x in recs if x["type"] == "config")
    ground = mix(addr) % 10
    first_s, last_s = recs[0]["at_s"], max(x["at_s"] for x in recs)
    by = collections.defaultdict(list)
    for x in recs:
        by[x["type"]].append(x)
    print("== %s  ground %d  span %d s" % (path.split("/")[-1], ground, last_s - first_s))

    bursts = [b for b in by["burst"] if b["source"] != "received" and b.get("addr") == 0 or b["verdict"] != "received"]
    own = [b for b in by["burst"] if b["verdict"] not in ("received", "named", "unframed")]
    heard = collections.Counter(b["verdict"] for b in by["burst"] if b["verdict"] in ("received", "named", "unframed"))
    print("  heard", dict(heard))
    print("  verdicts", dict(collections.Counter((b["verdict"], "name" if b["callsign"] else "pos") for b in own)))
    keyed = sorted(b["tx_keyed_us"] for b in own if b["verdict"] == "transmitted")
    if keyed:
        print("  tx_keyed_us median %d p99 %d max %d, >=2000: %d" % (
            statistics.median(keyed), keyed[min(len(keyed) - 1, int(len(keyed) * .99))], keyed[-1],
            sum(k >= 2000 for k in keyed)))
    for b in own:
        if b["verdict"] != "transmitted" or b["tx_keyed_us"] >= 2000:
            print("   odd burst", b["at_s"] - first_s, {k: b[k] for k in ("into_ms", "verdict", "band", "callsign", "tx_keyed_us")})

    race = [d for d in by["dwell"] if d["start_ms"] in (400, 800) and d["phase_ms"] - d["start_ms"] in (0, 1)
            and not d["burst_armed"]]
    print("  race signature (400/800, phase==start|+1, not burst_armed): %d" % len(race))
    for d in race[:5]:
        print("   ", d["at_s"] - first_s, {k: d[k] for k in ("start_ms", "phase_ms", "state", "band", "refusal", "armed")})

    for edge in EDGES:
        ds = [d for d in by["dwell"] if d["start_ms"] == edge]
        if not ds:
            continue
        ahead = [d for d in ds if d["phase_ms"] < edge or (edge == 205 and d["phase_ms"] >= 700)]
        rest = [d for d in ds if d not in ahead]
        late = [d for d in rest if d["phase_ms"] - edge > 1 and not d["burst_armed"]]
        print("  start %d: %d dwells, %d queued ahead (%.1f%%), at edge %d, joined in flight %d, late %s" % (
            edge, len(ds), len(ahead), 100 * len(ahead) / len(ds),
            sum(d["phase_ms"] - edge in (0, 1) for d in rest),
            sum(d["burst_armed"] and d["phase_ms"] - edge > 1 for d in rest),
            sorted(d["phase_ms"] - edge for d in late)))
    print("  refusals", dict(collections.Counter(d["refusal"] for d in by["dwell"])))

    sent = collections.defaultdict(list)
    for b in own:
        if b["verdict"] == "transmitted":
            sent[b["at_s"]].append(b)
    gnss = {g["at_s"]: g for g in by["gnss"]}
    windows = missed = 0
    missed_list = []
    for s in range(first_s + 1, last_s):
        if s % 10 != ground:
            continue
        windows += 1
        pos = [b for t in (s - 1, s, s + 1) for b in sent[t] if not b["callsign"]]
        if not pos:
            missed += 1
            g = gnss.get(s)
            missed_list.append((s - first_s, (s // 10) & 1, g and g["fix_valid"], g and g["nav_ms"]))
    print("  all-seconds position windows %d, missed %d  (rel_s, slot, fix_valid, nav_ms): %s" % (
        windows, missed, missed_list))

    names = [b for b in own if b["callsign"] and b["verdict"] == "transmitted"]
    if names:
        bad = []
        for b in names:
            s = b["at_s"] - 1
            pos = [p for p in sent[s] if not p["callsign"]]
            ok = s % 10 == ground and b["into_ms"] < 200 and pos
            if not ok:
                bad.append((b["at_s"] - first_s, b["into_ms"], s % 10, len(pos)))
        gaps = collections.Counter(b2["at_s"] - b1["at_s"] for b1, b2 in zip(names, names[1:]))
        into = sorted(b["into_ms"] for b in names)
        slot_of = collections.Counter(((b["at_s"] - 1) // 10) & 1 for b in names)
        print("  names %d, into_ms %d..%d, by slot of ground second %s, spacing %s, misplaced %s" % (
            len(names), into[0], into[-1], dict(slot_of), dict(gaps), bad))
        name_s = {b["at_s"] - 1 for b in names}
        no_name = [s - first_s for s in range(first_s + 1, last_s - 1) if s % 10 == ground and s not in name_s]
        print("  ground seconds without a name: %s" % no_name)

    print("  link", dict(collections.Counter(x["action"] for x in by["link"])),
          "max drops", max((x["drops"] for x in by["link"]), default=0))


for path in sys.argv[1:]:
    report(path)
